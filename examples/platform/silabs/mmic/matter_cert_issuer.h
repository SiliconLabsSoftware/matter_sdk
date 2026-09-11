#pragma once

#include "chip_tool_storage.h"

#include <cstdint>
#include <string>
#include <vector>

// Result of building a fresh operational certificate for a target NodeID.
// Everything is in Matter-TLV form except opKeyPub/opKeyPriv which are the
// raw EC material (uncompressed point / scalar).
struct IssuedNoc
{
    bool                 ok = false;
    std::string          error;
    std::vector<uint8_t> nocTlv;
    std::vector<uint8_t> opKeyPub;   // 65 bytes, uncompressed EC point
    std::vector<uint8_t> opKeyPriv;  // 32 bytes, raw scalar
    std::vector<uint8_t> tbsDer;     // ASN.1 DER TBS actually signed (for tests)
};

// Build a Matter-TLV NOC for `nodeId` on the fabric represented by
// `storage`, signed with the ICAC private key from `storage`, and
// generate a fresh operational P-256 key pair. Returns an `IssuedNoc`
// with `ok = true` on success. On failure `ok = false` and `error`
// is populated. Must be called after loadChipToolStorage() has returned
// true. OpenSSL must be linked into the binary.
IssuedNoc issueNoc(const ChipToolStorage & storage, uint64_t nodeId);
