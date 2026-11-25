/*
 *
 *    Copyright (c) 2020-2022 Project CHIP Authors
 *    Copyright (c) 2019 Nest Labs, Inc.
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

/**
 *    @file
 *          Utilities for accessing persisted device configuration on
 *          platforms based on the Silicon Labs SDK.
 */

#pragma once

#include <functional>
#include <platform/CHIPDeviceError.h>

#if defined(SL_COMMON_TOKEN_MANAGER_ENABLE_DYNAMIC_TOKENS) || defined(SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS)
#include <sl_token_manager_defines.h>
#include <sl_token_manager_manufacturing.h>
#endif
#ifndef SL_COMMON_TOKEN_MANAGER_ENABLE_DYNAMIC_TOKENS
#include "nvm3.h"
#include "nvm3_hal_flash.h"
#endif // SL_COMMON_TOKEN_MANAGER_ENABLE_DYNAMIC_TOKENS

#ifndef KVS_MAX_ENTRIES
#define KVS_MAX_ENTRIES 255 // Available key slot count for Kvs Key mapping.
#endif

// Delay before Key/Value is actually saved in NVM
#ifndef SL_KVS_SAVE_DELAY_SECONDS
#define SL_KVS_SAVE_DELAY_SECONDS 2
#endif

static_assert((KVS_MAX_ENTRIES <= 511), "Implementation supports up to 511 Kvs entries");
static_assert((KVS_MAX_ENTRIES >= 30), "Mininimal Kvs entries requirement is not met");

