/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <platform/silabs/address_resolve/PreCommissioning.h>
#include <platform/silabs/address_resolve/DirectConfig.h>

#include <credentials/CHIPCert.h>
#include <crypto/CHIPCryptoPAL.h>
#include <inet/IPAddress.h>
#include <lib/support/DefaultStorageKeyAllocator.h>
#include <lib/support/Span.h>
#include <platform/KeyValueStoreManager.h>
#include <platform/OpenThread/OpenThreadUtils.h>

#include <nvm3_default.h>
#include <psa/crypto.h>
#include <sl_psa_crypto.h>

#include <cinttypes>
#include <cstring>

extern "C" {
    #include "platform-efr32.h"
    otInstance * otGetInstance(void);
}
namespace chip {
namespace DeviceLayer {
namespace Internal {

namespace {

#ifdef USE_HARDCODED_VALUES
// Thread provisioning blobs.
// OpenThread NVM3 object keys
constexpr uint32_t kOtNvm3KeyActiveDataset = 0x20100; // OT_SETTINGS_KEY_ACTIVE_DATASET
constexpr uint32_t kOtNvm3KeyNetworkInfo   = 0x20300; // OT_SETTINGS_KEY_NETWORK_INFO

// Active Operational Dataset TLVs
constexpr uint8_t kThreadActiveDataset[] = {
    0x0e, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x0e, 0x4a,
    0x03, 0x00, 0x00, 0x19, 0x35, 0x06, 0x00, 0x04, 0x00, 0x1f, 0xff, 0xe0, 0x02, 0x08, 0xa1, 0x77,
    0x07, 0xfd, 0x5d, 0xd1, 0x1c, 0x06, 0x07, 0x08, 0xfd, 0xf6, 0x5d, 0x1c, 0x88, 0x22, 0x64, 0x04,
    0x05, 0x10, 0x7c, 0xb5, 0x40, 0x7c, 0x07, 0xfd, 0xbf, 0xaa, 0x8b, 0xbe, 0xcb, 0xe1, 0xd7, 0x1c,
    0xbc, 0xb6, 0x03, 0x0f, 0x4f, 0x70, 0x65, 0x6e, 0x54, 0x68, 0x72, 0x65, 0x61, 0x64, 0x2d, 0x35,
    0x66, 0x31, 0x66, 0x01, 0x02, 0x5f, 0x1f, 0x04, 0x10, 0xb8, 0xdd, 0xa7, 0x78, 0x28, 0xd3, 0x65,
    0xd5, 0x20, 0x7a, 0x5a, 0x01, 0x44, 0xd3, 0x7b, 0x31, 0x0c, 0x04, 0x02, 0xa0, 0xf7, 0xf8
};

// NetworkInfo struct
constexpr uint8_t kThreadNetworkInfo[] = {
    0x02, 0x04, 0x05, 0x04, 0x00, 0x00, 0x00, 0x00, 0xea, 0x03, 0x00, 0x00, 0xea, 0x03, 0x00, 0x00,
    0x21, 0x7c, 0x8e, 0x77, 0x22, 0x03, 0x76, 0xe3, 0x11, 0x5a, 0x45, 0x12, 0x72, 0xdf, 0x7c, 0xd1,
    0xc9, 0x91, 0x55, 0x56, 0x05, 0x00
};

// Matter fabric data.
// PLACEHOLDER: Root CA certificate (Matter TLV).
constexpr uint8_t kRcac[] = { 
    0x15, 0x30, 0x01, 0x01, 0x01, 0x24, 0x02, 0x01, 0x37, 0x03, 0x24, 0x14, 0x01, 0x18, 0x26, 0x04,
    0x80, 0x22, 0x81, 0x27, 0x26, 0x05, 0x80, 0x25, 0x4d, 0x3a, 0x37, 0x06, 0x24, 0x14, 0x01, 0x18,
    0x24, 0x07, 0x01, 0x24, 0x08, 0x01, 0x30, 0x09, 0x41, 0x04, 0x3a, 0x39, 0x21, 0xfc, 0x8e, 0x83,
    0x92, 0xa8, 0x3e, 0x9e, 0x9a, 0x2e, 0xe9, 0x80, 0x9f, 0x5e, 0x89, 0x99, 0x93, 0x09, 0xa2, 0x17,
    0x8a, 0x16, 0x14, 0xf8, 0xb2, 0x03, 0xbe, 0x44, 0x5d, 0xb0, 0x82, 0x60, 0x30, 0x1d, 0xe0, 0x06,
    0xb4, 0x9c, 0x92, 0xd7, 0xe9, 0x6a, 0x27, 0xfa, 0x2e, 0x7a, 0x13, 0x51, 0xb2, 0x52, 0xca, 0x35,
    0x18, 0x78, 0xd2, 0x0a, 0xd5, 0xa9, 0xe7, 0x39, 0x84, 0x7c, 0x37, 0x0a, 0x35, 0x01, 0x29, 0x01,
    0x18, 0x24, 0x02, 0x60, 0x30, 0x04, 0x14, 0x97, 0x51, 0x6c, 0x19, 0x4a, 0x98, 0xd2, 0x97, 0x73,
    0x09, 0xa6, 0x03, 0x9d, 0x76, 0x25, 0xc0, 0x05, 0x0c, 0x72, 0x4b, 0x30, 0x05, 0x14, 0x97, 0x51,
    0x6c, 0x19, 0x4a, 0x98, 0xd2, 0x97, 0x73, 0x09, 0xa6, 0x03, 0x9d, 0x76, 0x25, 0xc0, 0x05, 0x0c,
    0x72, 0x4b, 0x18, 0x30, 0x0b, 0x40, 0x11, 0x9d, 0x10, 0x41, 0x01, 0x05, 0x73, 0xac, 0x87, 0x98,
    0x2c, 0xfa, 0x8b, 0x54, 0xc2, 0xad, 0x5e, 0x98, 0x5c, 0x09, 0x56, 0x20, 0x97, 0xfc, 0xc8, 0x33,
    0x43, 0x9c, 0xaf, 0xc4, 0xd8, 0x7a, 0x82, 0xd1, 0x06, 0x89, 0x9e, 0xc1, 0xb5, 0x30, 0x75, 0x2a,
    0xa2, 0x1c, 0x1f, 0xdc, 0x22, 0x87, 0x58, 0x86, 0x93, 0x1c, 0x50, 0x47, 0x60, 0x85, 0x91, 0xbb,
    0x33, 0x65, 0x18, 0x3c, 0x41, 0x7f, 0x18 };
    
// PLACEHOLDER: Intermediate CA certificate (Matter TLV). Optional depending on the chain. If no ICAC, leave empty.
constexpr uint8_t kIcac[] = { 
    0x15, 0x30, 0x01, 0x01, 0x01, 0x24, 0x02, 0x01, 0x37, 0x03, 0x24, 0x14, 0x01, 0x18, 0x26, 0x04,
    0x80, 0x22, 0x81, 0x27, 0x26, 0x05, 0x80, 0x25, 0x4d, 0x3a, 0x37, 0x06, 0x24, 0x13, 0x02, 0x18,
    0x24, 0x07, 0x01, 0x24, 0x08, 0x01, 0x30, 0x09, 0x41, 0x04, 0x18, 0x3f, 0xe9, 0x81, 0xe4, 0xf4,
    0x97, 0x72, 0xc4, 0x0c, 0x43, 0xdb, 0xf7, 0xfc, 0x19, 0x3b, 0xcf, 0xc2, 0x6d, 0x10, 0xe3, 0x61,
    0x5b, 0xed, 0xc4, 0x1e, 0x05, 0x5f, 0x2f, 0xf5, 0x41, 0xf0, 0x90, 0xd1, 0x63, 0x8f, 0xa7, 0x76,
    0xe4, 0x5b, 0xda, 0x7a, 0x9b, 0x5a, 0x84, 0x10, 0xa8, 0x00, 0x44, 0x30, 0xb5, 0x5c, 0x57, 0x93,
    0x4d, 0x57, 0x54, 0x0e, 0x03, 0x11, 0x57, 0xaa, 0x75, 0x53, 0x37, 0x0a, 0x35, 0x01, 0x29, 0x01,
    0x18, 0x24, 0x02, 0x60, 0x30, 0x04, 0x14, 0x2d, 0x28, 0xd4, 0xe9, 0x96, 0x6e, 0x29, 0x93, 0x8d,
    0x6b, 0x35, 0x11, 0x04, 0xb9, 0xca, 0x7d, 0xea, 0x2b, 0xe4, 0x12, 0x30, 0x05, 0x14, 0x97, 0x51,
    0x6c, 0x19, 0x4a, 0x98, 0xd2, 0x97, 0x73, 0x09, 0xa6, 0x03, 0x9d, 0x76, 0x25, 0xc0, 0x05, 0x0c,
    0x72, 0x4b, 0x18, 0x30, 0x0b, 0x40, 0xdf, 0xe0, 0x49, 0xad, 0x71, 0x38, 0x19, 0x4c, 0xa9, 0x11,
    0x79, 0xf7, 0x5d, 0x3b, 0x71, 0x78, 0x2f, 0x73, 0x9f, 0x4d, 0xaa, 0xa7, 0x7e, 0xc5, 0x6f, 0x3b,
    0xc4, 0x8a, 0xce, 0xfd, 0x64, 0xb2, 0x24, 0xef, 0x61, 0xe5, 0x47, 0x2f, 0xaf, 0xf0, 0x93, 0x27,
    0xc0, 0xb2, 0xf5, 0x26, 0x24, 0x87, 0x84, 0xc1, 0xea, 0xf5, 0x79, 0xf3, 0x5a, 0xf2, 0x96, 0x6e,
    0x5e, 0xf6, 0xa4, 0x0b, 0x40, 0xd9, 0x18 };

// PLACEHOLDER: Node Operational Certificate (Matter TLV). Encodes this device's
// Used to derive the public key for the operational keypair, must match.
constexpr uint8_t kNoc[] = { 
    0x15, 0x30, 0x01, 0x01, 0x01, 0x24, 0x02, 0x01, 0x37, 0x03, 0x24, 0x13, 0x02, 0x18, 0x26, 0x04,
    0x80, 0x22, 0x81, 0x27, 0x26, 0x05, 0x80, 0x25, 0x4d, 0x3a, 0x37, 0x06, 0x24, 0x15, 0x01, 0x24,
    0x11, 0x01, 0x18, 0x24, 0x07, 0x01, 0x24, 0x08, 0x01, 0x30, 0x09, 0x41, 0x04, 0x1c, 0x8c, 0x23,
    0xc2, 0xd8, 0x52, 0x7e, 0x8c, 0xf9, 0x82, 0x64, 0xf3, 0x63, 0x5d, 0x7f, 0x20, 0x14, 0x28, 0x36,
    0x5d, 0x81, 0xc7, 0xe8, 0x2c, 0x79, 0x5b, 0x7f, 0xdb, 0x08, 0x40, 0x64, 0x6e, 0x0c, 0xfb, 0x72,
    0xcf, 0xe1, 0xb0, 0x1c, 0x73, 0x21, 0x59, 0xea, 0xd7, 0x4f, 0xdd, 0xd0, 0x74, 0x0e, 0xaa, 0xd8,
    0x8a, 0xe7, 0xac, 0x15, 0x93, 0x14, 0xe2, 0xe8, 0xf5, 0x04, 0x3d, 0x3c, 0x45, 0x37, 0x0a, 0x35,
    0x01, 0x28, 0x01, 0x18, 0x24, 0x02, 0x01, 0x36, 0x03, 0x04, 0x02, 0x04, 0x01, 0x18, 0x30, 0x04,
    0x14, 0x1a, 0x5f, 0x50, 0x12, 0xe8, 0xfa, 0x61, 0x6c, 0xf7, 0x75, 0x0b, 0x72, 0xd4, 0xdd, 0x57,
    0x42, 0x4d, 0x95, 0x27, 0x18, 0x30, 0x05, 0x14, 0x2d, 0x28, 0xd4, 0xe9, 0x96, 0x6e, 0x29, 0x93,
    0x8d, 0x6b, 0x35, 0x11, 0x04, 0xb9, 0xca, 0x7d, 0xea, 0x2b, 0xe4, 0x12, 0x18, 0x30, 0x0b, 0x40,
    0x2d, 0x54, 0xa7, 0xd0, 0xd0, 0xe0, 0xe7, 0x88, 0x35, 0xb4, 0x90, 0xad, 0x67, 0x1a, 0xed, 0xb8,
    0xb5, 0x4d, 0x99, 0x0e, 0xfa, 0x19, 0x83, 0xa2, 0xda, 0x2e, 0x15, 0xcf, 0x01, 0x2c, 0x6b, 0xf3,
    0xcb, 0x3a, 0x6f, 0xa6, 0xb1, 0x6c, 0x33, 0xcb, 0xcc, 0xd6, 0xcc, 0x55, 0xad, 0xe0, 0x87, 0x8d,
    0x39, 0xa5, 0xad, 0x7c, 0x54, 0x59, 0x91, 0xbc, 0x73, 0x8f, 0x4b, 0x7c, 0xe4, 0xb7, 0x60, 0x92,
    0x18 };

// PLACEHOLDER: Operational keypair serialization, must match the public key certified in the NOC.
constexpr uint8_t kOpKey[] = { 
    0x04, 0x1c, 0x8c, 0x23, 0xc2, 0xd8, 0x52, 0x7e, 0x8c, 0xf9, 0x82, 0x64, 0xf3, 0x63, 0x5d, 0x7f,
    0x20, 0x14, 0x28, 0x36, 0x5d, 0x81, 0xc7, 0xe8, 0x2c, 0x79, 0x5b, 0x7f, 0xdb, 0x08, 0x40, 0x64,
    0x6e, 0x0c, 0xfb, 0x72, 0xcf, 0xe1, 0xb0, 0x1c, 0x73, 0x21, 0x59, 0xea, 0xd7, 0x4f, 0xdd, 0xd0,
    0x74, 0x0e, 0xaa, 0xd8, 0x8a, 0xe7, 0xac, 0x15, 0x93, 0x14, 0xe2, 0xe8, 0xf5, 0x04, 0x3d, 0x3c,
    0x45, 0x99, 0x45, 0x40, 0x2b, 0x00, 0xe7, 0x4f, 0x10, 0xc4, 0x46, 0x68, 0x70, 0xcb, 0x82, 0xb1,
    0x85, 0x82, 0x44, 0x36, 0x50, 0x36, 0x6b, 0x89, 0x76, 0x39, 0xaa, 0x10, 0x08, 0x7a, 0x4d, 0x49,
    0x17 };

// PLACEHOLDER: FabricMetadata TLV (vendor id + fabric label)
constexpr uint8_t kFabricMetadata[] = { 0x15, 0x25, 0x00, 0xf1, 0xff, 0x2c, 0x01, 0x00, 0x18 };

// PLACEHOLDER: FabricIndexInfo TLV (the list of in-use fabric indices)
constexpr uint8_t kFabricIndexInfo[] = { 0x15, 0x24, 0x00, 0x02, 0x36, 0x01, 0x04, 0x01, 0x18, 0x18 };

// PLACEHOLDER: ACL 0 0 
constexpr uint8_t kAcl00[] = { 
    0x15, 0x24, 0x01, 0x05, 0x24, 0x02, 0x02, 0x36, 0x03, 0x06, 0x69, 0xb6, 0x01, 0x00, 0x18, 0x34,
    0x04, 0x18 };

// PLACEHOLDER: ACL 0 1 (Allows switch to control the light)
constexpr uint8_t kAcl01[] = { 
    0x15, 0x24, 0x01, 0x05, 0x24, 0x02, 0x02, 0x36, 0x03, 0x06, 0x69, 0xb6, 0x01, 0x00, 0x18, 0x34,
    0x04, 0x18 };

// PLACEHOLDER: Binding table (For the switch)
constexpr uint8_t kBindingTable[] = {
        0x15, 0x24, 0x01, 0x01, 0x24, 0x02, 0x00, 0x18
    };

// PLACEHOLDER: Binding table 0 (For the switch)
constexpr uint8_t kBindingTable0[] = {
        0x15, 0x24, 0x01, 0x01, 0x24, 0x02, 0x01, 0x24, 0x03, 0x06, 0x24, 0x04, 0x01, 0x24, 0x05, 0x02,
        0x24, 0x07, 0xff, 0x18
    };

// Target (peer) connection parameters
constexpr char kTargetAddress[]      = "edaa:b87e:e645:cc68:ffb2:df98:190:d298";
constexpr uint16_t kTargetPort       = 5540;
constexpr CompressedFabricId kTargetCompressedFabricId = 1984710029625773304ULL;
constexpr NodeId kTargetNodeId       = 2;
#endif // USE_HARDCODED_VALUES

// Fabric index
constexpr FabricIndex kFabricIndex = 1;

// Operational keypair lengths
constexpr size_t kPubKeyLen     = Crypto::kP256_PublicKey_Length; 
constexpr size_t kPrivKeyLen    = Crypto::kP256_PrivateKey_Length;
constexpr size_t kSerializedLen = kPubKeyLen + kPrivKeyLen;

// PSA key id 0x4400 == opaque id 0 (PSA_KEY_ID_FOR_MATTER_MIN + 0). 
constexpr psa_key_id_t kOpKeyPsaId = 0x4400U;

constexpr size_t kMaxIPv6StringLength = 40;

#ifdef USE_HARDCODED_VALUES
CHIP_ERROR WriteThreadObject(uint32_t nvm3Key, ByteSpan data)
{
    VerifyOrReturnLogError(nvm3_writeData(nvm3_defaultHandle, nvm3Key, data.data(), data.size()) == ECODE_NVM3_OK,
                        CHIP_ERROR_INTERNAL);
    return CHIP_NO_ERROR;
}

CHIP_ERROR WriteMatterKey(const char * key, ByteSpan data)
{
    return PersistedStorage::KeyValueStoreMgr().Put(key, data.data(), data.size());
}
#endif // USE_HARDCODED_VALUES

CHIP_ERROR SetupOpKey(ByteSpan serialized)
{
    // Only the private scalar is imported; PSA re-derives the public key from it.
    const uint8_t * privKey = serialized.data() + kPubKeyLen;

    // Public key the NOC certifies, used below to reject a mismatched blob.
    uint8_t nocBuf[Credentials::kMaxCHIPCertLength];
    size_t nocLen = 0;
    ReturnErrorOnFailure(PersistedStorage::KeyValueStoreMgr().Get(
        DefaultStorageKeyAllocator::FabricNOC(kFabricIndex).KeyName(), nocBuf, sizeof(nocBuf), &nocLen));

    Credentials::P256PublicKeySpan nocPublicKey;
    ReturnErrorOnFailure(Credentials::ExtractPublicKeyFromChipCert(ByteSpan(nocBuf, nocLen), nocPublicKey));
    // Import the operational keypair into PSA
    psa_crypto_init();

    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_id(&attr, kOpKeyPsaId);

    // PSA dictates the true location of the key.
    psa_set_key_lifetime(
        &attr, PSA_KEY_LIFETIME_FROM_PERSISTENCE_AND_LOCATION(PSA_KEY_LIFETIME_PERSISTENT,
                                                               sl_psa_get_most_secure_key_location()));
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, 256);
    psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));

    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_SIGN_HASH);

