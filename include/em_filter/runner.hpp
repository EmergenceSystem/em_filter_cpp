#pragma once
#include "filter.hpp"
#include "config.hpp"
#include <memory>
#include <string>

namespace em {

/**
 * Starts the transport(s) selected by `EM_FILTER_MODE` (relay | direct | both,
 * default relay) and runs forever:
 *   - relay:  one outbound WS RelayClient thread per resolved disco node,
 *             hello/query/result over `/ws/filter` (NAT-friendly, default).
 *   - direct: one AgentServer thread serving `/agent/query` + `/pop/gossip`
 *             + `/health`, plus a gossip-push thread that POSTs the agent's
 *             self-payload to each resolved seed disco.
 *   - both:   direct + relay concurrently, same identity.
 *
 * Usage:
 * @code
 * em::FilterRunner("my_filter",
 *                  std::make_shared<MyFilter>(),
 *                  em::AgentConfig{}).run();
 * @endcode
 */
class FilterRunner {
public:
    FilterRunner(std::string name,
                 std::shared_ptr<Filter> filter,
                 AgentConfig config = {});

    /** Start all configured transport threads and block until they all exit. */
    void run();

private:
    std::string             name_;
    std::shared_ptr<Filter> filter_;
    AgentConfig             config_;
};

} // namespace em
