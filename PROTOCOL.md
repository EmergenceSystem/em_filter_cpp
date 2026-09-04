# em_filter_cpp -- wire protocol

This document is the current protocol. It replaces an earlier `/ws` registration
model (`register` + `agent_hello`) which no longer exists on the mesh.

## Identity & crypto

Every value below is byte-identical to `em_pop_crypto.erl` and is verified
against a shared fixture (`fixtures/crypto_vectors.json`).

- **Keypair:** ed25519, persisted to `<key_dir>/node_ed25519.key` as raw
  `pubkey(32 bytes) || seed(32 bytes)`, created on first run if absent.
  `EM_FILTER_KEY_DIR` env selects the directory, else
  `./empop_key_<agent name>/`.
- **Peer id:** `id = SHA-256(pubkey)[0:16]`. On the wire: `signer_id = base64(id)`
  (padded, standard alphabet -- matches Erlang `base64:encode/1`).
- **canonical_identity:** `id || 0x00 || name` (name = UTF-8 agent name).
  Self-signature `sig = Ed25519_sign(canonical_identity, seed)`, wire form
  `base64(sig)`. Deliberately excludes host/port so a hub may rewrite a leaf's
  routing fields without breaking the self-signature.
- **canonical_response(items):** one line per item, in order:
  `url || 0x00 || title || 0x00 || resume || 0x0A`.
  - For a JSON object item, let `P` be its `properties` sub-object if present
    and an object, else the item itself.
  - `url` = `P.url` if a string, else empty.
  - `title` = first string among `P.title`, `P.label`, else empty.
  - `resume` = first string among `P.resume`, `P.value`, `P.description`,
    else empty.
  - A non-object item (or a `nlohmann::json` non-array `items`) yields an
    empty line (`0x00 0x00 0x0A`) per item, or empty bytes if `items` isn't
    an array.
- **Response signature:** `signature = base64(Ed25519_sign(canonical_response(results), seed))`,
  `signer_id = base64(id)`.

## Model A -- direct (the filter serves inbound HTTP)

- `POST /agent/query` -- body `{"query": "..."}`.
  Runs the handler, replies `200 {"results": [...], "signer_id": "<b64>", "signature": "<b64>"}`.
  Malformed/missing `query` -> `400`. Handler exception -> `500`.
- `GET /health` -- `200 "ok"`.
- `POST /pop/gossip` -- accepts a remote gossip payload (not parsed further;
  the SDK does not keep a peer table), replies `200` with the agent's own
  self-payload (same shape as the gossip push below).
- **Gossip push loop** -- every `EM_FILTER_GOSSIP_INTERVAL_MS` (default
  5000ms), `POST`s the self-payload to each resolved disco's `/pop/gossip`:

  ```json
  {
    "id": "<b64 id>", "name": "<name>",
    "host": "<advertise host>", "query_port": <int>,
    "pubkey": "<b64>", "sig": "<b64 selfsig>",
    "capabilities": ["search", "query", ...],
    "role": "filter"
  }
  ```

The filter must be reachable at `host:query_port` for this transport to work
(public IP, port-forward, or a tunnel).

## Model B -- WS relay (default, NAT-friendly)

The filter opens an outbound WebSocket to `ws(s)://<disco>/ws/filter` and
never needs inbound reachability.

- **Handshake** (filter -> disco):
  ```json
  {"action": "hello", "name": "<name>", "pubkey": "<b64>",
   "sig": "<b64 selfsig>", "capabilities": ["..."]}
  ```
  Ack: `{"action": "hello_ok", "id": "<b64 id>"}` (or
  `{"action": "error", "reason": "..."}`, in which case the client
  disconnects and reconnects after `EM_FILTER_RECONNECT_MS`).
- **Query** (disco -> filter): `{"action": "query", "id": "<qid>", "body": "<query>"}`.
- **Result** (filter -> disco):
  ```json
  {"action": "result", "id": "<qid>", "results": [...],
   "signer_id": "<b64>", "signature": "<b64>"}
  ```
  The filter signs `canonical_response(results)` itself -- the disco is a dumb
  pipe and cannot forge a result for this id (it never holds the private key).
- On disconnect (network error, dead socket, rejected hello), the client
  reconnects after `EM_FILTER_RECONNECT_MS` (default 5000ms). Handler memory
  resets to `{}` on each new session.

## Mode selection

`EM_FILTER_MODE` = `relay` (default) | `direct` | `both`. `both` runs the
direct HTTP server and the relay WS client(s) concurrently, under the same
identity.