namespace chip {
namespace DeviceLayer {
namespace Internal {

/**
 *
 * This implementation uses the Silicon Labs Silabs NVM3 flash data storage library
 * as the underlying storage layer.
 *
 * NOTE: This class is designed to be mixed-in to the concrete subclass of the
 * GenericConfigurationManagerImpl<> template.  When used this way, the class
 * naturally provides implementations for the delegated members referenced by
 * the template class (e.g. the ReadConfigValue() method).
 */

#ifdef SL_COMMON_TOKEN_MANAGER_ENABLE_DYNAMIC_TOKENS
inline constexpr uint32_t kUserNvm3KeyDomainLoLimit = SL_TOKEN_NVM3_REGION_USER;
inline constexpr uint32_t kUserNvm3KeyDomainHiLimit = SL_TOKEN_NVM3_REGION_ZIGBEE - 1;

// Only keep the MSBs of the Matter Region. The LSB of the region is determined by the keyBaseOffset.
// with SilabsConfigKey Helper function.
inline constexpr uint32_t kMatterNvm3KeyDomain = (SL_TOKEN_NVM3_REGION_MATTER & 0xFFFF000);

constexpr inline uint32_t SilabsConfigKey(uint8_t keyBaseOffset, uint8_t id)
{
    return SL_TOKEN_TYPE_NVM3 | kMatterNvm3KeyDomain | static_cast<uint32_t>(keyBaseOffset) << 8 | id;
}

#else
// Silabs NVM3 objects use a 20-bit number,
// NVM3 Key 19:16 Stack region
// NVM3 Key 15:0 Available NVM3 keys 0x0000 -> 0xFFFF.
// Matter stack reserved region ranges from 0x087200 to 0x087FFF
// e.g. key = 0x087201
// '08' = Matter nvm3 region
// '72' = the sub region group base offset (Factory, Config, Counter or KVS)
// '01' = the id offset inside the group.
inline constexpr uint32_t kUserNvm3KeyDomainLoLimit = 0x000000U; // User Domain NVM3 Key Range lower limit
inline constexpr uint32_t kUserNvm3KeyDomainHiLimit = 0x00FFFFU; // User Domain NVM3 Key Range Maximum limit
inline constexpr uint32_t kMatterNvm3KeyDomain      = 0x087000U; // Matter specific NVM3 range
constexpr inline uint32_t SilabsConfigKey(uint8_t keyBaseOffset, uint8_t id)
{
    return kMatterNvm3KeyDomain | static_cast<uint32_t>(keyBaseOffset) << 8 | id;
}
#endif // SL_COMMON_TOKEN_MANAGER_ENABLE_DYNAMIC_TOKENS

inline constexpr uint32_t kMatterNvm3KeyLoLimit = 0x087200U; // Do not modify without Silabs GSDK team approval
inline constexpr uint32_t kMatterNvm3KeyHiLimit = 0x087FFFU; // Do not modify without Silabs GSDK team approval

#ifdef SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS
// Common Token Manager static secure tokens (SST) holding the Matter attestation certificates.
//
// Series 3: the 16-bit value is a key in the KLV chain. Matter has a reserved Key range (SL_TOKEN_DOMAIN_STATIC_MATTER - 0x380 to
// 0x3FF) as defined in sl_token_manager_defines.h.
//
// Series 2: the 16-bit value is a byte offset in the static secure (lockbits) page.
// The first 0x200 bytes hold the lock words and the SDK tokens end at 0x470 (sl_token_manager_manufacturing.h).
// Only the first 4 KB of the page are addressable. Offsets and sizes must be even.
// Maximum slot sizes:
// DAC and PAI: Credentials::kMaxDERCertLength, the specification limit for a DER certificate.
// CD: Credentials::kMaxCMSSignedCDMessage, rounded up to an even size.
#if defined(_SILICON_LABS_32B_SERIES_3)
inline constexpr uint16_t kMatterSstId_DAC = SL_TOKEN_STATIC_SECURE_DATA_TOKENS | SL_TOKEN_DOMAIN_STATIC_MATTER | 0x00;
inline constexpr uint16_t kMatterSstId_PAI = SL_TOKEN_STATIC_SECURE_DATA_TOKENS | SL_TOKEN_DOMAIN_STATIC_MATTER | 0x01;
inline constexpr uint16_t kMatterSstId_CD  = SL_TOKEN_STATIC_SECURE_DATA_TOKENS | SL_TOKEN_DOMAIN_STATIC_MATTER | 0x02;
#else
inline constexpr uint16_t kMatterSstId_DAC = SL_TOKEN_STATIC_SECURE_DATA_TOKENS | 0x480;
inline constexpr uint16_t kMatterSstId_PAI = SL_TOKEN_STATIC_SECURE_DATA_TOKENS | 0x6E0;
inline constexpr uint16_t kMatterSstId_CD  = SL_TOKEN_STATIC_SECURE_DATA_TOKENS | 0x940;
#endif // _SILICON_LABS_32B_SERIES_3
inline constexpr uint32_t kMatterSstMaxSize_DAC = 600;
inline constexpr uint32_t kMatterSstMaxSize_PAI = 600;
inline constexpr uint32_t kMatterSstMaxSize_CD  = 792;

// Builds the same 32-bit identifier as SL_TOKEN_GET_STATIC_SECURE_TOKEN(token): type | id | size << 16.
constexpr inline uint32_t SilabsStaticSecureToken(uint16_t id, uint32_t size)
{
    return SL_TOKEN_TYPE_STATIC_SECURE | id | (size << SL_TOKEN_STATIC_TOKEN_SIZE_OFFSET);
}

// Slot size encoded in a static secure token identifier.
constexpr inline uint32_t SilabsStaticSecureTokenSize(uint32_t token)
{
    return (token >> SL_TOKEN_STATIC_TOKEN_SIZE_OFFSET) & SL_TOKEN_STATIC_TOKEN_SIZE_MASK;
}

static_assert(kMatterSstMaxSize_CD <= SL_TOKEN_STATIC_TOKEN_SIZE_MASK, "SST size does not fit in the token identifier");

#if defined(_SILICON_LABS_32B_SERIES_2)
static_assert((kMatterSstId_DAC % 4) == 0 && (kMatterSstId_PAI % 4) == 0 && (kMatterSstId_CD % 4) == 0,
              "Series 2 SST offsets must be word aligned");
static_assert((kMatterSstMaxSize_DAC % 2) == 0 && (kMatterSstMaxSize_PAI % 2) == 0 && (kMatterSstMaxSize_CD % 2) == 0,
              "Series 2 SST sizes must be even");
static_assert((kMatterSstId_DAC & 0x0FFF) + kMatterSstMaxSize_DAC <= (kMatterSstId_PAI & 0x0FFF), "DAC slot overlaps PAI");
static_assert((kMatterSstId_PAI & 0x0FFF) + kMatterSstMaxSize_PAI <= (kMatterSstId_CD & 0x0FFF), "PAI slot overlaps CD");
static_assert((kMatterSstId_CD & 0x0FFF) + kMatterSstMaxSize_CD <= 0x1000, "CD slot exceeds the addressable SST page");
#endif // defined(_SILICON_LABS_32B_SERIES_2)
#endif // SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS

class SilabsConfig
{
public:
    // Definitions for Silicon Labs EFR32 NVM3 driver:-

