/*
 *    Copyright (c) 2026 Project CHIP Authors
 *    All rights reserved.
 */
#include "ProvisionTransport.h"

namespace chip {
namespace DeviceLayer {
namespace Silabs {
namespace Provision {

ProvisionTransport & ProvisionTransport::GetInstance()
{
    static ProvisionTransport instance;
    return instance;
}

CHIP_ERROR ProvisionTransport::Init()
{
    return CHIP_NO_ERROR;
}

CHIP_ERROR ProvisionTransport::Read(uint8_t *, size_t, size_t &)
{
    return CHIP_ERROR_READ_FAILED;
}

CHIP_ERROR ProvisionTransport::Write(const uint8_t *, size_t)
{
    return CHIP_NO_ERROR;
}

CHIP_ERROR ProvisionTransport::OnDataAvailable()
{
    return CHIP_NO_ERROR;
}

} // namespace Provision
} // namespace Silabs
} // namespace DeviceLayer
} // namespace chip
