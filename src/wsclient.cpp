#include "wsclient.hpp"
#include "websocket.hpp"

#include <chrono>
#include <iostream>
#include <thread>

#include <nlohmann/json.hpp>

namespace em::internal {

using json = nlohmann::json;

RelayClient::RelayClient(Identity& identity, std::shared_ptr<Filter> filter,
                          std::mutex& filter_mutex, DiscoNode node, int reconnect_ms)
    : identity_(identity)
    , filter_(std::move(filter))
    , filter_mutex_(filter_mutex)
    , node_(std::move(node))
    , reconnect_ms_(reconnect_ms)
{}

void RelayClient::run_forever() {
    for (;;) {
        try {
            session();
        } catch (const std::exception& e) {
            std::cerr << "[em_filter] " << identity_.name()
                      << " relay session error: " << e.what() << "\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_ms_));
    }
}

bool RelayClient::session() {
    WsConn ws;
    if (!ws.connect(node_.host, node_.port, node_.tls, "/ws/filter")) {
        std::cerr << "[em_filter] " << identity_.name() << " relay connect failed ("
                  << node_.host << ":" << node_.port << ")\n";
        return false;
    }

    if (!ws.send_text(identity_.hello_payload().dump())) return false;

    auto raw_ack = ws.recv_text();
    if (!raw_ack) return false;
    json ack = json::parse(*raw_ack, nullptr, false);
    if (ack.is_discarded() || ack.value("action", "") != "hello_ok") {
        std::cerr << "[em_filter] " << identity_.name()
                  << " relay hello rejected: " << *raw_ack << "\n";
        return false;
    }

    std::cout << "[em_filter] " << identity_.name() << " relay connected to "
              << node_.host << ":" << node_.port << " -- entering query loop\n";

    json memory = json::object(); // resets on reconnect, like the direct path

    for (;;) {
        auto raw = ws.recv_text();
        if (!raw) break;

        json msg = json::parse(*raw, nullptr, false);
        if (msg.is_discarded() || msg.value("action", "") != "query") continue;

        std::string query_id = msg.value("id", "");
        std::string body = msg.value("body", "");

        json results;
        {
            std::lock_guard<std::mutex> lock(filter_mutex_);
            try {
                results = filter_->handle(body, memory);
            } catch (const std::exception& e) {
                std::cerr << "[em_filter] " << identity_.name()
                          << " handler error: " << e.what() << "\n";
                results = json::array();
            }
        }

        auto [signer_id, signature] = identity_.sign_results(results);
        if (!ws.send_text(json{
                {"action", "result"},
                {"id", query_id},
                {"results", results},
                {"signer_id", signer_id},
                {"signature", signature},
            }.dump())) {
            break;
        }
    }

    std::cout << "[em_filter] " << identity_.name() << " relay disconnected\n";
    return true;
}

} // namespace em::internal
