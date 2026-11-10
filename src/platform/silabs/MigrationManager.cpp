/*
 *    Copyright (c) 2023 Project CHIP Authors
 *    All rights reserved.
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

#include "MigrationManager.h"
#include "sl_component_catalog.h"
#include "sl_core.h"
#include <cstring>
#include <headers/ProvisionManager.h>
#include <headers/ProvisionStorage.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/ScopedMemoryBuffer.h>
#include <lib/support/Span.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/silabs/SilabsConfig.h>
#include <stdio.h>

#if defined(SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS) && (!defined(SL_MATTER_USE_FLASH_STORE) || !SL_MATTER_USE_FLASH_STORE)
#define SL_MATTER_MIGRATE_CERTS_TO_SST 1
#include <platform/silabs/platformAbstraction/SilabsPlatform.h>
#include <sl_token_manager_api.h>
#else
#define SL_MATTER_MIGRATE_CERTS_TO_SST 0
#endif

#ifdef _SILICON_LABS_32B_SERIES_3
// Symbols defined by the linker script, needed for the certificate migrations
extern uint8_t linker_static_secure_tokens_begin;
extern uint8_t linker_static_secure_tokens_end;
#endif

using namespace ::chip::DeviceLayer::Internal;
using namespace ::chip::DeviceLayer::PersistedStorage;

namespace chip {
namespace DeviceLayer {
namespace Silabs {

namespace {
typedef void (*func_ptr)();
typedef struct
{
    uint32_t migrationGroup;
    func_ptr migrationFunc;
} migrationData_t;

static migrationData_t migrationTable[] = {
    { .migrationGroup = 1, .migrationFunc = MigrateKvsMap },
    { .migrationGroup = 2, .migrationFunc = MigrateDacProvider },
    { .migrationGroup = 3, .migrationFunc = MigrateCounterConfigs },
    { .migrationGroup = 4, .migrationFunc = MigrateHardwareVersion },
    { .migrationGroup = 5, .migrationFunc = MigrateS3Certificates },
    { .migrationGroup = 6, .migrationFunc = MigrateCertificatesToStaticSecureTokens },
    // add any additional migration neccesary. migrationGroup should stay equal if done in the same commit or increment by 1 for
    // each new entry.
};

} // namespace

void MigrationManager::ApplyMigrations()
{
#ifdef SL_CATALOG_ZIGBEE_ZCL_FRAMEWORK_CORE_PRESENT
    // Suspend all other threads so they don't interfere with the migration
    // This is mostly targeted to the zigbee task that overwrites our certificates
    uint32_t threadCount = osThreadGetCount();
    chip::Platform::ScopedMemoryBuffer<osThreadId_t> threadIdTable;
    threadIdTable.Alloc(threadCount);
    if (threadIdTable.Get() != nullptr)
    {
        //  Forms a table of the active thread ids
        osThreadEnumerate(threadIdTable.Get(), threadCount);
        for (uint8_t tIdIndex = 0; tIdIndex < threadCount; tIdIndex++)
        {
            osThreadId_t tId = threadIdTable[tIdIndex];
            if (tId != osThreadGetId())
            {
                osThreadSuspend(tId);
            }
        }
    }
#endif // SL_CATALOG_ZIGBEE_ZCL_FRAMEWORK_CORE_PRESENT

    uint32_t lastMigationGroupDone = 0;
    TEMPORARY_RETURN_IGNORED SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_MigrationCounter, lastMigationGroupDone);

    uint32_t completedMigrationGroup = lastMigationGroupDone;
    for (uint32_t i = 0; i < MATTER_ARRAY_SIZE(migrationTable); i++)
    {
        if (lastMigationGroupDone < migrationTable[i].migrationGroup)
        {
            (*migrationTable[i].migrationFunc)();
            completedMigrationGroup = std::max(migrationTable[i].migrationGroup, completedMigrationGroup);
        }
    }
    TEMPORARY_RETURN_IGNORED SilabsConfig::WriteConfigValue(SilabsConfig::kConfigKey_MigrationCounter, completedMigrationGroup);

#ifdef SL_CATALOG_ZIGBEE_ZCL_FRAMEWORK_CORE_PRESENT
    // resume all threads
    if (threadIdTable.Get() != nullptr)
    {
        for (uint8_t tIdIndex = 0; tIdIndex < threadCount; tIdIndex++)
        {
            osThreadId_t tId = threadIdTable[tIdIndex];
            if (tId != osThreadGetId())
            {
                osThreadResume(tId);
            }
        }
    }
#endif // SL_CATALOG_ZIGBEE_ZCL_FRAMEWORK_CORE_PRESENT
}

void MigrationManager::MigrateUint16(uint32_t old_key, uint32_t new_key)
{
    uint16_t value = 0;
    if (CHIP_NO_ERROR == SilabsConfig::ReadConfigValue(old_key, value))
    {
        if (CHIP_NO_ERROR == SilabsConfig::WriteConfigValue(new_key, value))
        {
            // Free memory of old key location
            TEMPORARY_RETURN_IGNORED SilabsConfig::ClearConfigValue(old_key);
        }
    }
}

void MigrationManager::MigrateUint32(uint32_t old_key, uint32_t new_key)
{
    uint32_t value = 0;
    if (CHIP_NO_ERROR == SilabsConfig::ReadConfigValue(old_key, value))
    {
        if (CHIP_NO_ERROR == SilabsConfig::WriteConfigValue(new_key, value))
        {
            // Free memory of old key location
            TEMPORARY_RETURN_IGNORED SilabsConfig::ClearConfigValue(old_key);
        }
    }
}

void MigrateCounterConfigs(void)
{
    constexpr uint32_t kOldConfigKey_BootCount             = SilabsConfigKey(SilabsConfig::kMatterCounter_KeyBase, 0x00);
    constexpr uint32_t kOldConfigKey_TotalOperationalHours = SilabsConfigKey(SilabsConfig::kMatterCounter_KeyBase, 0x01);

    MigrationManager::MigrateUint32(kOldConfigKey_BootCount, SilabsConfig::kConfigKey_BootCount);
    MigrationManager::MigrateUint32(kOldConfigKey_TotalOperationalHours, SilabsConfig::kConfigKey_TotalOperationalHours);
}

MigrationManager & MigrationManager::GetMigrationInstance()
{
    static MigrationManager sMigrationManager;
    return sMigrationManager;
}

void MigrateDacProvider(void)
{
    constexpr uint32_t kOldKey_Creds_KeyId      = SilabsConfigKey(SilabsConfig::kMatterConfig_KeyBase, 0x21);
    constexpr uint32_t kOldKey_Creds_Base_Addr  = SilabsConfigKey(SilabsConfig::kMatterConfig_KeyBase, 0x22);
    constexpr uint32_t kOldKey_Creds_DAC_Offset = SilabsConfigKey(SilabsConfig::kMatterConfig_KeyBase, 0x23);
    constexpr uint32_t kOldKey_Creds_DAC_Size   = SilabsConfigKey(SilabsConfig::kMatterConfig_KeyBase, 0x24);
    constexpr uint32_t kOldKey_Creds_PAI_Size   = SilabsConfigKey(SilabsConfig::kMatterConfig_KeyBase, 0x26);
    constexpr uint32_t kOldKey_Creds_PAI_Offset = SilabsConfigKey(SilabsConfig::kMatterConfig_KeyBase, 0x25);
    constexpr uint32_t kOldKey_Creds_CD_Offset  = SilabsConfigKey(SilabsConfig::kMatterConfig_KeyBase, 0x27);
    constexpr uint32_t kOldKey_Creds_CD_Size    = SilabsConfigKey(SilabsConfig::kMatterConfig_KeyBase, 0x28);

    MigrationManager::MigrateUint32(kOldKey_Creds_KeyId, SilabsConfig::kConfigKey_Creds_KeyId);
    MigrationManager::MigrateUint32(kOldKey_Creds_Base_Addr, SilabsConfig::kConfigKey_Creds_Base_Addr);
    MigrationManager::MigrateUint32(kOldKey_Creds_DAC_Offset, SilabsConfig::kConfigKey_Creds_DAC_Offset);
    MigrationManager::MigrateUint32(kOldKey_Creds_DAC_Size, SilabsConfig::kConfigKey_Creds_DAC_Size);
    MigrationManager::MigrateUint32(kOldKey_Creds_PAI_Offset, SilabsConfig::kConfigKey_Creds_PAI_Offset);
    MigrationManager::MigrateUint32(kOldKey_Creds_PAI_Size, SilabsConfig::kConfigKey_Creds_PAI_Size);
    MigrationManager::MigrateUint32(kOldKey_Creds_CD_Offset, SilabsConfig::kConfigKey_Creds_CD_Offset);
    MigrationManager::MigrateUint32(kOldKey_Creds_CD_Size, SilabsConfig::kConfigKey_Creds_CD_Size);
}

void MigrateHardwareVersion(void)
{
    constexpr uint32_t kOldKey_HardwareVersion = SilabsConfigKey(SilabsConfig::kMatterConfig_KeyBase, 0x08);
    MigrationManager::MigrateUint16(kOldKey_HardwareVersion, SilabsConfig::kConfigKey_HardwareVersion);
}

void MigrateS3Certificates()
{
    // With static secure tokens the certificates leave the raw page scheme altogether (will be handled in group 6).
#if defined(_SILICON_LABS_32B_SERIES_3) && !defined(SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS)
    uint32_t tokenStartAddr = reinterpret_cast<uint32_t>(&linker_static_secure_tokens_begin);
    uint32_t secondPageAddr = tokenStartAddr + FLASH_PAGE_SIZE;
    uint32_t credsBaseAddr  = 0;

    // when credentials have been provided and stored in the first page of the static token location, we temporarely migrate them to
    // the second page.
    if (CHIP_NO_ERROR == SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_Creds_Base_Addr, credsBaseAddr) &&
        (credsBaseAddr >= tokenStartAddr && credsBaseAddr < secondPageAddr))
    {
#if SL_MATTER_PROVISION_CHANNEL_ENABLED
        uint32_t cdSize                = 0;
        uint32_t dacSize               = 0;
        uint32_t paiSize               = 0;
        Provision::Manager & provision = Provision::Manager::GetInstance();

        // Read the size of each credential type to determine the buffer size needed
        VerifyOrReturn(SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_Creds_CD_Size, cdSize) == CHIP_NO_ERROR);
        VerifyOrReturn(SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_Creds_DAC_Size, dacSize) == CHIP_NO_ERROR);
        VerifyOrReturn(SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_Creds_PAI_Size, paiSize) == CHIP_NO_ERROR);

        // Depending on existing configuration, certifications could be overlapping from the first page to the second page.
        // To mitigate any risk, we want to read all buffer before starting to move any of them.
        // allocate buffers for each certificate.
        // ScopedMemoryBuffer will automatically free the memory when it goes out of scope.
        Platform::ScopedMemoryBuffer<uint8_t> dacBuffer;
        Platform::ScopedMemoryBuffer<uint8_t> paiBuffer;
        Platform::ScopedMemoryBuffer<uint8_t> cdBuffer;
        dacBuffer.Alloc(dacSize);
        VerifyOrReturn(dacBuffer.Get() != nullptr);
        paiBuffer.Alloc(paiSize);
        VerifyOrReturn(paiBuffer.Get() != nullptr);
        cdBuffer.Alloc(cdSize);
        VerifyOrReturn(cdBuffer.Get() != nullptr);

        MutableByteSpan dacBufferSpan(dacBuffer.Get(), dacSize);
        MutableByteSpan paiBufferSpan(paiBuffer.Get(), paiSize);
        MutableByteSpan cdBufferSpan(cdBuffer.Get(), cdSize);

        ReturnOnFailure(provision.Init());
        // Read all certificates at the current location
        VerifyOrReturn(provision.GetStorage().GetDeviceAttestationCert(dacBufferSpan) == CHIP_NO_ERROR);
        VerifyOrReturn(provision.GetStorage().GetProductAttestationIntermediateCert(paiBufferSpan) == CHIP_NO_ERROR);
        VerifyOrReturn(provision.GetStorage().GetCertificationDeclaration(cdBufferSpan) == CHIP_NO_ERROR);

        ReturnOnFailure(provision.GetStorage().Initialize(0, 0));
        ReturnOnFailure(provision.GetStorage().SetCredentialsBaseAddress(secondPageAddr));
        // Write all certs back to the second page
        // The first set/write, after an Initialize, erases the new page. We don't need to do it explicitly.
        ReturnOnFailure(provision.GetStorage().SetDeviceAttestationCert(dacBufferSpan));
        ReturnOnFailure(provision.GetStorage().SetProductAttestationIntermediateCert(paiBufferSpan));
        ReturnOnFailure(provision.GetStorage().SetCertificationDeclaration(cdBufferSpan));
#else
        ChipLogError(NotSpecified, "Your credentials must be migrated. This requires this require the ProvisionStorageWriter");
        chipAbort();
#endif // SL_MATTER_PROVISION_CHANNEL_ENABLED
    }
#endif //_SILICON_LABS_32B_SERIES_3 && !SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS
}

#if SL_MATTER_MIGRATE_CERTS_TO_SST
namespace {

CHIP_ERROR ReadLegacyCertificate(uint32_t baseAddr, SilabsConfig::Key offsetKey, SilabsConfig::Key sizeKey, uint32_t maxLength,
                                 Platform::ScopedMemoryBuffer<uint8_t> & buffer, size_t & size)
{
    uint32_t offset = 0;
    uint32_t length = 0;
    ReturnErrorOnFailure(SilabsConfig::ReadConfigValue(offsetKey, offset));
    ReturnErrorOnFailure(SilabsConfig::ReadConfigValue(sizeKey, length));
    VerifyOrReturnError(length > 0 && length <= maxLength, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(buffer.Alloc(length).Get() != nullptr, CHIP_ERROR_NO_MEMORY);
    memcpy(buffer.Get(), reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(baseAddr + offset)), length);
    size = length;
    return CHIP_NO_ERROR;
}

/**
 * Stores the certificate in a static secure token and reads it back for comparison.
 * The token is padded to a word boundary (flash write constraint), the real lenght is stored in the nvm3 key.
 */
