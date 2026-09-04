#include "server.hpp"

#include <chrono>
#include <iostream>
#include <thread>

#include <httplib.h>

namespace em::internal {

using json = nlohmann::json;

struct AgentServer::Impl {
    httplib::Server svr;
};

AgentServer::AgentServer(Identity& identity, std::shared_ptr<Filter> filter,
                          std::mutex& filter_mutex,
                          std::string host, int port)
    : identity_(identity)
    , filter_(std::move(filter))
    , filter_mutex_(filter_mutex)
    , host_(std::move(host))
    , port_(port)
    , impl_(std::make_unique<Impl>())
{
    impl_->svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("ok", "text/plain");
    });

    impl_->svr.Post("/agent/query",
        [this](const httplib::Request& req, httplib::Response& res) {
            std::string query;
            try {
                json body = json::parse(req.body);
                if (!body.at("query").is_string()) throw std::runtime_error("query not a string");
                query = body.at("query").get<std::string>();
            } catch (const std::exception&) {
                res.status = 400;
                res.set_content(json{{"error", "bad query"}}.dump(), "application/json");
                return;
            }

            json results;
            try {
                std::lock_guard<std::mutex> lock(filter_mutex_);
                results = filter_->handle(query, memory_);
            } catch (const std::exception& e) {
                res.status = 500;
                res.set_content(json{{"error", e.what()}}.dump(), "application/json");
                return;
            }

            auto [signer_id, signature] = identity_.sign_results(results);
            res.set_content(json{
                {"results", results},
                {"signer_id", signer_id},
                {"signature", signature},
            }.dump(), "application/json");
        });

    impl_->svr.Post("/pop/gossip",
        [this](const httplib::Request&, httplib::Response& res) {
            // Minimal: we don't keep a peer table (SDKs don't run cosine
            // routing), just acknowledge with our own self-payload so the
            // remote side can bind us.
            res.set_content(identity_.gossip_payload(host_, port_).dump(), "application/json");
        });
}

AgentServer::~AgentServer() {
    stop();
}

void AgentServer::run() {
    if (port_ == 0) {
        port_ = impl_->svr.bind_to_any_port(host_.c_str());
        if (port_ < 0) {
            std::cerr << "[em_filter] AgentServer failed to bind on " << host_ << "\n";
            return;
        }
        impl_->svr.listen_after_bind();
    } else {
        if (!impl_->svr.bind_to_port(host_.c_str(), port_)) {
            std::cerr << "[em_filter] AgentServer failed to bind on "
                      << host_ << ":" << port_ << "\n";
            return;
        }
        impl_->svr.listen_after_bind();
    }
}

void AgentServer::stop() {
    impl_->svr.stop();
}

void AgentServer::wait_until_ready() {
    impl_->svr.wait_until_ready();
}

void gossip_push_loop(Identity& identity,
                       const std::vector<DiscoNode>& seeds,
                       const std::string& advertise_host, int query_port,
                       int interval_ms) {
    for (;;) {
        for (const auto& seed : seeds) {
            std::string base = std::string(seed.tls ? "https://" : "http://")
                              + seed.host + ":" + std::to_string(seed.port);
            httplib::Client cli(base);
            cli.set_connection_timeout(3);
            cli.set_read_timeout(3);
            auto payload = identity.gossip_payload(advertise_host, query_port).dump();
            auto res = cli.Post("/pop/gossip", payload, "application/json");
            if (!res) {
                std::cerr << "[em_filter] gossip push to " << seed.host << ":"
                          << seed.port << " failed\n";
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
}

} // namespace em::internal
