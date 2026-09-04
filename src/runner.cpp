#include "em_filter/runner.hpp"
#include "em_filter/identity.hpp"
#include "config_internal.hpp"
#include "server.hpp"
#include "wsclient.hpp"

#include <iostream>
#include <mutex>
#include <thread>
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
    std::string mode = internal::filter_mode();
    std::string key_dir = internal::resolve_key_dir(name_);
    Identity identity(name_, key_dir, filter_->capabilities());

    std::cout << "[em_filter] Starting agent '" << name_ << "' id="
              << crypto::b64_encode(identity.id()) << " mode=" << mode << "\n";

    std::mutex filter_mutex;
    std::vector<std::thread> threads;

    if (mode == "direct" || mode == "both") {
        int port = internal::query_port();
        std::string host = internal::advertise_host();
        int interval = internal::gossip_interval_ms();
        auto seeds = internal::resolve_nodes(config_);

        auto server = std::make_shared<internal::AgentServer>(
            identity, filter_, filter_mutex, host, port);
        threads.emplace_back([server]() { server->run(); });
        threads.emplace_back([&identity, seeds, host, port, interval]() {
            internal::gossip_push_loop(identity, seeds, host, port, interval);
        });
    }

    if (mode == "relay" || mode == "both") {
        auto nodes = internal::resolve_nodes(config_);
        int reconnect = internal::reconnect_ms();

        for (const auto& node : nodes) {
            threads.emplace_back([&identity, this, &filter_mutex, node, reconnect]() {
                internal::RelayClient client(identity, filter_, filter_mutex, node, reconnect);
                client.run_forever();
            });
        }
    }

    if (threads.empty()) {
        std::cerr << "[em_filter] " << name_ << " unknown EM_FILTER_MODE '" << mode
                  << "' (expected relay|direct|both) -- nothing to run\n";
        return;
    }

    for (auto& t : threads) t.join();
}

} // namespace em
