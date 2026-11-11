/*
 *    Copyright (c) 2024-2026 Project CHIP Authors
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
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceConfig.h>
#include <platform/silabs/SilabsConfig.h>
#include <platform/silabs/platformAbstraction/SilabsPlatform.h>
#include <provision/ProvisionStorageWriter.h>
#include <silabs_creds.h>

#include <cstring>

#if defined(SL_MATTER_ENABLE_OTA_ENCRYPTION) && SL_MATTER_ENABLE_OTA_ENCRYPTION
#include <platform/silabs/multi-ota/OtaTlvEncryptionKey.h>
#endif

#ifdef SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS
#include <lib/support/ScopedMemoryBuffer.h>
#include <sl_token_manager_api.h>
#endif // SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS

using SilabsConfig = chip::DeviceLayer::Internal::SilabsConfig;

#if defined(SL_PROVISION_GENERATOR) && SL_PROVISION_GENERATOR
extern void setNvm3End(uint32_t addr);
#endif // SL_PROVISION_GENERATOR
extern uint8_t linker_nvm_end[];
#ifdef _SILICON_LABS_32B_SERIES_3
extern uint8_t linker_static_secure_tokens_begin;
#endif

namespace chip {
namespace DeviceLayer {
namespace Silabs {
namespace Provision {

ProvisionStorageWriter & ProvisionStorageWriter::GetInstance()
{
    static ProvisionStorageWriter instance;
    return instance;
}

namespace {

#if !defined(SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS) || defined(_SILICON_LABS_32B_SERIES_2)
CHIP_ERROR ErasePage(uint32_t addr)
{
    return chip::DeviceLayer::Silabs::GetPlatform().FlashErasePage(addr);
}
#endif

size_t RoundNearest(size_t n, size_t multiple)
{
    return (n % multiple) > 0 ? n + (multiple - n % multiple) : n;
}

#ifndef SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS
size_t sCredentialsOffset = 0;

/**
 * Writes "size" bytes to the flash page. The data is padded with 0xff
 * up to the nearest 32-bit boundary.
 */
CHIP_ERROR WritePage(uint32_t addr, const uint8_t * data, size_t size)
{
    // The flash driver fails if the size is not a multiple of 4 (32-bits)
    size_t size_32 = RoundNearest(size, 4);
    if (size_32 == size)
    {
        return chip::DeviceLayer::Silabs::GetPlatform().FlashWritePage(addr, data, size);
    }

    // Create a temporary buffer, and pad it with "0xff"
    uint8_t * p = static_cast<uint8_t *>(chip::Platform::MemoryAlloc(size_32));
    VerifyOrReturnError(p != nullptr, CHIP_ERROR_INTERNAL);
    memcpy(p, data, size);
    memset(p + size, 0xff, size_32 - size);
    CHIP_ERROR err = chip::DeviceLayer::Silabs::GetPlatform().FlashWritePage(addr, p, size_32);
    chip::Platform::MemoryFree(p);
    return err;
}
#endif // !SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS

#ifdef SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS

#if defined(_SILICON_LABS_32B_SERIES_2)
// Set once the Matter slots of the static secure page have been erased for the current provisioning session.
bool sSstSlotsPrepared = false;

// Only the first 4 KB of the Series 2 static secure page are addressable (12-bit offset in the token identifier).
constexpr uint32_t kSstAddressableSize = 0x1000;
constexpr uint32_t kMatterSstTokens[]  = { SilabsConfig::kSstKey_DAC, SilabsConfig::kSstKey_PAI, SilabsConfig::kSstKey_CD };

bool IsErased(const uint8_t * data, size_t size)
{
    // Matter SST slots are word aligned, so check each 32-bit word for non-FF.
    const uint32_t * words = reinterpret_cast<const uint32_t *>(data);
    for (size_t i = 0; i < size / sizeof(uint32_t); i++)
    {
        if (words[i] != 0xFFFFFFFFu)
        {
            return false;
        }
    }
    return true;
}

CHIP_ERROR StaticSecurePageAddress(ProvisionStorageWriter & store, uint32_t & addr)
{
#if defined(SL_PROVISION_GENERATOR) && SL_PROVISION_GENERATOR
    // The generator firmware serves several flash sizes: the page was detected at runtime (see Initialize).
    return store.GetCredentialsBaseAddress(addr);
#else
    // Same page as the token manager's LOCKBITS_BASE.
    (void) store;
    addr = FLASH_BASE + FLASH_SIZE - FLASH_PAGE_SIZE;
    return CHIP_NO_ERROR;
#endif
}