CHIP_ERROR StoreStaticSecureToken(uint32_t token, const ByteSpan & value)
{
    constexpr size_t kPadding = 3u; // 3 bytes of padding to make the size a multiple of 4
    const size_t paddedSize   = (value.size() + kPadding) & ~static_cast<size_t>(kPadding);
    VerifyOrReturnError(paddedSize <= SilabsStaticSecureTokenSize(token), CHIP_ERROR_INVALID_ARGUMENT);

    Platform::ScopedMemoryBuffer<uint8_t> padded;
    VerifyOrReturnError(padded.Alloc(paddedSize).Get() != nullptr, CHIP_ERROR_NO_MEMORY);
    memcpy(padded.Get(), value.data(), value.size());
    memset(padded.Get() + value.size(), 0xFF, paddedSize - value.size());

    // An already existing token is accepted: the read back decides whether it holds this certificate.
    sl_status_t status = sl_token_manager_set_data(token, padded.Get(), static_cast<uint32_t>(paddedSize));
    if (status != SL_STATUS_OK && status != SL_STATUS_ALREADY_EXISTS)
    {
        ChipLogError(DeviceLayer, "Static secure token 0x%08x write failed: 0x%04lx", (unsigned) token, (unsigned long) status);
        return CHIP_ERROR_INTERNAL;
    }

    Platform::ScopedMemoryBuffer<uint8_t> readBack;
    VerifyOrReturnError(readBack.Alloc(value.size()).Get() != nullptr, CHIP_ERROR_NO_MEMORY);
    status = sl_token_manager_get_data(token, readBack.Get(), static_cast<uint32_t>(value.size()));
    if (status != SL_STATUS_OK)
    {
        ChipLogError(DeviceLayer, "Static secure token 0x%08x read failed: 0x%04lx", (unsigned) token, (unsigned long) status);
        return CHIP_ERROR_INTERNAL;
    }
    VerifyOrReturnError(memcmp(readBack.Get(), value.data(), value.size()) == 0, CHIP_ERROR_INTEGRITY_CHECK_FAILED);
    return CHIP_NO_ERROR;
}

