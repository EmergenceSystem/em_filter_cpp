#include "em_filter/identity.hpp"

namespace em {

using json = nlohmann::json;

Identity::Identity(std::string name, const std::string& key_dir,
                    std::vector<std::string> capabilities)
    : name_(std::move(name)), capabilities_(std::move(capabilities)) {
    std::tie(pubkey_, seed_) = crypto::load_or_create(key_dir);
    id_ = crypto::id_of(pubkey_);
}

std::string Identity::selfsig_b64() const {
    crypto::Bytes sig = crypto::sign(crypto::canonical_identity(id_, name_), seed_);
    return crypto::b64_encode(sig);
}

json Identity::hello_payload() const {
    return json{
        {"action", "hello"},
        {"name", name_},
        {"pubkey", crypto::b64_encode(pubkey_)},
        {"sig", selfsig_b64()},
        {"capabilities", capabilities_},
    };
}

json Identity::gossip_payload(const std::string& host, int query_port) const {
    return json{
        {"id", crypto::b64_encode(id_)},
        {"name", name_},
        {"host", host},
        {"query_port", query_port},
        {"pubkey", crypto::b64_encode(pubkey_)},
        {"sig", selfsig_b64()},
        {"capabilities", capabilities_},
        {"role", "filter"},
    };
}

std::pair<std::string, std::string> Identity::sign_results(const json& items) const {
    return crypto::sign_response(items, pubkey_, seed_);
}

} // namespace em
