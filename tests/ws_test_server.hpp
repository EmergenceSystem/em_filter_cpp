#pragma once
// Minimal single-connection WebSocket test server -- a stub "disco" used only
// by tests/test_wsclient.cpp to drive RelayClient. Not for production use:
// no TLS, no fragmentation, blocking, one connection at a time.

#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  define EM_WS_TEST_CLOSESOCK ::closesocket
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#  define EM_WS_TEST_CLOSESOCK ::close
#endif

#include <openssl/sha.h>

namespace em::test {

class WsTestServer {
public:
    WsTestServer() {
#ifdef _WIN32
        static bool wsa_init = [] { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); return true; }();
        (void)wsa_init;
#endif
        listen_fd_ = (int)::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) throw std::runtime_error("ws test server: socket() failed");

        int yes = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&yes), sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
            throw std::runtime_error("ws test server: bind() failed");

        socklen_t len = sizeof(addr);
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);

        if (::listen(listen_fd_, 1) != 0)
            throw std::runtime_error("ws test server: listen() failed");
    }

    ~WsTestServer() { close(); }

    WsTestServer(const WsTestServer&) = delete;
    WsTestServer& operator=(const WsTestServer&) = delete;

    int port() const { return port_; }

    /** Blocks until a client connects and the WS upgrade handshake completes. */
    void accept_and_handshake() {
        conn_fd_ = (int)::accept(listen_fd_, nullptr, nullptr);
        if (conn_fd_ < 0) throw std::runtime_error("ws test server: accept() failed");

        std::string req = read_http_request();
        std::string key = extract_header(req, "Sec-WebSocket-Key");
        std::string accept = ws_accept_key(key);

        std::string resp =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
        write_all(resp.data(), resp.size());
    }

    /** Send one unmasked text frame (server->client frames are never masked). */
    void send_text(const std::string& text) {
        size_t plen = text.size();
        std::vector<uint8_t> buf;
        buf.push_back(0x81);
        if (plen <= 125) {
            buf.push_back((uint8_t)plen);
        } else if (plen <= 65535) {
            buf.push_back(126);
            buf.push_back((uint8_t)(plen >> 8));
            buf.push_back((uint8_t)(plen & 0xff));
        } else {
            throw std::runtime_error("ws test server: payload too large");
        }
        buf.insert(buf.end(), text.begin(), text.end());
        write_all(reinterpret_cast<const char*>(buf.data()), buf.size());
    }

    /** Receive one text frame, unmasking client->server frames. nullopt on close/error. */
    std::optional<std::string> recv_text() {
        uint8_t hdr[2];
        if (read_exact(hdr, 2) < 0) return std::nullopt;

        uint8_t opcode = hdr[0] & 0x0f;
        bool masked = (hdr[1] & 0x80) != 0;
        uint64_t plen = hdr[1] & 0x7f;

        if (plen == 126) {
            uint8_t ext[2];
            if (read_exact(ext, 2) < 0) return std::nullopt;
            plen = ((uint64_t)ext[0] << 8) | ext[1];
        } else if (plen == 127) {
            uint8_t ext[8];
            if (read_exact(ext, 8) < 0) return std::nullopt;
            plen = 0;
            for (int i = 0; i < 8; i++) plen = (plen << 8) | ext[i];
        }

        uint8_t mask[4] = {};
        if (masked && read_exact(mask, 4) < 0) return std::nullopt;

        std::string payload(plen, '\0');
        if (plen > 0 && read_exact(payload.data(), (size_t)plen) < 0) return std::nullopt;
        if (masked) {
            for (size_t i = 0; i < plen; i++) payload[i] = (char)((uint8_t)payload[i] ^ mask[i % 4]);
        }

        if (opcode == 0x8) return std::nullopt; // CLOSE
        return payload;
    }

    void close() {
        if (conn_fd_ >= 0) { EM_WS_TEST_CLOSESOCK(conn_fd_); conn_fd_ = -1; }
        if (listen_fd_ >= 0) { EM_WS_TEST_CLOSESOCK(listen_fd_); listen_fd_ = -1; }
    }

private:
    int listen_fd_ = -1;
    int conn_fd_ = -1;
    int port_ = 0;

    int read_exact(void* buf, size_t len) {
        char* p = static_cast<char*>(buf);
        size_t got = 0;
        while (got < len) {
            int n = (int)::recv(conn_fd_, p + got, (int)(len - got), 0);
            if (n <= 0) return -1;
            got += (size_t)n;
        }
        return 0;
    }

    void write_all(const char* buf, size_t len) {
        size_t sent = 0;
        while (sent < len) {
            int n = (int)::send(conn_fd_, buf + sent, (int)(len - sent), 0);
            if (n <= 0) throw std::runtime_error("ws test server: send() failed");
            sent += (size_t)n;
        }
    }

    std::string read_http_request() {
        std::string out;
        char c;
        for (;;) {
            if (read_exact(&c, 1) < 0) throw std::runtime_error("ws test server: handshake read failed");
            out += c;
            if (out.size() >= 4 && out.compare(out.size() - 4, 4, "\r\n\r\n") == 0) break;
            if (out.size() > 8192) throw std::runtime_error("ws test server: handshake too large");
        }
        return out;
    }

    static std::string extract_header(const std::string& req, const std::string& name) {
        auto pos = req.find(name + ":");
        if (pos == std::string::npos) return "";
        pos += name.size() + 1;
        auto end = req.find("\r\n", pos);
        std::string v = req.substr(pos, end - pos);
        auto l = v.find_first_not_of(" \t");
        auto r = v.find_last_not_of(" \t");
        return (l == std::string::npos) ? "" : v.substr(l, r - l + 1);
    }

    static std::string ws_accept_key(const std::string& client_key) {
        static const char* GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        std::string combined = client_key + GUID;
        unsigned char digest[SHA_DIGEST_LENGTH];
        SHA1(reinterpret_cast<const unsigned char*>(combined.data()), combined.size(), digest);
        return b64_encode(digest, SHA_DIGEST_LENGTH);
    }

    static std::string b64_encode(const unsigned char* data, size_t len) {
        static const char* B64 =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (size_t i = 0; i < len; i += 3) {
            uint32_t b = (uint32_t)data[i] << 16;
            if (i + 1 < len) b |= (uint32_t)data[i + 1] << 8;
            if (i + 2 < len) b |= (uint32_t)data[i + 2];
            out += B64[(b >> 18) & 0x3f];
            out += B64[(b >> 12) & 0x3f];
            out += (i + 1 < len) ? B64[(b >> 6) & 0x3f] : '=';
            out += (i + 2 < len) ? B64[b & 0x3f] : '=';
        }
        return out;
    }
};

} // namespace em::test