    mbedtls_svc_key_id_t importedId;
    psa_status_t status = psa_import_key(&attr, privKey, kPrivKeyLen, &importedId);
    if (status == PSA_ERROR_ALREADY_EXISTS)
    {
        // Shouldn't happen, replace if it does.
        ChipLogProgress(DeviceLayer, "LoadFabric: PSA op key 0x%" PRIx32 " exists, replacing",
                        static_cast<uint32_t>(kOpKeyPsaId));
        psa_destroy_key(kOpKeyPsaId);
        status = psa_import_key(&attr, privKey, kPrivKeyLen, &importedId);
    }
    psa_reset_key_attributes(&attr);
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    // Cross-check the imported key against the NOC.
    uint8_t derivedPublicKey[kPubKeyLen];
    size_t derivedLen      = 0;
    psa_status_t pubStatus = psa_export_public_key(kOpKeyPsaId, derivedPublicKey, sizeof(derivedPublicKey), &derivedLen);
    if (pubStatus != PSA_SUCCESS || derivedLen != kPubKeyLen ||
        memcmp(derivedPublicKey, nocPublicKey.data(), kPubKeyLen) != 0)
    {
        ChipLogError(DeviceLayer, "LoadFabric: imported op key does not match NOC public key");
        psa_destroy_key(kOpKeyPsaId);
        return CHIP_ERROR_INVALID_PUBLIC_KEY;
    }
    ChipLogProgress(DeviceLayer, "LoadFabric: PSA op key 0x%" PRIx32 " imported and matches NOC",
                    static_cast<uint32_t>(kOpKeyPsaId));

