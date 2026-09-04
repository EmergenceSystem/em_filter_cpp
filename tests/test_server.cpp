// Model A HTTP server: signed /agent/query, /pop/gossip, /health.
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <httplib.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

#include "em_filter/crypto.hpp"
#include "em_filter/filter.hpp"
#include "em_filter/identity.hpp"
#include "server.hpp"

using json = nlohmann::json;

namespace {

std::string tmp_key_dir(const char* tag) {
    std::random_device rd;
    std::ostringstream oss;
    oss << std::filesystem::temp_directory_path().string() << "/em_filter_cpp_test_srv_" << tag
        << "_" << rd();
    std::filesystem::create_directories(oss.str());
    return oss.str();
}

class StubFilter : public em::Filter {
public:
    nlohmann::json handle(const std::string& body, nlohmann::json&) override {
        return json::array({{{"url", "https://x/1"}, {"title", "T: " + body}, {"resume", "R"}}});
    }
};

class ErrorFilter : public em::Filter {
public:
    nlohmann::json handle(const std::string&, nlohmann::json&) override {
        throw std::runtime_error("boom");
    }
};

struct RunningServer {
    em::Identity identity;
    em::internal::AgentServer server;
    std::thread thread;

    RunningServer(const char* tag, std::shared_ptr<em::Filter> filter, std::mutex& mtx)
        : identity(tag, tmp_key_dir(tag), {"search"})
        , server(identity, std::move(filter), mtx, "127.0.0.1", 0)
        , thread([this] { server.run(); })
    {
        server.wait_until_ready();
    }

    ~RunningServer() {
        server.stop();
        thread.join();
    }
};

} // namespace

TEST_CASE("AgentServer: /agent/query returns signed results", "[server]") {
    std::mutex mtx;
    RunningServer rs("query", std::make_shared<StubFilter>(), mtx);

    httplib::Client cli("127.0.0.1", rs.server.port());
    auto res = cli.Post("/agent/query", R"({"query":"hi"})", "application/json");
    REQUIRE(res);
    CHECK(res->status == 200);

    json body = json::parse(res->body);
    auto signer_id = em::crypto::b64_decode(body["signer_id"].get<std::string>());
    CHECK(signer_id == rs.identity.id());
    auto sig = em::crypto::b64_decode(body["signature"].get<std::string>());
    CHECK(em::crypto::verify(em::crypto::canonical_response(body["results"]), sig, rs.identity.pubkey()));
}

TEST_CASE("AgentServer: /health replies ok", "[server]") {
    std::mutex mtx;
    RunningServer rs("health", std::make_shared<StubFilter>(), mtx);

    httplib::Client cli("127.0.0.1", rs.server.port());
    auto res = cli.Get("/health");
    REQUIRE(res);
    CHECK(res->status == 200);
    CHECK(res->body == "ok");
}

TEST_CASE("AgentServer: malformed /agent/query body returns 400", "[server]") {
    std::mutex mtx;
    RunningServer rs("bad", std::make_shared<StubFilter>(), mtx);

    httplib::Client cli("127.0.0.1", rs.server.port());
    auto res = cli.Post("/agent/query", "not json", "application/json");
    REQUIRE(res);
    CHECK(res->status == 400);
}

TEST_CASE("AgentServer: handler error returns 500", "[server]") {
    std::mutex mtx;
    RunningServer rs("err", std::make_shared<ErrorFilter>(), mtx);

    httplib::Client cli("127.0.0.1", rs.server.port());
    auto res = cli.Post("/agent/query", R"({"query":"hi"})", "application/json");
    REQUIRE(res);
    CHECK(res->status == 500);
}

TEST_CASE("AgentServer: /pop/gossip replies own self-payload", "[server]") {
    std::mutex mtx;
    RunningServer rs("gossip", std::make_shared<StubFilter>(), mtx);

    httplib::Client cli("127.0.0.1", rs.server.port());
    auto res = cli.Post("/pop/gossip", R"({"id":"whatever"})", "application/json");
    REQUIRE(res);
    CHECK(res->status == 200);

    json g = json::parse(res->body);
    CHECK(g["role"] == "filter");
    CHECK(em::crypto::b64_decode(g["id"].get<std::string>()) == rs.identity.id());
    CHECK(g["query_port"] == rs.server.port());
}
