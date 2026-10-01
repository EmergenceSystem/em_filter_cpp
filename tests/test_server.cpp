// Model A HTTP server: signed /agent/query, /pop/gossip, /health.
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <httplib.h>

#include <atomic>
#include <chrono>
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
    REQUIRE(body["ts"].is_number_integer());
    const std::int64_t ts = body["ts"].get<std::int64_t>();
    CHECK(ts > 1700000000000LL);
    CHECK(em::crypto::verify(em::crypto::canonical_response_v2("hi", ts, body["results"]), sig,
                             rs.identity.pubkey()));
    // The v1 canonical form must no longer verify.
    CHECK_FALSE(em::crypto::verify(em::crypto::canonical_response(body["results"]), sig,
                                   rs.identity.pubkey()));
    // Bound to the query: a different query does not verify.
    CHECK_FALSE(em::crypto::verify(em::crypto::canonical_response_v2("other", ts, body["results"]), sig,
                                   rs.identity.pubkey()));
}

TEST_CASE("gossip_push_loop: POST carries verifiable x-pop-* auth headers", "[server][gossip]") {
    // Stub disco: captures the first /pop/gossip POST (headers + exact body).
    httplib::Server stub;
    std::mutex m;
    std::string got_body;
    httplib::Headers got_headers;
    std::atomic<bool> got{false};
    stub.Post("/pop/gossip", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(m);
        if (!got) {
            got_body = req.body;
            got_headers = req.headers;
            got = true;
        }
        res.set_content("{}", "application/json");
    });
    int port = stub.bind_to_any_port("127.0.0.1");
    REQUIRE(port > 0);
    std::thread stub_thread([&] { stub.listen_after_bind(); });
    stub.wait_until_ready();

    // gossip_push_loop never returns, so the detached thread must not reference
    // this test's stack: the identity is intentionally leaked and seeds copied.
    auto* identp = new em::Identity("gossiper", tmp_key_dir("gossip_auth"), std::vector<std::string>{"search"});
    em::Identity& ident = *identp;
    std::vector<em::DiscoNode> seeds{{"127.0.0.1", (uint16_t)port, false}};
    // Long interval: it pushes once immediately, and the next push (5s later)
    // never fires within the test process lifetime in practice.
    std::thread pusher([identp, seeds] {
        em::internal::gossip_push_loop(*identp, seeds, "1.2.3.4", 9600, 600000);
    });
    pusher.detach();

    for (int i = 0; i < 100 && !got; i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(got);

    std::lock_guard<std::mutex> lock(m);
    auto find = [&](const char* k) {
        auto it = got_headers.find(k);
        REQUIRE(it != got_headers.end());
        return it->second;
    };
    auto id = em::crypto::b64_decode(find("x-pop-id"));
    CHECK(id == ident.id());
    const std::int64_t ts = std::stoll(find("x-pop-ts"));
    CHECK(ts > 1700000000000LL);
    auto sig = em::crypto::b64_decode(find("x-pop-sig"));
    auto digest = em::crypto::sha256(em::crypto::Bytes(got_body.begin(), got_body.end()));
    CHECK(em::crypto::verify(em::crypto::canonical_gossip_auth(id, ts, digest), sig, ident.pubkey()));
    CHECK(json::parse(got_body)["role"] == "filter");

    stub.stop();
    stub_thread.join();
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
