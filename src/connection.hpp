#pragma once
#include "em_filter/filter.hpp"
#include "em_filter/config.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <optional>

namespace em::internal {

void run_connection(const std::string& agent_name,
                    const DiscoNode& node,
                    std::shared_ptr<Filter> filter,
                    std::mutex& filter_mutex,
                    const std::optional<std::string>& jwt_token,
                    int reconnect_ms);

} // namespace em::internal
