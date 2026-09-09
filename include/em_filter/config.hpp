#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace em {

struct DiscoNode {
    std::string host;
    uint16_t    port = 8080;
    bool        tls  = false;
};

/**
 * Configuration for a filter agent.
 * All fields are optional — empty/unset means auto-resolve.
 */
struct AgentConfig {
    std::optional<std::string> jwt_token;
    std::vector<DiscoNode>     disco_nodes;

    /** Default config: all fields empty → auto-resolve from env/conf/default */
    AgentConfig() = default;
};

} // namespace em