    // Register the fabric→slot mapping for Efr32PsaOperationalKeystore::Init()
    const uint8_t opKeyMap[] = { kFabricIndex };
    ReturnErrorOnFailure(
        SilabsConfig::WriteConfigValueBin(SilabsConfig::kConfigKey_OpKeyMap, opKeyMap, sizeof(opKeyMap)));

    return CHIP_NO_ERROR;
}

CHIP_ERROR ValidateKeyPresence(){
    psa_crypto_init();
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_status_t status = psa_get_key_attributes(kOpKeyPsaId, &attr);
    psa_reset_key_attributes(&attr);
    if (status == PSA_SUCCESS)
    {
        // Key already in PSA storage, nothing to import
        return CHIP_NO_ERROR;
    }
    return CHIP_ERROR_NOT_FOUND; // or whatever error path you want
}

} // namespace

CHIP_ERROR PreCommissioning::Init()
{
    // If the Opkey is not found and not in the PSA storage, we abandon preloading the fabric and the thread dataset.
    ReturnLogErrorOnFailure(LoadFabric());
  
    ReturnLogErrorOnFailure(LoadThread());

    return LoadTarget();
}

CHIP_ERROR PreCommissioning::GetTargetAddress(Transport::PeerAddress & address) const{
    address = mTargetAddress;
    return CHIP_NO_ERROR;
}
CHIP_ERROR PreCommissioning::GetTargetMrpConfig(ReliableMessageProtocolConfig & mrpConfig) const{
    mrpConfig = mTargetMrpConfig;
    return CHIP_NO_ERROR;
}

