// Model B WS relay client: hello/hello_ok handshake, then query/result loop.
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

#include "em_filter/config.hpp"
#include "em_filter/crypto.hpp"
#include "em_filter/filter.hpp"
#include "em_filter/identity.hpp"
#include "wsclient.hpp"
#include "ws_test_server.hpp"

using json = nlohmann::json;

namespace {

std::string tmp_key_dir(const char* tag) {
    std::random_device rd;
    std::ostringstream oss;
    oss << std::filesystem::temp_directory_path().string() << "/em_filter_cpp_test_ws_" << tag
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

} // namespace

TEST_CASE("RelayClient: hello/hello_ok handshake then query/result round-trip", "[wsclient]") {
    em::test::WsTestServer stub;
    em::Identity ident("relay1", tmp_key_dir("ws"), {"search"});
    std::mutex mtx;
    em::DiscoNode node{"127.0.0.1", (uint16_t)stub.port(), false};

    // RelayClient::run_forever() reconnects forever by design; drive exactly
    // one session on a background thread and close the stub server once the
    // round-trip below is observed to end the session cleanly.
    std::thread client_thread([&] {
        em::internal::RelayClient client(ident, std::make_shared<StubFilter>(), mtx, node, 50);
        client.run_forever();
    });

    stub.accept_and_handshake();

    auto hello_raw = stub.recv_text();
    REQUIRE(hello_raw.has_value());
    json hello = json::parse(*hello_raw);
    CHECK(hello["action"] == "hello");
    CHECK(hello["name"] == "relay1");
    CHECK(hello["capabilities"] == json::array({"search"}));

    auto pub = em::crypto::b64_decode(hello["pubkey"].get<std::string>());
    CHECK(pub == ident.pubkey());
    auto selfsig = em::crypto::b64_decode(hello["sig"].get<std::string>());
    CHECK(em::crypto::verify(em::crypto::canonical_identity(ident.id(), "relay1"), selfsig, pub));

    stub.send_text(json{{"action", "hello_ok"}, {"id", hello["pubkey"]}}.dump());
    stub.send_text(json{{"action", "query"}, {"id", "q1"}, {"body", "hi"}}.dump());

    auto result_raw = stub.recv_text();
    REQUIRE(result_raw.has_value());
    json result = json::parse(*result_raw);
    CHECK(result["action"] == "result");
    CHECK(result["id"] == "q1");

    auto signer_id = em::crypto::b64_decode(result["signer_id"].get<std::string>());
    CHECK(signer_id == ident.id());
    auto sig = em::crypto::b64_decode(result["signature"].get<std::string>());
    CHECK(em::crypto::verify(em::crypto::canonical_response(result["results"]), sig, ident.pubkey()));
    CHECK(result["results"][0]["title"] == "T: hi");

    stub.close();
    client_thread.detach(); // run_forever() never returns; process exit reaps it.
}
