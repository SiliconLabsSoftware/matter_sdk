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
#include <headers/ProvisionManager.h>
#include <headers/ProvisionStorage.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/silabs/SilabsConfig.h>
#include <stdio.h>

extern uint8_t linker_static_secure_tokens_begin; // Symbol defined by the linker script needed for MigrateS3Certificates

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
    // add any additional migration neccesary. migrationGroup should stay equal if done in the same commit or increment by 1 for
    // each new entry.
};

} // namespace

void MigrationManager::ApplyMigrations()
{
#ifdef SL_CATALOG_ZIGBEE_ZCL_FRAMEWORK_CORE_PRESENT
    // Suspend all other threads so they don't interfere with the migration
    // This is mostly targeted to the zigbee task that overwrites our certificates
    uint32_t threadCount         = osThreadGetCount();
    osThreadId_t * threadIdTable = new osThreadId_t[threadCount];
    //  Forms a table of the active thread ids
    osThreadEnumerate(threadIdTable, threadCount);
    for (uint8_t tIdIndex = 0; tIdIndex < threadCount; tIdIndex++)
    {
        osThreadId_t tId = threadIdTable[tIdIndex];
        if (tId != osThreadGetId())
        {
            osThreadSuspend(tId);
        }
    }
#endif // SL_CATALOG_ZIGBEE_ZCL_FRAMEWORK_CORE_PRESENT

    uint32_t lastMigationGroupDone = 0;
    SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_MigrationCounter, lastMigationGroupDone);

    uint32_t completedMigrationGroup = lastMigationGroupDone;
    for (uint32_t i = 0; i < MATTER_ARRAY_SIZE(migrationTable); i++)
    {
        if (lastMigationGroupDone < migrationTable[i].migrationGroup)
        {
            (*migrationTable[i].migrationFunc)();
            completedMigrationGroup = std::max(migrationTable[i].migrationGroup, completedMigrationGroup);
        }
    }
    SilabsConfig::WriteConfigValue(SilabsConfig::kConfigKey_MigrationCounter, completedMigrationGroup);

#ifdef SL_CATALOG_ZIGBEE_ZCL_FRAMEWORK_CORE_PRESENT
    // resume all threads
    for (uint8_t tIdIndex = 0; tIdIndex < threadCount; tIdIndex++)
    {
        osThreadId_t tId = threadIdTable[tIdIndex];
        if (tId != osThreadGetId())
        {
            osThreadResume(tId);
        }
    }
    delete[] threadIdTable;
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
            SilabsConfig::ClearConfigValue(old_key);
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
            SilabsConfig::ClearConfigValue(old_key);
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
#ifdef _SILICON_LABS_32B_SERIES_3
    uint32_t tokenStartAddr = reinterpret_cast<uint32_t>(&linker_static_secure_tokens_begin);
    uint32_t secondPageAddr = tokenStartAddr + FLASH_PAGE_SIZE;
    uint32_t credsBaseAddr  = 0;

    // when credentials have been provided and stored in the first page of the static token location, we temporarely migrate them to
    // the second page.
    if (CHIP_NO_ERROR == SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_Creds_Base_Addr, credsBaseAddr) &&
        (credsBaseAddr >= tokenStartAddr && credsBaseAddr < secondPageAddr))
    {
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
        uint8_t * dacBuffer = new uint8_t[dacSize];
        uint8_t * paiBuffer = new uint8_t[paiSize];
        uint8_t * cdBuffer  = new uint8_t[cdSize];
        VerifyOrReturn(dacBuffer != nullptr && paiBuffer != nullptr && cdBuffer != nullptr);

        MutableByteSpan dacBufferSpan(dacBuffer, dacSize);
        MutableByteSpan paiBufferSpan(paiBuffer, paiSize);
        MutableByteSpan cdBufferSpan(cdBuffer, cdSize);

        enum migrationStep
        {
            eReadDac,
            eReadPai,
            eReadCd,
            eWriteCertificates,
            eCleanup,
        };

        migrationStep steps = eReadDac;
        provision.Init();

        while (steps != eCleanup)
        {
            switch (steps)
            {
            case eReadDac:
                // read DAC at its current location.
                steps = (provision.GetStorage().GetDeviceAttestationCert(dacBufferSpan) == CHIP_NO_ERROR) ? eReadPai : eCleanup;
                break;

            case eReadPai:
                // read PAI at its current location.
                steps = (provision.GetStorage().GetProductAttestationIntermediateCert(paiBufferSpan) == CHIP_NO_ERROR) ? eReadCd
                                                                                                                       : eCleanup;
                break;

            case eReadCd:
                // read CD at its current location.
                steps = (provision.GetStorage().GetCertificationDeclaration(cdBufferSpan) == CHIP_NO_ERROR) ? eWriteCertificates
                                                                                                            : eCleanup;
                break;

            case eWriteCertificates:
                // Step to write the certificates to their new location
                provision.GetStorage().Initialize(0, 0);
                provision.GetStorage().SetCredentialsBaseAddress(secondPageAddr);
                // Write all certs back to the second page
                // The first set/write, after an Initialize, erases the new page. We don't need to do it explicitly.
                provision.GetStorage().SetDeviceAttestationCert(dacBufferSpan);
                provision.GetStorage().SetProductAttestationIntermediateCert(paiBufferSpan);
                provision.GetStorage().SetCertificationDeclaration(cdBufferSpan);
                steps = eCleanup;
                break;

            case eCleanup:
                // We shouldn't get here but all steps are done. We do the cleanup outside of the while.
                break;
            }
        }
        // Free allocated memory. If allocation step failed we are still safe do to so.
        delete[] dacBuffer;
        delete[] paiBuffer;
        delete[] cdBuffer;
    }
#endif //_SILICON_LABS_32B_SERIES_3
}

} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
