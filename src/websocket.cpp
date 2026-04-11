#include "websocket.hpp"
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib,"ws2_32.lib")
#  define close_sock(fd) closesocket(fd)
#else
#  include <sys/socket.h>
#  include <netdb.h>
#  include <unistd.h>
#  define close_sock(fd) ::close(fd)
#endif

#include <openssl/rand.h>

namespace em::internal {

// -- Base64 ----------------------------------------------------------------

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const uint8_t* src, size_t len) {
    std::string out;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = (uint32_t)src[i] << 16;
        if (i+1 < len) b |= (uint32_t)src[i+1] << 8;
        if (i+2 < len) b |= (uint32_t)src[i+2];
        out += B64[(b>>18)&0x3f];
        out += B64[(b>>12)&0x3f];
        out += (i+1 < len) ? B64[(b>>6)&0x3f] : '=';
        out += (i+2 < len) ? B64[(b   )&0x3f] : '=';
    }
    return out;
}

// -- I/O helpers -----------------------------------------------------------

static int sock_write(WsConn& ws, const void* buf, size_t len) {
    const char* p = static_cast<const char*>(buf);
    size_t sent = 0;
    while (sent < len) {
        int n = ws.ssl
            ? SSL_write(ws.ssl, p+sent, (int)(len-sent))
            : (int)::send(ws.fd, p+sent, (int)(len-sent), 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int sock_read_exact(WsConn& ws, void* buf, size_t len) {
    char* p = static_cast<char*>(buf);
    size_t got = 0;
    while (got < len) {
        int n = ws.ssl
            ? SSL_read(ws.ssl, p+got, (int)(len-got))
            : (int)::recv(ws.fd, p+got, (int)(len-got), 0);
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

static int read_http_headers(WsConn& ws, std::string& out) {
    out.clear();
    char c;
    while (true) {
        if (sock_read_exact(ws, &c, 1) < 0) return -1;
        out += c;
        if (out.size() >= 4) {
            auto& s = out;
            if (s[s.size()-4]=='\r' && s[s.size()-3]=='\n' &&
                s[s.size()-2]=='\r' && s[s.size()-1]=='\n') return 0;
        }
        if (out.size() > 8192) return -1;
    }
}

// -- TCP connect -----------------------------------------------------------

static int tcp_connect(const std::string& host, int port) {
#ifdef _WIN32
    static bool wsa_init = false;
    if (!wsa_init) { WSADATA d; WSAStartup(MAKEWORD(2,2),&d); wsa_init=true; }
#endif
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    std::string ps = std::to_string(port);
    if (getaddrinfo(host.c_str(), ps.c_str(), &hints, &res) != 0) return -1;

    int fd = -1;
    for (auto* r = res; r; r = r->ai_next) {
        fd = (int)socket(r->ai_family, r->ai_socktype, r->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, r->ai_addr, (int)r->ai_addrlen) == 0) break;
        close_sock(fd); fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

// -- Public: connect -------------------------------------------------------

bool WsConn::connect(const std::string& host, int port, bool tls,
                     const std::string& path) {
    fd = tcp_connect(host, port);
    if (fd < 0) return false;

    if (tls) {
        ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) { close_sock(fd); fd=-1; return false; }
        SSL_CTX_set_default_verify_paths(ctx);
        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, fd);
        SSL_set_tlsext_host_name(ssl, host.c_str());
        if (SSL_connect(ssl) != 1) {
            SSL_free(ssl); SSL_CTX_free(ctx);
            close_sock(fd); ssl=nullptr; ctx=nullptr; fd=-1; return false;
        }
    }

    uint8_t key_bytes[16];
    RAND_bytes(key_bytes, 16);
    auto key = base64_encode(key_bytes, 16);

    std::string req =
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + host + ":" + std::to_string(port) + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + key + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    if (sock_write(*this, req.data(), req.size()) < 0) { close(); return false; }

    std::string resp;
    if (read_http_headers(*this, resp) < 0) { close(); return false; }
    if (resp.find("101") == std::string::npos) { close(); return false; }
    return true;
}

// -- Public: send text frame -----------------------------------------------

bool WsConn::send_text(const std::string& text) {
    size_t plen = text.size();
    uint8_t hdr[10];
    int hlen;
    hdr[0] = 0x81;
    if (plen <= 125) {
        hdr[1] = (uint8_t)(0x80 | plen); hlen = 2;
    } else if (plen <= 65535) {
        hdr[1] = 0x80|126;
        hdr[2] = (uint8_t)(plen>>8); hdr[3] = (uint8_t)(plen&0xff); hlen=4;
    } else {
        hdr[1]=0x80|127;
        for(int i=0;i<8;i++) hdr[2+i]=(uint8_t)(plen>>(56-8*i)); hlen=10;
    }

    uint8_t mask[4];
    RAND_bytes(mask, 4);

    size_t total = (size_t)hlen + 4 + plen;
    std::vector<uint8_t> buf(total);
    std::memcpy(buf.data(), hdr, (size_t)hlen);
    std::memcpy(buf.data()+hlen, mask, 4);
    for (size_t i = 0; i < plen; i++)
        buf[hlen+4+i] = (uint8_t)text[i] ^ mask[i%4];

    return sock_write(*this, buf.data(), total) == 0;
}

// -- Public: recv one text frame -------------------------------------------

std::optional<std::string> WsConn::recv_text() {
    for(;;) {
        uint8_t hdr[2];
        if (sock_read_exact(*this, hdr, 2) < 0) return std::nullopt;

        uint8_t  opcode = hdr[0] & 0x0f;
        bool     masked = (hdr[1] & 0x80) != 0;
        uint64_t plen   = hdr[1] & 0x7f;

        if (plen == 126) {
            uint8_t ext[2];
            if (sock_read_exact(*this, ext, 2)<0) return std::nullopt;
            plen = ((uint64_t)ext[0]<<8)|ext[1];
        } else if (plen == 127) {
            uint8_t ext[8];
            if (sock_read_exact(*this, ext, 8)<0) return std::nullopt;
            plen=0; for(int i=0;i<8;i++) plen=(plen<<8)|ext[i];
        }

        uint8_t fmask[4]={};
        if (masked && sock_read_exact(*this, fmask, 4)<0) return std::nullopt;

        std::string payload(plen, '\0');
        if (plen > 0 && sock_read_exact(*this, payload.data(), (size_t)plen)<0) return std::nullopt;
        if (masked) for(size_t i=0;i<plen;i++) payload[i]^=fmask[i%4];

        if (opcode==0x8) return std::nullopt;     // CLOSE
        if (opcode==0x9||opcode==0xa) continue;   // PING/PONG
        return payload;
    }
}

// -- Public: close ---------------------------------------------------------

void WsConn::close() {
    if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); ssl=nullptr; }
    if (ctx) { SSL_CTX_free(ctx); ctx=nullptr; }
    if (fd>=0) { close_sock(fd); fd=-1; }
}

} // namespace em::internal