    using Key = uint32_t;

    // NVM3 key base offsets used by the CHIP Device Layer.
    // ** Key base can range from 0x2 to 0xF **
    // Persistent config values set at manufacturing time. Retained during factory reset.
    static constexpr uint8_t kMatterFactory_KeyBase = 0x2;
    // Persistent config values set at runtime. Cleared during factory reset.
    static constexpr uint8_t kMatterConfig_KeyBase = 0x3;
    // Persistent counter values set at runtime. Retained during factory reset.
    static constexpr uint8_t kMatterCounter_KeyBase = 0x4;
    // Persistent config values set at runtime. Cleared during factory reset.
    static constexpr uint8_t kMatterKvs_KeyBase       = 0x5;
    static constexpr uint8_t kMatterKvs_ExtendedRange = 0x6;

    // Key definitions for well-known configuration values.
    // Factory config keys
    static constexpr Key kConfigKey_SerialNum              = SilabsConfigKey(kMatterFactory_KeyBase, 0x00);
    static constexpr Key kConfigKey_MfrDeviceId            = SilabsConfigKey(kMatterFactory_KeyBase, 0x01);
    static constexpr Key kConfigKey_MfrDeviceCert          = SilabsConfigKey(kMatterFactory_KeyBase, 0x02);
    static constexpr Key kConfigKey_MfrDevicePrivateKey    = SilabsConfigKey(kMatterFactory_KeyBase, 0x03);
    static constexpr Key kConfigKey_ManufacturingDate      = SilabsConfigKey(kMatterFactory_KeyBase, 0x04);
    static constexpr Key kConfigKey_SetupPayloadBitSet     = SilabsConfigKey(kMatterFactory_KeyBase, 0x05);
    static constexpr Key kConfigKey_MfrDeviceICACerts      = SilabsConfigKey(kMatterFactory_KeyBase, 0x06);
    static constexpr Key kConfigKey_SetupDiscriminator     = SilabsConfigKey(kMatterFactory_KeyBase, 0x07);
    static constexpr Key kConfigKey_Spake2pIterationCount  = SilabsConfigKey(kMatterFactory_KeyBase, 0x08);
    static constexpr Key kConfigKey_Spake2pSalt            = SilabsConfigKey(kMatterFactory_KeyBase, 0x09);
    static constexpr Key kConfigKey_Spake2pVerifier        = SilabsConfigKey(kMatterFactory_KeyBase, 0x0A);
    static constexpr Key kConfigKey_ProductId              = SilabsConfigKey(kMatterFactory_KeyBase, 0x0B);
    static constexpr Key kConfigKey_VendorId               = SilabsConfigKey(kMatterFactory_KeyBase, 0x0C);
    static constexpr Key kConfigKey_VendorName             = SilabsConfigKey(kMatterFactory_KeyBase, 0x0D);
    static constexpr Key kConfigKey_ProductName            = SilabsConfigKey(kMatterFactory_KeyBase, 0x0E);
    static constexpr Key kConfigKey_HardwareVersionString  = SilabsConfigKey(kMatterFactory_KeyBase, 0x0F);
    static constexpr Key kConfigKey_ProductLabel           = SilabsConfigKey(kMatterFactory_KeyBase, 0x10);
    static constexpr Key kConfigKey_ProductURL             = SilabsConfigKey(kMatterFactory_KeyBase, 0x11);
    static constexpr Key kConfigKey_PartNumber             = SilabsConfigKey(kMatterFactory_KeyBase, 0x12);
    static constexpr Key kConfigKey_CACerts                = SilabsConfigKey(kMatterFactory_KeyBase, 0x13);
    static constexpr Key kConfigKey_DeviceCerts            = SilabsConfigKey(kMatterFactory_KeyBase, 0x14);
    static constexpr Key kConfigKey_hostname               = SilabsConfigKey(kMatterFactory_KeyBase, 0x15);
    static constexpr Key kConfigKey_clientid               = SilabsConfigKey(kMatterFactory_KeyBase, 0x16);
    static constexpr Key kConfigKey_Test_Event_Trigger_Key = SilabsConfigKey(kMatterFactory_KeyBase, 0x17);
    static constexpr Key kConfigKey_HardwareVersion        = SilabsConfigKey(kMatterFactory_KeyBase, 0x18);
    // kConfigKey_PersistentUniqueId is the inputkey in the generating of the Rotating Device ID
    // SHALL NOT be the same as the UniqueID attribute exposed in the Basic Information cluster.
    static constexpr Key kConfigKey_PersistentUniqueId = SilabsConfigKey(kMatterFactory_KeyBase, 0x1F);
    static constexpr Key kConfigKey_Creds_KeyId        = SilabsConfigKey(kMatterFactory_KeyBase, 0x20);
    static constexpr Key kConfigKey_Creds_Base_Addr    = SilabsConfigKey(kMatterFactory_KeyBase, 0x21);
    static constexpr Key kConfigKey_Creds_DAC_Offset   = SilabsConfigKey(kMatterFactory_KeyBase, 0x22);
    static constexpr Key kConfigKey_Creds_DAC_Size     = SilabsConfigKey(kMatterFactory_KeyBase, 0x23);
    static constexpr Key kConfigKey_Creds_PAI_Offset   = SilabsConfigKey(kMatterFactory_KeyBase, 0x24);
    static constexpr Key kConfigKey_Creds_PAI_Size     = SilabsConfigKey(kMatterFactory_KeyBase, 0x25);
    static constexpr Key kConfigKey_Creds_CD_Offset    = SilabsConfigKey(kMatterFactory_KeyBase, 0x26);
    static constexpr Key kConfigKey_Creds_CD_Size      = SilabsConfigKey(kMatterFactory_KeyBase, 0x27);
    static constexpr Key kConfigKey_Provision_Request  = SilabsConfigKey(kMatterFactory_KeyBase, 0x28);
    static constexpr Key kConfigKey_Provision_Version  = SilabsConfigKey(kMatterFactory_KeyBase, 0x29);
    static constexpr Key kOtaTlvEncryption_KeyId       = SilabsConfigKey(kMatterFactory_KeyBase, 0x30);

#ifdef SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS
    // Static secure tokens (not NVM3). Accessed through the sl_token_manager_* APIs.
    static constexpr Key kSstKey_DAC = SilabsStaticSecureToken(kMatterSstId_DAC, kMatterSstMaxSize_DAC);
    static constexpr Key kSstKey_PAI = SilabsStaticSecureToken(kMatterSstId_PAI, kMatterSstMaxSize_PAI);
    static constexpr Key kSstKey_CD  = SilabsStaticSecureToken(kMatterSstId_CD, kMatterSstMaxSize_CD);
#endif // SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS

