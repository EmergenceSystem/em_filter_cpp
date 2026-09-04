#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace em::crypto {

using Bytes = std::vector<unsigned char>;

constexpr size_t PUBKEY_LEN = 32;
constexpr size_t SEED_LEN   = 32;
constexpr size_t SIG_LEN    = 64;
constexpr size_t ID_LEN     = 16;

/**
 * Load the ed25519 keypair from `<key_dir>/node_ed25519.key`
 * (raw layout: pub(32) || seed(32), same as em_pop_crypto:load_or_create/1),
 * creating a fresh one on first run if the file is absent.
 * Returns (pubkey32, seed32).
 */
std::pair<Bytes, Bytes> load_or_create(const std::string& key_dir);

/** Peer id = SHA-256(pubkey)[0:16]. */
Bytes id_of(const Bytes& pubkey);

/** canonical_identity = id || 0x00 || name (UTF-8 bytes). */
Bytes canonical_identity(const Bytes& id, const std::string& name);

/**
 * canonical_response(items): one line per item, `url \0 title \0 resume \n`,
 * using the item's `properties` sub-object when present and an object,
 * else the item itself. title <- title|label, resume <- resume|value|description.
 * A non-object item or non-list `items` yields empty lines / empty bytes.
 */
Bytes canonical_response(const nlohmann::json& items);

/** Ed25519 sign msg with the 32-byte seed. Returns a 64-byte signature. */
Bytes sign(const Bytes& msg, const Bytes& seed);

/** Ed25519 verify. */
bool verify(const Bytes& msg, const Bytes& sig, const Bytes& pubkey);

/** Sign canonical_response(items); returns (signer_id_b64, signature_b64). */
std::pair<std::string, std::string> sign_response(const nlohmann::json& items,
                                                    const Bytes& pubkey,
                                                    const Bytes& seed);

/** Padded standard base64 (matches Erlang base64:encode/1). */
std::string b64_encode(const Bytes& data);
Bytes b64_decode(const std::string& s);

/** Lowercase-hex helpers (used by tests to compare against fixture hex fields). */
std::string hex_encode(const Bytes& data);
Bytes hex_decode(const std::string& hex);

} // namespace em::crypto
