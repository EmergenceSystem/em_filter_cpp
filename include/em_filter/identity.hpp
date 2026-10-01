#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "em_filter/crypto.hpp"

namespace em {

/** Current wall-clock time as unix milliseconds (the `ts` of v2 responses / gossip auth). */
std::int64_t now_ms();

/**
 * Holds a loaded/created ed25519 keypair, the agent name and its declared
 * capabilities; builds the wire payloads shared by both transports
 * (Model A gossip, Model B hello) and signs query results.
 */
class Identity {
public:
    Identity(std::string name, const std::string& key_dir,
             std::vector<std::string> capabilities);

    const std::string& name() const { return name_; }
    const crypto::Bytes& pubkey() const { return pubkey_; }
    const crypto::Bytes& id() const { return id_; }
    const std::vector<std::string>& capabilities() const { return capabilities_; }

    /** Model B handshake frame: {action:hello,name,pubkey:b64,sig:b64(selfsig),capabilities}. */
    nlohmann::json hello_payload() const;

    /** Model A gossip push payload (id/name/host/query_port/pubkey/sig/capabilities/role). */
    nlohmann::json gossip_payload(const std::string& host, int query_port) const;

    /** Sign canonical_response(items); returns (signer_id_b64, signature_b64). */
    std::pair<std::string, std::string> sign_results(const nlohmann::json& items) const;

    /**
     * v2 response signing: signs canonical_response_v2(query, ts, items), binding the
     * signature to the received query and a unix-millis timestamp. Returns
     * (signer_id_b64, signature_b64). Used by both transports.
     */
    std::pair<std::string, std::string> sign_results_v2(const std::string& query, std::int64_t ts,
                                                         const nlohmann::json& items) const;

    /**
     * Auth headers for an outbound /pop/gossip POST over the exact `body` bytes:
     * x-pop-id = base64(id), x-pop-ts = decimal(ts_ms), x-pop-sig = sign_gossip(id, ts, body).
     */
    std::vector<std::pair<std::string, std::string>>
    gossip_auth_headers(const std::string& body, std::int64_t ts) const;

private:
    std::string selfsig_b64() const;

    std::string name_;
    std::vector<std::string> capabilities_;
    crypto::Bytes pubkey_;
    crypto::Bytes seed_;
    crypto::Bytes id_;
};

} // namespace em
