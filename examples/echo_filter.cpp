/**
 * echo_filter -- minimal em_filter_cpp example agent.
 *
 * Build:
 *   cmake -B build && cmake --build build
 *   ./build/echo_filter
 *
 * Runs in Model B (WS relay, default) against a local disco:
 *   EM_DISCO_HOST=disco.example.com ./build/echo_filter
 *
 * Runs in Model A (direct HTTP), reachable from the mesh at query_port:
 *   EM_FILTER_MODE=direct EM_FILTER_QUERY_PORT=9600 \
 *   EM_DISCO_HOST=disco.example.com ./build/echo_filter
 *
 * See EM_FILTER_MODE=both to run both transports concurrently.
 */
#include <em_filter/filter.hpp>
#include <em_filter/runner.hpp>
#include <nlohmann/json.hpp>
#include <iostream>

class EchoFilter : public em::Filter {
public:
    nlohmann::json handle(const std::string& body,
                           nlohmann::json& /*memory*/) override {
        std::cout << "[echo_filter] query: " << body << "\n";
        return nlohmann::json::array({{
            {"type", "url"},
            {"properties", {
                {"url",   "https://example.com"},
                {"title", "Echo: " + body},
            }},
        }});
    }

    std::vector<std::string> capabilities() const override {
        return {"search", "query", "echo"};
    }
};

int main() {
    em::FilterRunner("echo_filter",
                     std::make_shared<EchoFilter>(),
                     em::AgentConfig{}).run();
}