CHIP_ERROR PreCommissioning::GetTargetPeerId(PeerId & peerId) const
{
    peerId = mTargetPeerId;
    return CHIP_NO_ERROR;
}

CHIP_ERROR PreCommissioning::GetTargetExtAddress(otExtAddress & extAddress) const
{
    extAddress = mTargetExtAddress;
    return CHIP_NO_ERROR;
}

CHIP_ERROR PreCommissioning::SetupThread()
{
#ifdef USE_HARDCODED_VALUES
    // TODO: Placeholder to have a working example of setting up the Thread connection at runtime, will be use for OOB commissioning but not currently needed.
    ReturnErrorOnFailure(WriteThreadObject(kOtNvm3KeyActiveDataset, ByteSpan(kThreadActiveDataset)));
    ReturnErrorOnFailure(WriteThreadObject(kOtNvm3KeyNetworkInfo, ByteSpan(kThreadNetworkInfo)));
#endif // USE_HARDCODED_VALUES
    return CHIP_NO_ERROR;
}

CHIP_ERROR PreCommissioning::SetupMatterFabric()
{
#ifdef USE_HARDCODED_VALUES
    // TODO: Placeholder to have a working example of setting up the Matter fabric at runtime, will be use for OOB commissioning but not currently needed.
    
    // Operational certificate chain. The NOC must be written before SetupOpKey()
    // runs, since the helper reads it back to cross-check the imported keypair.
    ReturnErrorOnFailure(WriteMatterKey(DefaultStorageKeyAllocator::FabricRCAC(kFabricIndex).KeyName(), ByteSpan(kRcac)));
    ReturnErrorOnFailure(WriteMatterKey(DefaultStorageKeyAllocator::FabricICAC(kFabricIndex).KeyName(), ByteSpan(kIcac)));
    ReturnErrorOnFailure(WriteMatterKey(DefaultStorageKeyAllocator::FabricNOC(kFabricIndex).KeyName(), ByteSpan(kNoc)));

    // Fabric data
    ReturnErrorOnFailure(WriteMatterKey(DefaultStorageKeyAllocator::FabricMetadata(kFabricIndex).KeyName(), ByteSpan(kFabricMetadata)));
    ReturnErrorOnFailure(WriteMatterKey(DefaultStorageKeyAllocator::FabricIndexInfo().KeyName(), ByteSpan(kFabricIndexInfo)));

    // Access control for the controller (otbr)
    ReturnErrorOnFailure(
        WriteMatterKey(DefaultStorageKeyAllocator::AccessControlAclEntry(kFabricIndex, 0).KeyName(), ByteSpan(kAcl00)));
    
    // Access control for the switch (For the light)
    ReturnErrorOnFailure(
        WriteMatterKey(DefaultStorageKeyAllocator::AccessControlAclEntry(kFabricIndex, 1).KeyName(), ByteSpan(kAcl01)));
        
    // Binding table (For the switch)
    ReturnErrorOnFailure(WriteMatterKey(DefaultStorageKeyAllocator::BindingTable().KeyName(), ByteSpan(kBindingTable)));

    // Oprational keypair
    ReturnErrorOnFailure(SetupOpKey(ByteSpan(kOpKey)));
#endif // USE_HARDCODED_VALUES
    return CHIP_NO_ERROR;
}