    // Matter Config Keys
    static constexpr Key kConfigKey_ServiceConfig      = SilabsConfigKey(kMatterConfig_KeyBase, 0x01);
    static constexpr Key kConfigKey_PairedAccountId    = SilabsConfigKey(kMatterConfig_KeyBase, 0x02);
    static constexpr Key kConfigKey_ServiceId          = SilabsConfigKey(kMatterConfig_KeyBase, 0x03);
    static constexpr Key kConfigKey_LastUsedEpochKeyId = SilabsConfigKey(kMatterConfig_KeyBase, 0x05);
    static constexpr Key kConfigKey_FailSafeArmed      = SilabsConfigKey(kMatterConfig_KeyBase, 0x06);
    static constexpr Key kConfigKey_GroupKey           = SilabsConfigKey(kMatterConfig_KeyBase, 0x07);
    static constexpr Key kConfigKey_RegulatoryLocation = SilabsConfigKey(kMatterConfig_KeyBase, 0x09);
    static constexpr Key kConfigKey_CountryCode        = SilabsConfigKey(kMatterConfig_KeyBase, 0x0A);
    static constexpr Key kConfigKey_WiFiSSID           = SilabsConfigKey(kMatterConfig_KeyBase, 0x0C);
    static constexpr Key kConfigKey_WiFiPSK            = SilabsConfigKey(kMatterConfig_KeyBase, 0x0D);
    static constexpr Key kConfigKey_WiFiSEC            = SilabsConfigKey(kMatterConfig_KeyBase, 0x0E);
    static constexpr Key kConfigKey_GroupKeyBase       = SilabsConfigKey(kMatterConfig_KeyBase, 0x0F);
    static constexpr Key kConfigKey_LockUser           = SilabsConfigKey(kMatterConfig_KeyBase, 0x10);
    static constexpr Key kConfigKey_Credential         = SilabsConfigKey(kMatterConfig_KeyBase, 0x11);
    static constexpr Key kConfigKey_LockUserName       = SilabsConfigKey(kMatterConfig_KeyBase, 0x12);
    static constexpr Key kConfigKey_CredentialData     = SilabsConfigKey(kMatterConfig_KeyBase, 0x13);
    static constexpr Key kConfigKey_UserCredentials    = SilabsConfigKey(kMatterConfig_KeyBase, 0x14);
    static constexpr Key kConfigKey_WeekDaySchedules   = SilabsConfigKey(kMatterConfig_KeyBase, 0x15);
    static constexpr Key kConfigKey_YearDaySchedules   = SilabsConfigKey(kMatterConfig_KeyBase, 0x16);
    static constexpr Key kConfigKey_HolidaySchedules   = SilabsConfigKey(kMatterConfig_KeyBase, 0x17);
    // UniqueId exposed in the Basic Information cluster. It is cleared on factoryreset
    // We will generate a random ID, if none was previously provided.
    static constexpr Key kConfigKey_UniqueId              = SilabsConfigKey(kMatterConfig_KeyBase, 0x18);
    static constexpr Key kConfigKey_OpKeyMap              = SilabsConfigKey(kMatterConfig_KeyBase, 0x20);
    static constexpr Key kConfigKey_BootCount             = SilabsConfigKey(kMatterConfig_KeyBase, 0x21);
    static constexpr Key kConfigKey_TotalOperationalHours = SilabsConfigKey(kMatterConfig_KeyBase, 0x22);
    static constexpr Key kConfigKey_MatterUpdateReboot    = SilabsConfigKey(kMatterConfig_KeyBase, 0x23);
    // Survives post-apply reboot so VendorOtaMatterGate can skip NotifyUpdateApplied.
    static constexpr Key kConfigKey_HttpsOtaSkipNotify    = SilabsConfigKey(kMatterConfig_KeyBase, 0x24);

