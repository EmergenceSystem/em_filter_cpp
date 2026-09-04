#pragma once
#include "em_filter/config.hpp"
#include <string>
#include <vector>

namespace em::internal {

std::vector<DiscoNode> resolve_nodes(const AgentConfig& cfg);
std::optional<std::string> resolve_jwt(const AgentConfig& cfg);
int reconnect_ms();

bool infer_tls(const std::string& host, int port);
std::pair<int,bool> default_port_tls(const std::string& host);

/** EM_FILTER_KEY_DIR env, else "./empop_key_<name>/" (mirrors the Erlang
 *  per-port fallback). */
std::string resolve_key_dir(const std::string& name);

/** EM_FILTER_MODE env: "relay" (default) | "direct" | "both". */
std::string filter_mode();

/** EM_FILTER_QUERY_PORT env, default 9600 -- Model A listen/advertise port. */
int query_port();

/** EM_FILTER_HOST env, default "0.0.0.0" -- Model A bind/advertise host. */
std::string advertise_host();

/** EM_FILTER_GOSSIP_INTERVAL_MS env, default 5000. */
int gossip_interval_ms();

} // namespace em::internal
