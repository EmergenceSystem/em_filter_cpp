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

} // namespace em::internal
