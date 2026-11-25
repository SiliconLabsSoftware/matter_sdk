/*****************************************************************************
 * @file main.cpp
 * @brief main() function.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc.  Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement.  This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#include <headers/ProvisionManager.h>
#include <lib/support/CHIPMem.h>
#include <provision/ProvisionChannel.h>
#include <provision/ProvisionCrypto.h>
#include <provision/ProvisionStorageReader.h>
#include <provision/ProvisionStorageWriter.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

using namespace chip::DeviceLayer::Silabs::Provision;

void resetDevice()
{
    sys_reboot(SYS_REBOOT_COLD);
}

int main()
{
    chip::Platform::MemoryInit();
    ChipLogError(DeviceLayer, "Starting Matter Provisioning");

    Manager & manager = Manager::GetInstance();

    // NVS geometry comes from the first host packet (Protocol V2 calls
    // ProvisionStorageWriter::Initialize). Do not initialize storage here.
    manager.SetStorageBackend(ProvisionStorageReader::GetInstance(), &ProvisionStorageWriter::GetInstance());
    manager.SetCryptoProvider(ProvisionCrypto::GetInstance());
    manager.SetChannel(ProvisionChannel::GetInstance());
    manager.SetResetHandler(resetDevice);
    if (manager.Init(true) != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Failed to initialize provisioning manager");
        return -1;
    }

    while (manager.Step())
    {
        k_yield();
    }

    return 0;
}
