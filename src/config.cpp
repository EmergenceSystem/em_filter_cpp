#include "config_internal.hpp"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace em::internal {

bool infer_tls(const std::string& host, int port) {
    if (host == "localhost" || host == "127.0.0.1" || host == "::1")
        return false;
    return port == 443;
}

std::pair<int,bool> default_port_tls(const std::string& host) {
    if (host == "localhost" || host == "127.0.0.1" || host == "::1")
        return {8080, false};
    return {443, true};
}

int reconnect_ms() {
    const char* s = std::getenv("EM_FILTER_RECONNECT_MS");
    if (s) {
        int v = std::atoi(s);
        if (v > 0) return v;
    }
    return 5000;
}

static std::string trim(std::string s) {
    auto l = s.find_first_not_of(" \t\r\n");
    auto r = s.find_last_not_of(" \t\r\n");
    return (l == std::string::npos) ? "" : s.substr(l, r - l + 1);
}

static std::string conf_path() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata) return std::string(appdata) + "\\emergence\\emergence.conf";
#else
    const char* home = std::getenv("HOME");
    if (home) return std::string(home) + "/.config/emergence/emergence.conf";
#endif
    return {};
}

static std::vector<DiscoNode> parse_nodes_str(const std::string& s) {
    std::vector<DiscoNode> out;
    std::istringstream ss(s);
    std::string entry;
    while (std::getline(ss, entry, ',')) {
        entry = trim(entry);
        if (entry.empty()) continue;

        DiscoNode n;
        if (!entry.empty() && entry[0] == '[') {
            // IPv6: [::1]:9000
            auto close = entry.find(']');
            if (close == std::string::npos) continue;
            n.host = entry.substr(1, close - 1);
            if (close + 1 < entry.size() && entry[close+1] == ':')
                n.port = std::stoi(entry.substr(close + 2));
            else
                std::tie(n.port, n.tls) = default_port_tls(n.host);
            n.tls = infer_tls(n.host, n.port);
        } else {
            auto colon = entry.rfind(':');
            if (colon != std::string::npos) {
                n.host = entry.substr(0, colon);
                n.port = std::stoi(entry.substr(colon + 1));
                n.tls  = infer_tls(n.host, n.port);
            } else {
                n.host = entry;
                std::tie(n.port, n.tls) = default_port_tls(n.host);
            }
        }
        out.push_back(std::move(n));
    }
    return out;
}

static std::vector<DiscoNode> read_conf_nodes() {
    std::string path = conf_path();
    if (path.empty()) return {};

    std::ifstream f(path);
    if (!f) return {};

    std::string section, line, last_nodes;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        if (section == "em_disco") {
            auto eq = line.find('=');
            if (eq != std::string::npos) {
                auto key = trim(line.substr(0, eq));
                auto val = trim(line.substr(eq + 1));
                if (key == "nodes") last_nodes = val;
            }
        }
    }
    if (!last_nodes.empty()) return parse_nodes_str(last_nodes);
    return {};
}

std::vector<DiscoNode> resolve_nodes(const AgentConfig& cfg) {
    if (!cfg.disco_nodes.empty()) return cfg.disco_nodes;

    const char* host_env = std::getenv("EM_DISCO_HOST");
    const char* port_env = std::getenv("EM_DISCO_PORT");

    if (host_env) {
        DiscoNode n;
        n.host = host_env;
        if (port_env) n.port = std::atoi(port_env);
        else std::tie(n.port, std::ignore) = default_port_tls(n.host);
        n.tls = infer_tls(n.host, n.port);
        return {n};
    }
    if (port_env) {
        return {DiscoNode{"localhost", (uint16_t)std::atoi(port_env), false}};
    }

    auto conf = read_conf_nodes();
    if (!conf.empty()) return conf;

    return {DiscoNode{"localhost", 8080, false}};
}

std::optional<std::string> resolve_jwt(const AgentConfig& cfg) {
    if (cfg.jwt_token) return cfg.jwt_token;
    const char* env = std::getenv("EM_FILTER_JWT_TOKEN");
    if (env) return std::string(env);
    return std::nullopt;
}

std::string resolve_key_dir(const std::string& name) {
    const char* env = std::getenv("EM_FILTER_KEY_DIR");
    if (env && *env) return env;
    return "./empop_key_" + name + "/";
}

std::string filter_mode() {
    const char* env = std::getenv("EM_FILTER_MODE");
    if (env && *env) return env;
    return "relay";
}

int query_port() {
    const char* env = std::getenv("EM_FILTER_QUERY_PORT");
    if (env) {
        int v = std::atoi(env);
        if (v > 0) return v;
    }
    return 9600;
}

std::string advertise_host() {
    const char* env = std::getenv("EM_FILTER_HOST");
    if (env && *env) return env;
    return "0.0.0.0";
}

int gossip_interval_ms() {
    const char* env = std::getenv("EM_FILTER_GOSSIP_INTERVAL_MS");
    if (env) {
        int v = std::atoi(env);
        if (v > 0) return v;
    }
    return 5000;
}

} // namespace em::internal