CHIP_ERROR PreCommissioning::SetupTarget()
{
#ifdef USE_HARDCODED_VALUES
    Inet::IPAddress addr;
    VerifyOrReturnError(Inet::IPAddress::FromString(kTargetAddress, addr), CHIP_ERROR_INVALID_ARGUMENT);

    mTargetAddress = Transport::PeerAddress::UDP(addr, kTargetPort);
    mTargetPeerId  = PeerId(kTargetCompressedFabricId, kTargetNodeId);
    mTargetMrpConfig = { System::Clock::Milliseconds32(2000), System::Clock::Milliseconds32(2000), System::Clock::Milliseconds16(4000) };
#endif // USE_HARDCODED_VALUES
    return CHIP_NO_ERROR;
}

CHIP_ERROR PreCommissioning::LoadThread()
{
    // This is already currently performed at boot time in sl_ot_init(), here we just need to retrieve the ext address.
    size_t outLen = 0;
    ReturnErrorOnFailure(SilabsConfig::ReadConfigValueBin(kTargetExtendedAddress, mTargetExtAddress.m8, sizeof(otExtAddress), outLen));
    VerifyOrReturnError(outLen == sizeof(otExtAddress), CHIP_ERROR_INVALID_ARGUMENT);
    return CHIP_NO_ERROR;
}