#ifdef _SILICON_LABS_32B_SERIES_2
// Series 2 static secure tokens are raw bytes at an offset of the last flash page, writable only when erased.
constexpr uint32_t kStaticSecurePageAddr = FLASH_BASE + FLASH_SIZE - FLASH_PAGE_SIZE;

bool IsStaticSecureSlotErased(uint32_t token)
{
    const uint8_t * slot = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(kStaticSecurePageAddr + (token & 0x0FFF)));
    for (uint32_t i = 0; i < SilabsStaticSecureTokenSize(token); i++)
    {
        if (slot[i] != 0xFF)
        {
            return false;
        }
    }
    return true;
}
#endif // _SILICON_LABS_32B_SERIES_2

} // namespace
#endif // SL_MATTER_MIGRATE_CERTS_TO_SST

void MigrateCertificatesToStaticSecureTokens()
{
#if SL_MATTER_MIGRATE_CERTS_TO_SST
    // Devices provisioned before static secure tokens keep the certificates in a raw flash page located by
    // kConfigKey_Creds_Base_Addr and the NVM3 offset/size keys. The offset keys are removed once the tokens are
    // verified, which makes this migration a no-op on devices provisioned with static secure tokens.
    VerifyOrReturn(SilabsConfig::ConfigValueExists(SilabsConfig::kConfigKey_Creds_DAC_Offset));
    uint32_t credsBaseAddr = 0;
    VerifyOrReturn(SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_Creds_Base_Addr, credsBaseAddr) == CHIP_NO_ERROR);

    // Read everything before touching the flash: on Series 2 the token slots overlap the legacy layout.
    Platform::ScopedMemoryBuffer<uint8_t> dac;
    Platform::ScopedMemoryBuffer<uint8_t> pai;
    Platform::ScopedMemoryBuffer<uint8_t> cd;
    size_t dacSize = 0;
    size_t paiSize = 0;
    size_t cdSize  = 0;
    VerifyOrReturn(ReadLegacyCertificate(credsBaseAddr, SilabsConfig::kConfigKey_Creds_DAC_Offset,
                                         SilabsConfig::kConfigKey_Creds_DAC_Size, kMatterSstMaxSize_DAC, dac,
                                         dacSize) == CHIP_NO_ERROR);
    VerifyOrReturn(ReadLegacyCertificate(credsBaseAddr, SilabsConfig::kConfigKey_Creds_PAI_Offset,
                                         SilabsConfig::kConfigKey_Creds_PAI_Size, kMatterSstMaxSize_PAI, pai,
                                         paiSize) == CHIP_NO_ERROR);
    VerifyOrReturn(ReadLegacyCertificate(credsBaseAddr, SilabsConfig::kConfigKey_Creds_CD_Offset,
                                         SilabsConfig::kConfigKey_Creds_CD_Size, kMatterSstMaxSize_CD, cd,
                                         cdSize) == CHIP_NO_ERROR);
    ByteSpan dacSpan(dac.Get(), dacSize);
    ByteSpan paiSpan(pai.Get(), paiSize);
    ByteSpan cdSpan(cd.Get(), cdSize);

    ChipLogProgress(DeviceLayer, "Migrating certificates to static secure tokens, legacy page:0x%08x", (unsigned) credsBaseAddr);

