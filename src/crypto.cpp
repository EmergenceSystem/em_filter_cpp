#include "em_filter/crypto.hpp"

#include <sodium.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace em::crypto {

namespace fs = std::filesystem;

static void ensure_sodium() {
    static std::once_flag flag;
    static bool ok = false;
    std::call_once(flag, []() { ok = (sodium_init() >= 0); });
    if (!ok) throw std::runtime_error("em_filter: sodium_init() failed");
}

std::string b64_encode(const Bytes& data) {
    ensure_sodium();
    size_t maxlen = sodium_base64_ENCODED_LEN(data.size(), sodium_base64_VARIANT_ORIGINAL);
    std::string out(maxlen, '\0');
    sodium_bin2base64(out.data(), maxlen, data.data(), data.size(),
                       sodium_base64_VARIANT_ORIGINAL);
    out.resize(std::strlen(out.c_str()));
    return out;
}

Bytes b64_decode(const std::string& s) {
    ensure_sodium();
    Bytes out(s.size() + 1);
    size_t out_len = 0;
    if (sodium_base642bin(out.data(), out.size(), s.data(), s.size(), nullptr, &out_len,
                           nullptr, sodium_base64_VARIANT_ORIGINAL) != 0) {
        throw std::runtime_error("em_filter: base64 decode failed");
    }
    out.resize(out_len);
    return out;
}

std::string hex_encode(const Bytes& data) {
    ensure_sodium();
    size_t hexlen = data.size() * 2 + 1;
    std::string out(hexlen, '\0');
    sodium_bin2hex(out.data(), hexlen, data.data(), data.size());
    out.resize(std::strlen(out.c_str()));
    return out;
}

Bytes hex_decode(const std::string& hex) {
    ensure_sodium();
    Bytes out(hex.size() / 2);
    size_t out_len = 0;
    if (sodium_hex2bin(out.data(), out.size(), hex.data(), hex.size(), nullptr, &out_len,
                        nullptr) != 0) {
        throw std::runtime_error("em_filter: hex decode failed");
    }
    out.resize(out_len);
    return out;
}

Bytes id_of(const Bytes& pubkey) {
    ensure_sodium();
    unsigned char h[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(h, pubkey.data(), pubkey.size());
    return Bytes(h, h + ID_LEN);
}

Bytes canonical_identity(const Bytes& id, const std::string& name) {
    Bytes v;
    v.reserve(id.size() + 1 + name.size());
    v.insert(v.end(), id.begin(), id.end());
    v.push_back(0);
    v.insert(v.end(), name.begin(), name.end());
    return v;
}

// Returns the first string field among `keys` found on `p` (an object), else "".
static std::string pick(const nlohmann::json& p, std::initializer_list<const char*> keys) {
    if (p.is_object()) {
        for (auto k : keys) {
            auto it = p.find(k);
            if (it != p.end() && it->is_string()) return it->get<std::string>();
        }
    }
    return "";
}

Bytes canonical_response(const nlohmann::json& items) {
    Bytes out;
    if (!items.is_array()) return out;

    for (const auto& item : items) {
        nlohmann::json p = nlohmann::json::object();
        if (item.is_object()) {
            auto pit = item.find("properties");
            p = (pit != item.end() && pit->is_object()) ? *pit : item;
        }
        std::string u = pick(p, {"url"});
        std::string t = pick(p, {"title", "label"});
        std::string r = pick(p, {"resume", "value", "description"});

        out.insert(out.end(), u.begin(), u.end());
        out.push_back(0);
        out.insert(out.end(), t.begin(), t.end());
        out.push_back(0);
        out.insert(out.end(), r.begin(), r.end());
        out.push_back('\n');
    }
    return out;
}

Bytes sign(const Bytes& msg, const Bytes& seed) {
    ensure_sodium();
    if (seed.size() != SEED_LEN) throw std::invalid_argument("em_filter: seed must be 32 bytes");
    unsigned char pub[crypto_sign_PUBLICKEYBYTES];
    unsigned char sk[crypto_sign_SECRETKEYBYTES];
    crypto_sign_seed_keypair(pub, sk, seed.data());

    Bytes sig(crypto_sign_BYTES);
    crypto_sign_detached(sig.data(), nullptr, msg.data(), msg.size(), sk);
    sodium_memzero(sk, sizeof sk);
    return sig;
}

bool verify(const Bytes& msg, const Bytes& sig, const Bytes& pubkey) {
    ensure_sodium();
    if (sig.size() != crypto_sign_BYTES || pubkey.size() != crypto_sign_PUBLICKEYBYTES)
        return false;
    return crypto_sign_verify_detached(sig.data(), msg.data(), msg.size(), pubkey.data()) == 0;
}

std::pair<std::string, std::string> sign_response(const nlohmann::json& items,
                                                    const Bytes& pubkey, const Bytes& seed) {
    Bytes cr = canonical_response(items);
    Bytes sig = sign(cr, seed);
    return {b64_encode(id_of(pubkey)), b64_encode(sig)};
}

std::pair<Bytes, Bytes> load_or_create(const std::string& key_dir) {
    ensure_sodium();
    fs::path dir(key_dir);
    fs::path file = dir / "node_ed25519.key";

    if (fs::exists(file)) {
        std::ifstream f(file, std::ios::binary);
        Bytes raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (raw.size() < PUBKEY_LEN + SEED_LEN)
            throw std::runtime_error("em_filter: key file " + file.string() + " is corrupt");
        return {Bytes(raw.begin(), raw.begin() + PUBKEY_LEN),
                Bytes(raw.begin() + PUBKEY_LEN, raw.begin() + PUBKEY_LEN + SEED_LEN)};
    }

    Bytes seed(SEED_LEN);
    randombytes_buf(seed.data(), seed.size());

    unsigned char pub[crypto_sign_PUBLICKEYBYTES];
    unsigned char sk[crypto_sign_SECRETKEYBYTES];
    crypto_sign_seed_keypair(pub, sk, seed.data());
    sodium_memzero(sk, sizeof sk);

    Bytes pubkey(pub, pub + PUBKEY_LEN);

    if (!dir.empty()) fs::create_directories(dir);
    std::ofstream f(file, std::ios::binary);
    f.write(reinterpret_cast<const char*>(pubkey.data()), (std::streamsize)pubkey.size());
    f.write(reinterpret_cast<const char*>(seed.data()), (std::streamsize)seed.size());

    return {pubkey, seed};
}

} // namespace em::crypto