CHIP_ERROR PreCommissioning::LoadFabric()
{
    // The Fabric RCAC, NOC and ICAC are already loaded in the FabricTable, here we only retrieve the operational keypair 
    // from the NVM3, verify it matches the NOC, store it in the PSA and remove the NVM3 entry.
    uint8_t serialized[kSerializedLen];
    size_t outLen = 0;
    CHIP_ERROR error = SilabsConfig::ReadConfigValueBin(kOperationalKeypair, serialized, kSerializedLen, outLen);
    if (error == CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND)
    {
        // Verify if it is already in the PSA storage
        VerifyOrReturnError(ValidateKeyPresence() == CHIP_NO_ERROR, CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND);
        return CHIP_NO_ERROR;
    }
    VerifyOrReturnError(outLen == kSerializedLen, CHIP_ERROR_INVALID_ARGUMENT);
    ReturnErrorOnFailure(SetupOpKey(ByteSpan(serialized, kSerializedLen)));

    // Clearing the opkey for security
    return SilabsConfig::ClearConfigValue(kOperationalKeypair);
}

CHIP_ERROR PreCommissioning::LoadTarget()
{
    char targetAddress[kMaxIPv6StringLength];
    size_t outLen = 0;
    ReturnErrorOnFailure(SilabsConfig::ReadConfigValueStr(kPairedIPV6Address, targetAddress, sizeof(targetAddress), outLen));
    VerifyOrReturnError(outLen > 0 && outLen < sizeof(targetAddress), CHIP_ERROR_INVALID_ARGUMENT);
    Inet::IPAddress addr;
    VerifyOrReturnError(Inet::IPAddress::FromString(targetAddress, addr), CHIP_ERROR_INVALID_ARGUMENT);
    
    uint16_t port = 0;
    ReturnErrorOnFailure(SilabsConfig::ReadConfigValue(kPairedPort, port));
    mTargetAddress = Transport::PeerAddress::UDP(addr, port);

    uint32_t idleInterval;
    uint32_t activeInterval;
    uint32_t activeThresholdTime;
    ReturnErrorOnFailure(SilabsConfig::ReadConfigValue(kMRPRemoteIdleInterval, idleInterval));
    ReturnErrorOnFailure(SilabsConfig::ReadConfigValue(kMRPRemoteActiveInterval, activeInterval));
    ReturnErrorOnFailure(SilabsConfig::ReadConfigValue(kMRPRemoteActiveThresholdTime, activeThresholdTime));
    mTargetMrpConfig.mIdleRetransTimeout = System::Clock::Milliseconds32(idleInterval);
    mTargetMrpConfig.mActiveRetransTimeout = System::Clock::Milliseconds32(activeInterval);
    mTargetMrpConfig.mActiveThresholdTime = System::Clock::Milliseconds16(activeThresholdTime);

    CompressedFabricId compressedFabricId;
    ReturnErrorOnFailure(SilabsConfig::ReadConfigValue(kPairedCompressedFabricId, compressedFabricId));
    mTargetPeerId.SetCompressedFabricId(compressedFabricId);

    NodeId nodeId;
    ReturnErrorOnFailure(SilabsConfig::ReadConfigValue(kPairedNodeId, nodeId));
    mTargetPeerId.SetNodeId(nodeId);
    return CHIP_NO_ERROR;
}
} // namespace Internal
} // namespace DeviceLayer
} // namespace chip