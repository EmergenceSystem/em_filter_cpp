#include "connection.hpp"
#include "websocket.hpp"
#include "config_internal.hpp"

#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>
#include <chrono>

namespace em::internal {

using json = nlohmann::json;

static void send_json(WsConn& ws, const json& obj) {
    ws.send_text(obj.dump());
}

static void handle_query(WsConn& ws,
                          const std::string& agent_name,
                          const std::string& query_id,
                          const std::string& body,
                          json& memory,
                          Filter& filter) {
    json result;
    try {
        result = filter.handle(body, memory);
    } catch (const std::exception& e) {
        std::cerr << "[em_filter] " << agent_name << " handler error: "
                  << e.what() << "\n";
        result = nullptr;
    }

    send_json(ws, {
        {"action", "result"},
        {"id",     query_id},
        {"data",   result},
    });
}

static void connect_once(const std::string& agent_name,
                          const DiscoNode& node,
                          Filter& filter,
                          std::mutex& mtx,
                          const std::optional<std::string>& jwt) {
    std::string path = jwt ? "/ws?token=" + *jwt : "/ws";
    std::string scheme = node.tls ? "wss" : "ws";

    std::cout << "[em_filter] " << agent_name << " connecting to "
              << scheme << "://" << node.host << ":" << node.port << "/ws\n";

    WsConn ws;
    if (!ws.connect(node.host, node.port, node.tls, path)) {
        std::cerr << "[em_filter] " << agent_name << " connect failed ("
                  << node.host << ":" << node.port << ")\n";
        return;
    }

    // Step 1: register
    send_json(ws, {{"action", "register"}, {"name", agent_name}});

    // Step 2: agent_hello
    {
        std::lock_guard<std::mutex> lock(mtx);
        send_json(ws, {
            {"action", "agent_hello"},
            {"capabilities", filter.capabilities()},
        });
    }

    std::cout << "[em_filter] " << agent_name
              << " registered -- entering message loop\n";

    json memory = json::object();  // starts as {}, like Erlang #{}

    for (;;) {
        auto raw = ws.recv_text();
        if (!raw) break;

        json msg;
        try { msg = json::parse(*raw); }
        catch (...) {
            std::cerr << "[em_filter] " << agent_name << " invalid JSON, skipping\n";
            continue;
        }

        if (!msg.contains("action") || msg["action"] != "query") continue;

        if (!msg.contains("id") || !msg["id"].is_string()) {
            std::cerr << "[em_filter] " << agent_name
                      << " query missing 'id', skipping\n";
            continue;
        }
        std::string query_id = msg["id"];
        std::string body     = msg.value("body", "");
        // trim leading/trailing whitespace
        auto l = body.find_first_not_of(" \t\r\n");
        auto r = body.find_last_not_of(" \t\r\n");
        body = (l == std::string::npos) ? "" : body.substr(l, r-l+1);

        std::cout << "[em_filter] " << agent_name << " query "
                  << query_id << ": " << body << "\n";

        std::lock_guard<std::mutex> lock(mtx);
        handle_query(ws, agent_name, query_id, body, memory, filter);
    }

    std::cout << "[em_filter] " << agent_name << " disconnected\n";
}

void run_connection(const std::string& agent_name,
                    const DiscoNode& node,
                    std::shared_ptr<Filter> filter,
                    std::mutex& filter_mutex,
                    const std::optional<std::string>& jwt_token,
                    int reconnect_ms) {
    for (;;) {
        connect_once(agent_name, node, *filter, filter_mutex, jwt_token);
        std::cout << "[em_filter] " << agent_name << " reconnecting in "
                  << reconnect_ms << "ms\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_ms));
    }
}

} // namespace em::internal
