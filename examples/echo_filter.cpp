/**
 * echo_filter -- exemple minimal d'un agent em_filter C++.
 *
 * Build:
 *   cmake -B build && cmake --build build
 *   ./build/echo_filter
 *
 * Avec un broker distant:
 *   EM_DISCO_HOST=disco.example.com EM_DISCO_PORT=443 \
 *   EM_FILTER_JWT_TOKEN=eyJ... ./build/echo_filter
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