    static constexpr Key kConfigKey_GroupKeyMax =
        SilabsConfigKey(kMatterConfig_KeyBase, 0x1E); // Allows 16 Group Keys to be created.

    // Matter Counter Keys
    static constexpr Key kConfigKey_LifeTimeCounter  = SilabsConfigKey(kMatterCounter_KeyBase, 0x02);
    static constexpr Key kConfigKey_MigrationCounter = SilabsConfigKey(kMatterCounter_KeyBase, 0x03);

    // Matter KVS storage Keys
    static constexpr Key kConfigKey_KvsStringKeyMap = SilabsConfigKey(kMatterKvs_KeyBase, 0x00);
    static constexpr Key kConfigKey_KvsFirstKeySlot = SilabsConfigKey(kMatterKvs_KeyBase, 0x01);
    static constexpr Key kConfigKey_KvsLastKeySlot =
        SilabsConfigKey(kMatterKvs_KeyBase + (KVS_MAX_ENTRIES >> 8), KVS_MAX_ENTRIES & UINT8_MAX);

    // Set key id limits for each group.
    static constexpr Key kMinConfigKey_MatterFactory = SilabsConfigKey(kMatterFactory_KeyBase, 0x00);
    static constexpr Key kMaxConfigKey_MatterFactory = SilabsConfigKey(kMatterFactory_KeyBase, 0x30);
    static constexpr Key kMinConfigKey_MatterConfig  = SilabsConfigKey(kMatterConfig_KeyBase, 0x00);
    static constexpr Key kMaxConfigKey_MatterConfig  = SilabsConfigKey(kMatterConfig_KeyBase, 0x24);