/**
 * Makes sure the Matter slots of the static secure page are erased. When they are not (legacy certificates
 * or a previous provisioning), the addressable part of the page is copied, the page is erased and the copy
 * is written back without the Matter slots, so that the tokens owned by other stacks survive.
 * Bytes above kSstAddressableSize cannot hold tokens and are not preserved.
 */
CHIP_ERROR EraseMatterStaticSecureSlots(uint32_t page_addr)
{
    const uint8_t * page = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(page_addr));
    bool erased          = true;
    for (uint32_t token : kMatterSstTokens)
    {
        erased = erased && IsErased(page + (token & 0x0FFF), Internal::SilabsStaticSecureTokenSize(token));
    }
    VerifyOrReturnError(!erased, CHIP_NO_ERROR);

    Platform::ScopedMemoryBuffer<uint8_t> copy;
    VerifyOrReturnError(copy.Alloc(kSstAddressableSize).Get() != nullptr, CHIP_ERROR_NO_MEMORY);
    memcpy(copy.Get(), page, kSstAddressableSize);
    for (uint32_t token : kMatterSstTokens)
    {
        memset(copy.Get() + (token & 0x0FFF), 0xFF, Internal::SilabsStaticSecureTokenSize(token));
    }

    ReturnErrorOnFailure(chip::DeviceLayer::Silabs::GetPlatform().FlashInit());
    ReturnErrorOnFailure(ErasePage(page_addr));
    return chip::DeviceLayer::Silabs::GetPlatform().FlashWritePage(page_addr, copy.Get(), kSstAddressableSize);
}
#endif // _SILICON_LABS_32B_SERIES_2

/**
 * Writes the certificate bytes in the static secure token (padded to a word boundary).
 * Static secure tokens are write-once: rewriting an existing token requires upgradable tokens (not supported yet).
 */
CHIP_ERROR WriteStaticSecureTokenData(ProvisionStorageWriter & store, uint32_t token, const ByteSpan & value)
{
    const uint32_t slotSize = Internal::SilabsStaticSecureTokenSize(token);
    VerifyOrReturnError(!value.empty() && value.size() <= slotSize, CHIP_ERROR_INVALID_ARGUMENT);

    // Flash writes need a word aligned length; pad with the erased value. The NVM3 size key holds the real length.
    const size_t size = RoundNearest(value.size(), 4);
    VerifyOrReturnError(size <= slotSize, CHIP_ERROR_INVALID_ARGUMENT);
    Platform::ScopedMemoryBuffer<uint8_t> buffer;
    VerifyOrReturnError(buffer.Alloc(size).Get() != nullptr, CHIP_ERROR_NO_MEMORY);
    memcpy(buffer.Get(), value.data(), value.size());
    memset(buffer.Get() + value.size(), 0xFF, size - value.size());

#if defined(_SILICON_LABS_32B_SERIES_2)
    uint32_t page_addr = 0;
    ReturnErrorOnFailure(StaticSecurePageAddress(store, page_addr));
    if (!sSstSlotsPrepared)
    {
        ReturnErrorOnFailure(EraseMatterStaticSecureSlots(page_addr));
        sSstSlotsPrepared = true;
    }
#else
    (void) store;
#endif

#if defined(SL_PROVISION_GENERATOR) && SL_PROVISION_GENERATOR && defined(_SILICON_LABS_32B_SERIES_2)
    // The token manager resolves the static secure page at compile time, which does not match devices with a
    // larger flash than the one the generator firmware was built for. Write the token layout at the runtime page.
    const uint32_t addr = page_addr + (token & 0x0FFF);
    return chip::DeviceLayer::Silabs::GetPlatform().FlashWritePage(addr, buffer.Get(), size);
#else
    sl_status_t status = sl_token_manager_set_data(token, buffer.Get(), static_cast<uint32_t>(size));
    if (SL_STATUS_ALREADY_EXISTS == status)
    {
        ChipLogError(DeviceLayer, "Static secure token 0x%08x already provisioned, rewriting requires upgradable tokens",
                     (unsigned) token);
        return CHIP_ERROR_INCORRECT_STATE;
    }
    if (SL_STATUS_FULL == status || SL_STATUS_NO_MORE_RESOURCE == status)
    {
        ChipLogError(DeviceLayer, "Static secure token storage is full");
        return CHIP_ERROR_NO_MEMORY;
    }
    if (status != SL_STATUS_OK)
    {
        // sl_status_t is not a CHIP_ERROR encoding, so the numeric code is logged and a generic error is returned.
        ChipLogError(DeviceLayer, "Static secure token 0x%08x write failed: 0x%04lx", (unsigned) token, (unsigned long) status);
        return CHIP_ERROR_INTERNAL;
    }
    return CHIP_NO_ERROR;
#endif // SL_PROVISION_GENERATOR && _SILICON_LABS_32B_SERIES_2
}

