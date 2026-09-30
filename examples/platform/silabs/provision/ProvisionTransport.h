/*
 *    Copyright (c) 2026 Project CHIP Authors
 *    All rights reserved.
 */
#pragma once

#include <headers/ProvisionTransport.h>

namespace chip {
namespace DeviceLayer {
namespace Silabs {
namespace Provision {

class ProvisionTransport final : public IProvisionTransport
{
public:
    static ProvisionTransport & GetInstance();

    ProvisionTransport(const ProvisionTransport &)             = delete;
    ProvisionTransport & operator=(const ProvisionTransport &) = delete;

    CHIP_ERROR Init() override;
    CHIP_ERROR Read(uint8_t * buffer, size_t bufferLength, size_t & bytesRead) override;
    CHIP_ERROR Write(const uint8_t * buffer, size_t bufferLength) override;
    CHIP_ERROR OnDataAvailable() override;

private:
    ProvisionTransport() = default;
};

} // namespace Provision
} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
