# em_filter_cpp

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

C++ SDK for building [Emergence](https://github.com/EmergenceSystem) network agents.

`em_filter_cpp` lets any C++ process join the Emergence distributed discovery network
as a **filter agent** — a service that receives search queries from the `em_disco`
broker, processes them (web search, DNS lookup, LLM call, database query, …), and
returns structured results.

This library is the C++ equivalent of the Erlang `em_filter` library: same WebSocket
protocol, same configuration contract, idiomatic modern C++ API.

---

## How it works

```
 ┌─────────────┐    WebSocket     ┌───────────────┐    WebSocket     ┌─────────────┐
 │  em_disco   │ ◄─────────────── │ FilterRunner  │ ───────────────► │  em_disco   │
 │  (broker)   │  query / result  │ (your agent)  │  (multi-node)    │  (replica)  │
 └─────────────┘                  └───────────────┘                  └─────────────┘
                                         │
                               std::thread per node
                                         │
                                  ┌──────┴──────┐
                                  │  em::Filter │
                                  │  subclass   │
                                  └─────────────┘
```

1. `FilterRunner` resolves disco nodes and spawns one `std::thread` per node.
2. Each thread maintains a persistent WebSocket connection with automatic reconnection.
3. On a `query` frame, the thread calls your `Filter::handle()` and sends back a `result` frame.

---

## Requirements

- C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+)
- CMake 3.20+
- OpenSSL (install via system package manager or [vcpkg](https://vcpkg.io/))
- [nlohmann/json](https://github.com/nlohmann/json) — downloaded automatically via CMake `FetchContent`

---

## Building

```bash
mkdir build && cd build

# Linux / macOS
cmake ..
cmake --build . --config Release

# Windows with vcpkg
cmake .. -DOPENSSL_ROOT_DIR=C:/vcpkg/installed/x64-windows
cmake --build . --config Release
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

By default the agent connects to `localhost:8080`. Override via environment
variables or `em::AgentConfig` — see [Configuration](#configuration).

---

## Try the built-in example

```bash
./build/Release/echo_filter

# With a custom broker:
EM_DISCO_HOST=disco.example.com \
EM_DISCO_PORT=443 \
EM_FILTER_JWT_TOKEN=eyJ... \
./build/Release/echo_filter
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

`handle` is called for every `query` frame from `em_disco`.
- `body` — raw query string (e.g. `"erlang otp"`)
- `memory` — current memory state (JSON object, passed by reference; persists between
  queries within a connection; reset to `{}` on reconnect — same as Erlang RAM mode)

Modify `memory` in-place or replace it entirely. The return value is the result JSON —
typically an array of embryo objects.

### Result format

| Type | Required properties |
|------|---------------------|
| `"url"` | `url`, `title` |
| `"dns"` | `domain`, `ips` |
| `"text"` | `content` |

Return `nlohmann::json{}` (null) or an empty array for "no results".

### Capabilities

`capabilities()` returns the list of capabilities your agent advertises.
`em_disco` uses these to route queries. Default: `{"search", "query"}`.

---

## Configuration

### Environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `EM_DISCO_HOST` | — | Disco broker hostname |
| `EM_DISCO_PORT` | — | Disco broker port |
| `EM_FILTER_JWT_TOKEN` | — | JWT for authenticated brokers |
| `EM_FILTER_RECONNECT_MS` | `5000` | Reconnect delay in milliseconds |

### Node resolution order

1. `AgentConfig.disco_nodes` — explicit list (highest priority)
2. `EM_DISCO_HOST` / `EM_DISCO_PORT` env vars
3. `[em_disco] nodes = …` in `emergence.conf`
4. `localhost:8080` — built-in default

### TLS inference

| Host | Port | Transport |
|------|------|-----------|
| `localhost`, `127.0.0.1`, `::1` | any | `ws://` (plain) |
| any other | 443 | `wss://` (TLS) |
| any other | other | `ws://` (plain) |

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

## Multi-node

`FilterRunner` connects to all resolved nodes simultaneously, one `std::thread` per node.
The filter is shared via `std::shared_ptr` — all threads call the same object, so ensure
`handle()` is thread-safe (or use a mutex around shared state). Memory (the `nlohmann::json&`
reference) is local to each thread — starts as `{}` and resets on reconnect.

---

## HTML utilities

`#include "em_filter/html.hpp"` — namespace `em`:

```cpp
#include "em_filter/html.hpp"

std::string html  = fetch_page(url);
std::string clean = em::strip_scripts(html);          // remove <script>…</script>
std::string text  = em::get_text(clean);              // strip all tags → plain text
std::string dec   = em::decode_html_entities(text);   // caf&eacute; → café

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

## WebSocket protocol

The agent speaks a minimal JSON-over-WebSocket protocol to `em_disco`.

**Agent → Disco:**
```json
{ "action": "register",    "name": "<agent_name>" }
{ "action": "agent_hello", "capabilities": ["search", "query"] }
{ "action": "result",      "id": "<query_id>", "data": <result> }
```

**Disco → Agent:**
```json
{ "action": "query", "id": "<query_id>", "body": "<query_string>" }
```

The library handles the handshake and reconnection automatically.
Your code only implements `Filter::handle`.

---

## License

[MIT](LICENSE)
