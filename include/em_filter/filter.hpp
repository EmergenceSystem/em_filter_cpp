#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace em {

/**
 * Handler contract for an Emergence filter agent.
 *
 * Mirrors the Erlang em_filter handler:
 *   handle(Body, Memory) -> {Result, NewMemory}
 *
 * In C++ the memory is passed by reference and can be modified in-place
 * (equivalent to returning the same map), or replaced entirely.
 * The return value is the result JSON (array of embryos or null).
 *
 * Example:
 * @code
 * class MyFilter : public em::Filter {
 * public:
 *     nlohmann::json handle(const std::string& body, nlohmann::json& memory) override {
 *         return nlohmann::json::array({{
 *             {"type", "url"},
 *             {"properties", {{"url", "https://example.com"}, {"title", "Echo: " + body}}}
 *         }});
 *     }
 * };
 * @endcode
 */
class Filter {
public:
    virtual ~Filter() = default;

    /**
     * Handle an incoming query from em_disco.
     *
     * @param body   raw query string (e.g. "erlang otp")
     * @param memory current memory state (JSON object, persisted between calls)
     * @return       JSON result — typically an array of embryo objects.
     *               Return nullptr (json{}) or an empty array for "no results".
     */
    virtual nlohmann::json handle(const std::string& body,
                                   nlohmann::json& memory) = 0;

    /**
     * Capabilities announced in the agent_hello handshake frame.
     * em_disco uses these to route queries.
     */
    virtual std::vector<std::string> capabilities() const {
        return {"search", "query"};
    }
};

} // namespace em
