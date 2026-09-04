#pragma once
#include "em_filter/config.hpp"
#include "em_filter/filter.hpp"
#include "em_filter/identity.hpp"

#include <memory>
#include <mutex>

namespace em::internal {

/**
 * Model B: outbound WebSocket relay client.
 *
 * Opens a WS connection to `<node>/ws/filter`, sends `hello`, expects
 * `hello_ok`, then answers `query` frames with signed `result` frames.
 * Reconnects with a fixed delay on any error or disconnect.
 */
class RelayClient {
public:
    RelayClient(Identity& identity, std::shared_ptr<Filter> filter,
                std::mutex& filter_mutex,
                DiscoNode node, int reconnect_ms);

    /** Blocks forever, reconnecting on failure. */
    void run_forever();

private:
    /** Runs one hello -> query/result session. Returns when the connection ends. */
    bool session();

    Identity& identity_;
    std::shared_ptr<Filter> filter_;
    std::mutex& filter_mutex_;
    DiscoNode node_;
    int reconnect_ms_;
};

} // namespace em::internal
