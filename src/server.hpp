#pragma once
#include "em_filter/config.hpp"
#include "em_filter/filter.hpp"
#include "em_filter/identity.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace em::internal {

/**
 * Model A: serves inbound HTTP.
 *   POST /agent/query  -- {"query": "..."} -> {results, signer_id, signature}
 *   POST /pop/gossip   -- accept a remote gossip payload, reply own self-payload
 *   GET  /health       -- "ok"
 */
class AgentServer {
public:
    AgentServer(Identity& identity, std::shared_ptr<Filter> filter,
                std::mutex& filter_mutex,
                std::string host, int port);
    ~AgentServer();

    AgentServer(const AgentServer&) = delete;
    AgentServer& operator=(const AgentServer&) = delete;

    /** Binds and serves requests until stop() is called. Blocks. */
    void run();

    /** Stop serving; safe to call from another thread. */
    void stop();

    /** Blocks until run() has bound the listening socket and is ready to accept. */
    void wait_until_ready();

    /** Actual bound port (resolved after run() binds, useful when constructed with port 0). */
    int port() const { return port_; }

private:
    Identity& identity_;
    std::shared_ptr<Filter> filter_;
    std::mutex& filter_mutex_;
    std::string host_;
    int port_;
    nlohmann::json memory_ = nlohmann::json::object();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Model A: push identity.gossip_payload() to each seed's /pop/gossip every
 * interval_ms, forever. Intended to run on its own thread.
 */
void gossip_push_loop(Identity& identity,
                       const std::vector<DiscoNode>& seeds,
                       const std::string& advertise_host, int query_port,
                       int interval_ms);

} // namespace em::internal