/**
 * Stores a certificate as a static secure token and records its length in NVM3.
 * The legacy offset key is removed last: it designates the raw flash location the reader must use.
 */
CHIP_ERROR WriteStaticSecureToken(ProvisionStorageWriter & store, uint32_t token, SilabsConfig::Key offset_key,
                                  SilabsConfig::Key size_key, const ByteSpan & value)
{
    ReturnErrorOnFailure(WriteStaticSecureTokenData(store, token, value));
    ReturnErrorOnFailure(SilabsConfig::WriteConfigValue(size_key, static_cast<uint32_t>(value.size())));
    RETURN_SAFELY_IGNORED SilabsConfig::ClearConfigValue(offset_key);
    return CHIP_NO_ERROR;
}

#else // SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS

CHIP_ERROR WriteFile(ProvisionStorageWriter & store, size_t & credentialsOffset, SilabsConfig::Key offset_key,
                     SilabsConfig::Key size_key, const ByteSpan & value)
{
    uint32_t base_addr = 0;
    ReturnErrorOnFailure(store.GetCredentialsBaseAddress(base_addr));
    if (0 == credentialsOffset)
    {
        ReturnErrorOnFailure(ErasePage(base_addr));
    }

    ReturnErrorOnFailure(WritePage(base_addr + credentialsOffset, value.data(), value.size()));

    // Store file offset
    ReturnErrorOnFailure(SilabsConfig::WriteConfigValue(offset_key, (uint32_t) credentialsOffset));
    // Store file size
    ReturnErrorOnFailure(SilabsConfig::WriteConfigValue(size_key, (uint32_t) value.size()));
    // Calculate offset for the next file
    credentialsOffset = RoundNearest(credentialsOffset + value.size(), 64);
    return CHIP_NO_ERROR;
}

#endif // SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS

} // namespace

//
// DeviceInstanceInfoProvider
//

CHIP_ERROR ProvisionStorageWriter::SetSerialNumber(const char * value, size_t len)
{
    return SilabsConfig::WriteConfigValueStr(SilabsConfig::kConfigKey_SerialNum, value, len);
}

CHIP_ERROR ProvisionStorageWriter::SetVendorId(uint16_t value)
{
    return SilabsConfig::WriteConfigValue(SilabsConfig::kConfigKey_VendorId, value);
}

CHIP_ERROR ProvisionStorageWriter::SetVendorName(const char * value, size_t len)
{
    return SilabsConfig::WriteConfigValueStr(SilabsConfig::kConfigKey_VendorName, value, len);
}

CHIP_ERROR ProvisionStorageWriter::SetProductId(uint16_t value)
{
    return SilabsConfig::WriteConfigValue(SilabsConfig::kConfigKey_ProductId, value);
}

CHIP_ERROR ProvisionStorageWriter::SetProductName(const char * value, size_t len)
{
    return SilabsConfig::WriteConfigValueStr(SilabsConfig::kConfigKey_ProductName, value, len);
}

CHIP_ERROR ProvisionStorageWriter::SetProductLabel(const char * value, size_t len)
{
    return SilabsConfig::WriteConfigValueStr(SilabsConfig::kConfigKey_ProductLabel, value, len);
}

CHIP_ERROR ProvisionStorageWriter::SetProductURL(const char * value, size_t len)
{
    return SilabsConfig::WriteConfigValueStr(SilabsConfig::kConfigKey_ProductURL, value, len);
}

CHIP_ERROR ProvisionStorageWriter::SetPartNumber(const char * value, size_t len)
{
    return SilabsConfig::WriteConfigValueStr(SilabsConfig::kConfigKey_PartNumber, value, len);
}

CHIP_ERROR ProvisionStorageWriter::SetHardwareVersion(uint16_t value)
{
    return SilabsConfig::WriteConfigValue(SilabsConfig::kConfigKey_HardwareVersion, value);
}

