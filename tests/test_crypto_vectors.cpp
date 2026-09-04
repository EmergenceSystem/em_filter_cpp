// Cross-language crypto parity test: em_filter_cpp must reproduce
// fixtures/crypto_vectors.json byte-for-byte against em_pop_crypto.erl.
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <string>

#include "em_filter/crypto.hpp"

using json = nlohmann::json;
using em::crypto::Bytes;

namespace {

json load_fixture() {
    std::ifstream f(FIXTURE_PATH);
    REQUIRE(f.good());
    json j;
    f >> j;
    return j;
}

} // namespace

TEST_CASE("id_of matches fixture", "[crypto][fixture]") {
    json fx = load_fixture();
    Bytes pub = em::crypto::hex_decode(fx["pubkey_hex"].get<std::string>());

    Bytes id = em::crypto::id_of(pub);
    CHECK(em::crypto::hex_encode(id) == fx["id_hex"].get<std::string>());
    CHECK(em::crypto::b64_encode(id) == fx["signer_id_b64"].get<std::string>());
}

TEST_CASE("canonical_identity matches fixture", "[crypto][fixture]") {
    json fx = load_fixture();
    Bytes id = em::crypto::hex_decode(fx["id_hex"].get<std::string>());
    std::string name = fx["name"].get<std::string>();

    Bytes got = em::crypto::canonical_identity(id, name);
    CHECK(em::crypto::hex_encode(got) == fx["canonical_identity_hex"].get<std::string>());
}

TEST_CASE("selfsig matches fixture (deterministic ed25519)", "[crypto][fixture]") {
    json fx = load_fixture();
    Bytes id = em::crypto::hex_decode(fx["id_hex"].get<std::string>());
    Bytes seed = em::crypto::hex_decode(fx["privkey_hex"].get<std::string>());
    std::string name = fx["name"].get<std::string>();

    Bytes sig = em::crypto::sign(em::crypto::canonical_identity(id, name), seed);
    CHECK(em::crypto::b64_encode(sig) == fx["selfsig_b64"].get<std::string>());
}

TEST_CASE("canonical_response + response signature match fixture", "[crypto][fixture]") {
    json fx = load_fixture();
    Bytes pub = em::crypto::hex_decode(fx["pubkey_hex"].get<std::string>());
    Bytes seed = em::crypto::hex_decode(fx["privkey_hex"].get<std::string>());

    Bytes cr = em::crypto::canonical_response(fx["items"]);
    CHECK(em::crypto::hex_encode(cr) == fx["canonical_response_hex"].get<std::string>());

    Bytes sig = em::crypto::sign(cr, seed);
    CHECK(em::crypto::b64_encode(sig) == fx["response_signature_b64"].get<std::string>());
    CHECK(em::crypto::verify(cr, sig, pub));
}

TEST_CASE("sign_response helper reproduces fixture signer_id + signature", "[crypto][fixture]") {
    json fx = load_fixture();
    Bytes pub = em::crypto::hex_decode(fx["pubkey_hex"].get<std::string>());
    Bytes seed = em::crypto::hex_decode(fx["privkey_hex"].get<std::string>());

    auto [signer_id, sig] = em::crypto::sign_response(fx["items"], pub, seed);
    CHECK(signer_id == fx["signer_id_b64"].get<std::string>());
    CHECK(sig == fx["response_signature_b64"].get<std::string>());
}

TEST_CASE("re-signing round-trips through verify", "[crypto][roundtrip]") {
    json fx = load_fixture();
    Bytes pub = em::crypto::hex_decode(fx["pubkey_hex"].get<std::string>());
    Bytes seed = em::crypto::hex_decode(fx["privkey_hex"].get<std::string>());

    Bytes msg = {'h', 'e', 'l', 'l', 'o'};
    Bytes sig = em::crypto::sign(msg, seed);
    CHECK(em::crypto::verify(msg, sig, pub));
    Bytes tampered = {'h', 'e', 'l', 'l', 'p'};
    CHECK_FALSE(em::crypto::verify(tampered, sig, pub));
}