#ifdef _SILICON_LABS_32B_SERIES_3
    // The legacy page lies inside the static secure region: the token manager cannot append KLV objects over
    // raw data, erase that page first. The remainder of the region (existing KLV chain) is left untouched.
    uint32_t tokenStartAddr = reinterpret_cast<uint32_t>(&linker_static_secure_tokens_begin);
    uint32_t tokenEndAddr   = reinterpret_cast<uint32_t>(&linker_static_secure_tokens_end);
    if (credsBaseAddr >= tokenStartAddr && credsBaseAddr < tokenEndAddr)
    {
        VerifyOrReturn(GetPlatform().FlashInit() == CHIP_NO_ERROR);
        VerifyOrReturn(GetPlatform().FlashErasePage(credsBaseAddr) == CHIP_NO_ERROR);
    }
#else
    // Series 2: the token slots overlap the legacy layout of the same page and can only be written once erased.
    // The legacy provisioning erased this page when it wrote the certificates, which are now held in RAM.
    if (!IsStaticSecureSlotErased(SilabsConfig::kSstKey_DAC) || !IsStaticSecureSlotErased(SilabsConfig::kSstKey_PAI) ||
        !IsStaticSecureSlotErased(SilabsConfig::kSstKey_CD))
    {
        VerifyOrReturn(GetPlatform().FlashInit() == CHIP_NO_ERROR);
        VerifyOrReturn(GetPlatform().FlashErasePage(kStaticSecurePageAddr) == CHIP_NO_ERROR);
    }
