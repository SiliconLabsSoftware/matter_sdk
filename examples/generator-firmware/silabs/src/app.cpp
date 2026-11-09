/*******************************************************************************
 * @file app.cpp
 * @brief Provisioning application task
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inin. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inin. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#include "app.h"
#include <FreeRTOS.h>
#include <headers/ProvisionManager.h>
#include <lib/support/CHIPMem.h>
#include <platform/silabs/platformAbstraction/SilabsPlatform.h>
#include <provision/ProvisionChannel.h>
#include <provision/ProvisionCrypto.h>
#include <provision/ProvisionStorageReader.h>
#include <provision/ProvisionStorageWriter.h>
#include <task.h>

#define MAIN_TASK_STACK_SIZE (1024)
#define MAIN_TASK_PRIORITY (configMAX_PRIORITIES - 1)

using namespace chip::DeviceLayer::Silabs;

namespace {

TaskHandle_t main_Task;

void resetDevice()
{
    GetPlatform().SoftwareReset();
}

void taskMain(void * pvParameter)
{
    Provision::Manager & manager                      = Provision::Manager::GetInstance();
    Provision::ProvisionStorageWriter & storageWriter = Provision::ProvisionStorageWriter::GetInstance();

#if defined(SLI_SI91X_MCU_INTERFACE) && SLI_SI91X_MCU_INTERFACE
    RETURN_SAFELY_IGNORED(storageWriter.Initialize());
#endif

    manager.SetStorageBackend(Provision::ProvisionStorageReader::GetInstance(), &storageWriter);
    manager.SetCryptoProvider(Provision::ProvisionCrypto::GetInstance());
    manager.SetChannel(Provision::ProvisionChannel::GetInstance());
    manager.SetResetHandler(resetDevice);

    // Initialize
    RETURN_SAFELY_IGNORED(manager.Init(true));
    // Run manager
    while (true)
        manager.Step();
}

} // namespace

/*******************************************************************************
 * Initialize application.
 ******************************************************************************/

extern "C" void app_init(void)
{
    app_platform_init();
    chip::Platform::MemoryInit();
    xTaskCreate(taskMain, "Provision Task", MAIN_TASK_STACK_SIZE, nullptr, MAIN_TASK_PRIORITY, &main_Task);
}
