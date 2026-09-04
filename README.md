# em_filter_cpp

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

C++ SDK for building [Emergence](https://github.com/EmergenceSystem) network agents.

`em_filter_cpp` lets any C++ process join the Emergence distributed discovery
network as a **filter agent** -- a service that receives search queries from
the mesh, processes them (web search, DNS lookup, LLM call, database query,
...), and returns ed25519-signed structured results.

This is byte-identical, protocol-level parity with the Erlang reference
(`em_pop_crypto.erl`): same keypair format, same canonical byte forms, same
signatures. See [PROTOCOL.md](PROTOCOL.md) for the full wire protocol.

---

## Two transports, one identity

- **Model B -- WS relay (default, NAT-friendly).** The agent opens an
  outbound WebSocket to a disco and never needs an inbound port. Good default
  for a laptop or a box behind a home router.
- **Model A -- direct HTTP.** The agent serves `POST /agent/query`,
  `POST /pop/gossip`, `GET /health` and gossips its own identity to seed
  discos. Requires the agent to be reachable at `host:query_port`.

Select with `EM_FILTER_MODE=relay|direct|both` (default `relay`). `both` runs
concurrently under the same ed25519 identity.

```
                  Model B (default)                      Model A
 ┌─────────┐   outbound WS, hello/query/result    ┌─────────┐   inbound POST /agent/query
 │ FilterRunner│ ───────────────────────────────► │ FilterRunner│ ◄─────────────────────── em_disco
 │ (your agent)│      NAT-friendly, no open port    │ (your agent)│   + gossip push to /pop/gossip
 └─────────┘                                       └─────────┘
      │                                                   │
      └──────────────────── em::Filter subclass ──────────┘
```

1. `FilterRunner` loads/creates the agent's ed25519 keypair (`Identity`) and
   starts the transport thread(s) selected by `EM_FILTER_MODE`.
2. Every query result is signed: `signature = Ed25519(canonical_response(results), seed)`.
3. Your code only implements `Filter::handle`.

---

## Requirements

- C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+)
- CMake 3.16+
- OpenSSL, libsodium (`pkg-config libsodium`)
- [nlohmann/json](https://github.com/nlohmann/json) and
  [cpp-httplib](https://github.com/yhirose/cpp-httplib) -- downloaded
  automatically via CMake `FetchContent`

---

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
ctest --test-dir .
```

To use the library in your own CMake project:

```cmake
add_subdirectory(em_filter_cpp)
target_link_libraries(my_filter PRIVATE em_filter_cpp)
```

---

## Quick start

```cpp
#include "em_filter/filter.hpp"
#include "em_filter/runner.hpp"
#include <nlohmann/json.hpp>

class MyFilter : public em::Filter {
public:
    nlohmann::json handle(const std::string& body,
                          nlohmann::json& memory) override {
        return nlohmann::json::array({{
            {"type", "url"},
            {"properties", {
                {"url",   "https://example.com"},
                {"title", "Result for: " + body}
            }}
        }});
    }

    std::vector<std::string> capabilities() const override {
        return {"search", "query"};
    }
};

int main() {
    em::FilterRunner("my_filter",
                     std::make_shared<MyFilter>(),
                     em::AgentConfig{}).run();
}
```

By default the agent relays through `localhost:8080` (Model B). Point it at a
real disco and see [Configuration](#configuration).

---

## Try the built-in example

```bash
# Model B (default): outbound WS relay
EM_DISCO_HOST=disco.example.com ./build/echo_filter

# Model A: direct HTTP, must be reachable at query_port
EM_FILTER_MODE=direct EM_FILTER_QUERY_PORT=9600 \
EM_DISCO_HOST=disco.example.com ./build/echo_filter
```

---

## The Filter class

```cpp
namespace em {
class Filter {
public:
    virtual nlohmann::json handle(const std::string& body,
                                   nlohmann::json& memory) = 0;
    virtual std::vector<std::string> capabilities() const {
        return {"search", "query"};
    }
};
}
```

`handle` is called for every query, on either transport.
- `body` -- raw query string (e.g. `"erlang otp"`)
- `memory` -- current memory state (JSON object, passed by reference;
  persists between queries within a session; reset to `{}` on reconnect)

Modify `memory` in-place or replace it entirely. The return value is the
result JSON -- typically an array of embryo objects, signed automatically
before being sent.

### Result format

| Type | Required properties |
|------|---------------------|
| `"url"` | `url`, `title` |
| `"dns"` | `domain`, `ips` |
| `"text"` | `content` |

Return `nlohmann::json{}` (null) or an empty array for "no results".

### Capabilities

`capabilities()` returns the list of plain-string capabilities your agent
advertises. The disco computes the routing vector from these (never the SDK
itself -- see PROTOCOL.md). Default: `{"search", "query"}`.

---

## Configuration

### Environment variables

| Variable | Default | Description |
|----------|---------|--------------|
| `EM_FILTER_MODE` | `relay` | `relay` \| `direct` \| `both` |
| `EM_DISCO_HOST` | -- | Disco hostname (relay target / gossip seed) |
| `EM_DISCO_PORT` | -- | Disco port |
| `EM_FILTER_KEY_DIR` | `./empop_key_<name>/` | ed25519 keypair directory |
| `EM_FILTER_QUERY_PORT` | `9600` | Model A listen/advertise port |
| `EM_FILTER_HOST` | `0.0.0.0` | Model A bind/advertise host |
| `EM_FILTER_GOSSIP_INTERVAL_MS` | `5000` | Model A gossip push interval |
| `EM_FILTER_RECONNECT_MS` | `5000` | Model B reconnect delay |
| `EM_FILTER_JWT_TOKEN` | -- | Shared `auth_token`, if the mesh requires one |

### Node resolution order

1. `AgentConfig.disco_nodes` -- explicit list (highest priority)
2. `EM_DISCO_HOST` / `EM_DISCO_PORT` env vars
3. `[em_disco] nodes = ...` in `emergence.conf`
4. `localhost:8080` -- built-in default

### TLS inference

| Host | Port | Transport |
|------|------|-----------|
| `localhost`, `127.0.0.1`, `::1` | any | plain |
| any other | 443 | TLS |
| any other | other | plain |

### `emergence.conf`

```ini
[em_disco]
nodes = localhost:8080, disco.example.com, [::1]:9000
```

Platform paths:
- **Linux / macOS:** `~/.config/emergence/emergence.conf`
- **Windows:** `%APPDATA%\emergence\emergence.conf`

### Programmatic configuration

```cpp
#include "em_filter/config.hpp"

em::AgentConfig config;
config.jwt_token = "eyJ...";
config.disco_nodes = {
    {"disco.example.com",  443, true},
    {"disco2.example.com", 443, true},
};

em::FilterRunner("my_filter", std::make_shared<MyFilter>(), config).run();
```

---

## Multi-node (relay / both)

In `relay` or `both` mode, `FilterRunner` opens one `RelayClient` thread per
resolved disco node -- redundant relay connections under the same identity.
The filter is shared via `std::shared_ptr`; all threads call the same
object, so keep `handle()` thread-safe (a mutex around shared state, or none
if it's read-only/stateless). Each session's `memory` (the `nlohmann::json&`
reference) is local to that WS session -- starts as `{}` and resets on
reconnect.

---

## HTML utilities

`#include "em_filter/html.hpp"` -- namespace `em`:

```cpp
#include "em_filter/html.hpp"

std::string html  = fetch_page(url);
std::string clean = em::strip_scripts(html);          // remove <script>...</script>
std::string text  = em::get_text(clean);              // strip all tags -> plain text
std::string dec   = em::decode_html_entities(text);   // caf&eacute; -> café

auto items = em::extract_elements(html, "li.b_algo");
for (auto& item : items) {
    auto href = em::extract_attribute(item, "href");
    if (href && !em::should_skip_link(*href, {"ads.", "tracking."})) {
        // process href
    }
}
```

| Function | Description |
|----------|-------------|
| `strip_scripts(html)` | Remove all `<script>` blocks |
| `get_text(html)` | Strip all HTML tags, return plain text |
| `extract_elements(html, selector)` | Inner HTML of matching elements (tag, `.class`, `#id`, `tag.class`) |
| `extract_attribute(element, attr)` | `std::optional<std::string>` attribute value |
| `decode_html_entities(text)` | Decode `&#N;`, `&#xHH;`, `&name;` entities |
| `should_skip_link(url, excluded)` | `true` if URL is not HTTP or matches an excluded substring |

---

## Protocol

See [PROTOCOL.md](PROTOCOL.md) for the full wire protocol: identity/crypto,
Model A HTTP routes, Model B WS frames.

---

## License

[MIT](LICENSE)
