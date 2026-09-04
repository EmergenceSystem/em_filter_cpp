#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <random>
#include <sstream>

#include "em_filter/crypto.hpp"
#include "em_filter/identity.hpp"

using json = nlohmann::json;

namespace {

// Unique scratch dir per test run so identities/keys don't collide.
std::string tmp_key_dir(const char* tag) {
    std::random_device rd;
    std::ostringstream oss;
    oss << std::filesystem::temp_directory_path().string() << "/em_filter_cpp_test_" << tag
        << "_" << rd();
    std::filesystem::create_directories(oss.str());
    return oss.str();
}

} // namespace

TEST_CASE("hello_payload has the right shape and a verifiable self-signature",
          "[identity]") {
    em::Identity ident("t", tmp_key_dir("hello"), {"search"});

    json h = ident.hello_payload();
    CHECK(h["action"] == "hello");
    CHECK(h["name"] == "t");
    CHECK(h["capabilities"] == json::array({"search"}));

    em::crypto::Bytes pub = em::crypto::b64_decode(h["pubkey"].get<std::string>());
    CHECK(pub == ident.pubkey());

    em::crypto::Bytes sig = em::crypto::b64_decode(h["sig"].get<std::string>());
    CHECK(em::crypto::verify(em::crypto::canonical_identity(ident.id(), "t"), sig, pub));
}

TEST_CASE("gossip_payload carries host/query_port/role=filter", "[identity]") {
    em::Identity ident("t2", tmp_key_dir("gossip"), {"search", "query"});

    json g = ident.gossip_payload("1.2.3.4", 9600);
    CHECK(g["query_port"] == 9600);
    CHECK(g["role"] == "filter");
    CHECK(g["host"] == "1.2.3.4");
    CHECK(em::crypto::b64_decode(g["id"].get<std::string>()) == ident.id());
}

TEST_CASE("keypair persists across Identity instances (load_or_create round-trip)",
          "[identity]") {
    std::string dir = tmp_key_dir("persist");
    em::Identity a("persist", dir, {"search"});
    em::Identity b("persist", dir, {"search"});
    CHECK(a.pubkey() == b.pubkey());
    CHECK(a.id() == b.id());
}

TEST_CASE("sign_results reproduces a verifiable response signature", "[identity]") {
    em::Identity ident("signer", tmp_key_dir("sign"), {"search"});
    json items = json::array(
        {{{"url", "https://x/1"}, {"title", "T"}, {"resume", "R"}}});

    auto [signer_id, sig_b64] = ident.sign_results(items);
    CHECK(em::crypto::b64_decode(signer_id) == ident.id());

    em::crypto::Bytes sig = em::crypto::b64_decode(sig_b64);
    CHECK(em::crypto::verify(em::crypto::canonical_response(items), sig, ident.pubkey()));
}