CHIP_ERROR ProvisionStorageWriter::SetHardwareVersionString(const char * value, size_t len)
{
    return SilabsConfig::WriteConfigValueStr(SilabsConfig::kConfigKey_HardwareVersionString, value, len);
}

CHIP_ERROR ProvisionStorageWriter::SetManufacturingDate(const char * value, size_t len)
{
    return SilabsConfig::WriteConfigValueStr(SilabsConfig::kConfigKey_ManufacturingDate, value, len);
}

CHIP_ERROR ProvisionStorageWriter::SetPersistentUniqueId(const uint8_t * value, size_t size)
{
    return SilabsConfig::WriteConfigValueBin(SilabsConfig::kConfigKey_PersistentUniqueId, value, size);
}

CHIP_ERROR ProvisionStorageWriter::SetSetupDiscriminator(uint16_t value)
{
    return SilabsConfig::WriteConfigValue(SilabsConfig::kConfigKey_SetupDiscriminator, value);
}

CHIP_ERROR ProvisionStorageWriter::SetSpake2pIterationCount(uint32_t value)
{
    return SilabsConfig::WriteConfigValue(SilabsConfig::kConfigKey_Spake2pIterationCount, value);
}

CHIP_ERROR ProvisionStorageWriter::SetSpake2pSalt(const char * value, size_t size)
{
    return SilabsConfig::WriteConfigValueStr(SilabsConfig::kConfigKey_Spake2pSalt, value, size);
}

CHIP_ERROR ProvisionStorageWriter::SetSpake2pVerifier(const char * value, size_t size)
{
    return SilabsConfig::WriteConfigValueStr(SilabsConfig::kConfigKey_Spake2pVerifier, value, size);
}

CHIP_ERROR ProvisionStorageWriter::SetFirmwareInformation(const ByteSpan & value)
{
    (void) value;
    return CHIP_NO_ERROR;
}

CHIP_ERROR ProvisionStorageWriter::SetCertificationDeclaration(const ByteSpan & value)
{
#ifdef SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS
    return WriteStaticSecureToken(*this, SilabsConfig::kSstKey_CD, SilabsConfig::kConfigKey_Creds_CD_Offset,
                                  SilabsConfig::kConfigKey_Creds_CD_Size, value);
#else
    return WriteFile(*this, sCredentialsOffset, SilabsConfig::kConfigKey_Creds_CD_Offset, SilabsConfig::kConfigKey_Creds_CD_Size,
                     value);
#endif
}

CHIP_ERROR ProvisionStorageWriter::SetProductAttestationIntermediateCert(const ByteSpan & value)
{
#ifdef SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS
    return WriteStaticSecureToken(*this, SilabsConfig::kSstKey_PAI, SilabsConfig::kConfigKey_Creds_PAI_Offset,
                                  SilabsConfig::kConfigKey_Creds_PAI_Size, value);
#else
    return WriteFile(*this, sCredentialsOffset, SilabsConfig::kConfigKey_Creds_PAI_Offset, SilabsConfig::kConfigKey_Creds_PAI_Size,
                     value);
#endif
}

CHIP_ERROR ProvisionStorageWriter::SetDeviceAttestationCert(const ByteSpan & value)
{
#ifdef SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS
    return WriteStaticSecureToken(*this, SilabsConfig::kSstKey_DAC, SilabsConfig::kConfigKey_Creds_DAC_Offset,
                                  SilabsConfig::kConfigKey_Creds_DAC_Size, value);
#else
    return WriteFile(*this, sCredentialsOffset, SilabsConfig::kConfigKey_Creds_DAC_Offset, SilabsConfig::kConfigKey_Creds_DAC_Size,
                     value);
#endif
}

CHIP_ERROR ProvisionStorageWriter::SetProvisionVersion(const char * value, size_t size)
{
    return SilabsConfig::WriteConfigValueStr(SilabsConfig::kConfigKey_Provision_Version, value, size);
}

CHIP_ERROR ProvisionStorageWriter::SetTestEventTriggerKey(const ByteSpan & value)
{
#if defined(SL_MATTER_TEST_EVENT_TRIGGER_ENABLED) && SL_MATTER_TEST_EVENT_TRIGGER_ENABLED
    constexpr size_t kEnableKeyLength = 16;
    VerifyOrReturnError(value.size() == kEnableKeyLength, CHIP_ERROR_INVALID_ARGUMENT);
    return SilabsConfig::WriteConfigValueBin(SilabsConfig::kConfigKey_Test_Event_Trigger_Key, value.data(), value.size());
#else
    (void) value;
    return CHIP_ERROR_NOT_IMPLEMENTED;
#endif // SL_MATTER_TEST_EVENT_TRIGGER_ENABLED
}

