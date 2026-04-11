#pragma once
#include <string>
#include <optional>
#include <openssl/ssl.h>

namespace em::internal {

struct WsConn {
    int      fd  = -1;
    SSL*     ssl = nullptr;
    SSL_CTX* ctx = nullptr;

    WsConn() = default;
    WsConn(const WsConn&) = delete;
    WsConn& operator=(const WsConn&) = delete;
    ~WsConn() { close(); }

    // Returns true on success
    bool connect(const std::string& host, int port, bool tls, const std::string& path);

    // Returns false on error
    bool send_text(const std::string& text);

    // Returns nullopt on close/error, empty string on ignored frames
    std::optional<std::string> recv_text();

    void close();
};

} // namespace em::internal