    // Allows 32 Counters to be created.
    static constexpr Key kMinConfigKey_MatterCounter = SilabsConfigKey(kMatterCounter_KeyBase, 0x00);
    static constexpr Key kMaxConfigKey_MatterCounter = SilabsConfigKey(kMatterCounter_KeyBase, 0x1F);

    static constexpr Key kMinConfigKey_MatterKvs = kConfigKey_KvsStringKeyMap;
    static constexpr Key kMaxConfigKey_MatterKvs = SilabsConfigKey(kMatterKvs_ExtendedRange, 0xFF);
    static_assert(kConfigKey_KvsLastKeySlot <= kMaxConfigKey_MatterKvs,
                  "Configured KVS_MAX_ENTRIES overflows the reserved KVS Key range");

    static CHIP_ERROR Init(void);
    static void DeInit(void);

    // Configuration methods used by the GenericConfigurationManagerImpl<> template.
    static CHIP_ERROR ReadConfigValue(Key key, bool & val);
    static CHIP_ERROR ReadConfigValue(Key key, uint16_t & val);
    static CHIP_ERROR ReadConfigValue(Key key, uint32_t & val);
    static CHIP_ERROR ReadConfigValue(Key key, uint64_t & val);
    static CHIP_ERROR ReadConfigValueStr(Key key, char * buf, size_t bufSize, size_t & outLen);
    static CHIP_ERROR ReadConfigValueBin(Key key, uint8_t * buf, size_t bufSize, size_t & outLen, size_t offset = 0);
    static CHIP_ERROR ReadConfigValueCounter(uint8_t counterIdx, uint32_t & val);
    static CHIP_ERROR WriteConfigValue(Key key, bool val);
    static CHIP_ERROR WriteConfigValue(Key key, uint16_t val);
    static CHIP_ERROR WriteConfigValue(Key key, uint32_t val);
    static CHIP_ERROR WriteConfigValue(Key key, uint64_t val);
    static CHIP_ERROR WriteConfigValueStr(Key key, const char * str, size_t strLen = 0);
    static CHIP_ERROR WriteConfigValueBin(Key key, const uint8_t * data, size_t dataLen);
    static CHIP_ERROR WriteConfigValueCounter(uint8_t counterIdx, uint32_t val);
    static CHIP_ERROR ClearConfigValue(Key key);
    static bool ConfigValueExists(Key key);
    static bool ConfigValueExists(Key key, size_t & dataLen);
    static CHIP_ERROR FactoryResetConfig(void);
    static bool ValidConfigKey(Key key);

    static void RunConfigUnitTest(void);
    static void RepackNvm3Flash(void);

protected:
    using ForEachRecordFunct = std::function<CHIP_ERROR(const Key & nvm3Key, const size_t & length)>;
    static CHIP_ERROR ForEachRecord(Key firstKey, Key lastKey, bool addNewRecord, ForEachRecordFunct funct);
};

} // namespace Internal
} // namespace DeviceLayer
} // namespace chip
