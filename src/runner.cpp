#include "em_filter/runner.hpp"
#include "connection.hpp"
#include "config_internal.hpp"
#include <thread>
#include <mutex>
#include <iostream>
#include <vector>

namespace em {

FilterRunner::FilterRunner(std::string name,
                            std::shared_ptr<Filter> filter,
                            AgentConfig config)
    : name_(std::move(name))
    , filter_(std::move(filter))
    , config_(std::move(config))
{}

void FilterRunner::run() {
    auto nodes      = internal::resolve_nodes(config_);
    auto jwt        = internal::resolve_jwt(config_);
    int  reconnect  = internal::reconnect_ms();

    std::cout << "[em_filter] Starting agent '" << name_
              << "' on " << nodes.size() << " node(s)\n";

    std::mutex filter_mutex;
    std::vector<std::thread> threads;

    for (const auto& node : nodes) {
        threads.emplace_back([&, node]() {
            internal::run_connection(name_, node, filter_,
                                     filter_mutex, jwt, reconnect);
        });
    }

    for (auto& t : threads) t.join();
}

} // namespace em
