#pragma once
#include "filter.hpp"
#include "config.hpp"
#include <memory>
#include <string>

namespace em {

/**
 * Starts one std::thread per resolved disco node and runs forever.
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

    /** Start all connection threads and block until they all exit. */
    void run();

private:
    std::string             name_;
    std::shared_ptr<Filter> filter_;
    AgentConfig             config_;
};

} // namespace em