#endif // _SILICON_LABS_32B_SERIES_3

    if (StoreStaticSecureToken(SilabsConfig::kSstKey_DAC, dacSpan) != CHIP_NO_ERROR ||
        StoreStaticSecureToken(SilabsConfig::kSstKey_PAI, paiSpan) != CHIP_NO_ERROR ||
        StoreStaticSecureToken(SilabsConfig::kSstKey_CD, cdSpan) != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Static secure token migration failed, legacy certificate keys kept");
        return;
    }

    // The offset keys designate the legacy location, the reader now uses the tokens. The size keys are kept: they
    // hold the certificate lengths for the tokens. kConfigKey_Creds_Base_Addr is kept for diagnostics.
    constexpr SilabsConfig::Key kLegacyOffsetKeys[] = {
        SilabsConfig::kConfigKey_Creds_DAC_Offset,
        SilabsConfig::kConfigKey_Creds_PAI_Offset,
        SilabsConfig::kConfigKey_Creds_CD_Offset,
    };
    for (SilabsConfig::Key key : kLegacyOffsetKeys)
    {
        RETURN_SAFELY_IGNORED SilabsConfig::ClearConfigValue(key);
    }
    ChipLogProgress(DeviceLayer, "Certificates migrated to static secure tokens");
#endif // SL_MATTER_MIGRATE_CERTS_TO_SST
}

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