CHIP_ERROR ProvisionStorageWriter::SetOtaTlvEncryptionKey(const ByteSpan & value)
{
#if defined(SL_MATTER_ENABLE_OTA_ENCRYPTION) && SL_MATTER_ENABLE_OTA_ENCRYPTION
#if defined(SL_MBEDTLS_USE_TINYCRYPT) && SL_MBEDTLS_USE_TINYCRYPT
    return SilabsConfig::WriteConfigValueBin(SilabsConfig::kOtaTlvEncryption_KeyId, value.data(), value.size());
#else
    Silabs::OtaTlvEncryptionKey key;
    ReturnErrorOnFailure(key.Import(value.data(), value.size()));
    return SilabsConfig::WriteConfigValue(SilabsConfig::kOtaTlvEncryption_KeyId, key.GetId());
#endif
#else
    (void) value;
    return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
#endif
}

CHIP_ERROR ProvisionStorageWriter::SetSetupPayload(const uint8_t * value, size_t size)
{
    return SilabsConfig::WriteConfigValueBin(SilabsConfig::kConfigKey_SetupPayloadBitSet, value, size);
}

CHIP_ERROR ProvisionStorageWriter::Initialize(uint32_t flash_addr, uint32_t flash_size)
{
#ifdef SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS
#if defined(_SILICON_LABS_32B_SERIES_2)
    sSstSlotsPrepared = false;
#endif
#else
    sCredentialsOffset = 0;
#endif

    uint32_t base_addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(linker_nvm_end));
    if (flash_size > 0)
    {
#ifndef SLI_SI91X_MCU_INTERFACE
        base_addr = (flash_addr + flash_size - FLASH_PAGE_SIZE);
#endif
#ifdef _SILICON_LABS_32B_SERIES_3
        uint32_t tokenStartAddr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&linker_static_secure_tokens_begin));
#ifdef SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS
        // Certificates are KLV objects managed by the token manager inside the static secure region.
        base_addr = tokenStartAddr;
#else
        base_addr = tokenStartAddr + FLASH_PAGE_SIZE;
#endif
#endif
        TEMPORARY_RETURN_IGNORED chip::DeviceLayer::Silabs::GetPlatform().FlashInit();
#if defined(SL_PROVISION_GENERATOR) && SL_PROVISION_GENERATOR
        // TODO: Might be ok to move this to gfw only using Reader.GetCredentialsBaseAddress().
        // Series 2 GFW learns NVM3 geometry from the host flash size; open NVM3 first, then
        // init SilabsConfig to initialize CTM.
        setNvm3End(base_addr);
        ReturnErrorOnFailure(SilabsConfig::Init());
#endif // SL_PROVISION_GENERATOR
        return SetCredentialsBaseAddress(base_addr);
    }
#ifdef SL_COMMON_TOKEN_MANAGER_ENABLE_STATIC_TOKENS
    // No flash geometry given. Keep the base address written at provisioning time.
    // The legacy reader and the migration to static secure tokens depend on it.
    return CHIP_NO_ERROR;
#else
    return SetCredentialsBaseAddress(base_addr);
#endif
}

CHIP_ERROR ProvisionStorageWriter::Commit()
{
    return CHIP_NO_ERROR;
}

CHIP_ERROR ProvisionStorageWriter::GetFlashPageSize(uint32_t & size)
{
    size = FLASH_PAGE_SIZE;
    return CHIP_NO_ERROR;
}

CHIP_ERROR ProvisionStorageWriter::SetCredentialsBaseAddress(uint32_t addr)
{
    return SilabsConfig::WriteConfigValue(SilabsConfig::kConfigKey_Creds_Base_Addr, addr);
}

CHIP_ERROR ProvisionStorageWriter::GetCredentialsBaseAddress(uint32_t & addr)
{
    return SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_Creds_Base_Addr, addr);
}

CHIP_ERROR ProvisionStorageWriter::SetProvisionRequest(bool value)
{
    return SilabsConfig::WriteConfigValue(SilabsConfig::kConfigKey_Provision_Request, value);
}

CHIP_ERROR ProvisionStorageWriter::GetProvisionRequest(bool & value)
{
    return SilabsConfig::ReadConfigValue(SilabsConfig::kConfigKey_Provision_Request, value);
}

} // namespace Provision
} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
