/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2019 Google LLC.
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

/**
 * @file
 * @brief Customer-facing AppTask definition site.
 *
 * Add `*Impl()` overrides here to customize individual AppTask behaviors.
 * Any `*Impl()` you do not override keeps the default AppTask behavior.
 *
 * See the app README ("Override API Reference") for the full list of
 * overridable methods.
 */

#include "CustomerAppTask.h"

#include <lib/support/CodeUtils.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/silabs/wifi/WifiInterface.h>

using namespace chip;
using namespace chip::DeviceLayer;
using namespace chip::DeviceLayer::Silabs;

CustomerAppTask CustomerAppTask::sAppTask;

static void sWifiEventHandler(const ChipDeviceEvent * aEvent, intptr_t)
{
    if (aEvent->Type != DeviceEventType::kWFXSystemEvent)
    {
        return;
    }

    switch (aEvent->Platform.event.WFXSystemEvent.data.genericMsgEvent.header.id)
    {
    case to_underlying(WifiInterface::WifiEvent::kStartUp):
        ChipLogProgress(DeviceLayer, "Wi-Fi startup");
        break;

    case to_underlying(WifiInterface::WifiEvent::kConnect):
        ChipLogProgress(DeviceLayer, "Wi-Fi connected to AP");
        break;

    case to_underlying(WifiInterface::WifiEvent::kDisconnect):
        ChipLogProgress(DeviceLayer, "Wi-Fi disconnected from AP");
        break;

    case to_underlying(WifiInterface::WifiEvent::kGotIPv4):
        ChipLogProgress(DeviceLayer, "Wi-Fi got IPv4 address");
        break;

    case to_underlying(WifiInterface::WifiEvent::kLostIP):
        ChipLogProgress(DeviceLayer, "Wi-Fi lost IP address");
        break;

    default:
        break;
    }
}

AppTask & AppTask::GetAppTask()
{
    return CustomerAppTask::GetAppTask();
}

CHIP_ERROR CustomerAppTask::AppInitImpl()
{
    ReturnErrorOnFailure(AppTask::AppInit());

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI_STATION
    ReturnErrorOnFailure(PlatformMgr().AddEventHandler(sWifiEventHandler, 0));
#endif

    return CHIP_NO_ERROR;
}
