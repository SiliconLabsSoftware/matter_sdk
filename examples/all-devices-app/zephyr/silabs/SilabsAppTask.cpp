/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
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

#include "SilabsAppTask.h"

#include <credentials/DeviceAttestationCredsProvider.h>
#include <headers/ProvisionStorage.h>
#include <lib/support/CodeUtils.h>
#include <platform/CommissionableDataProvider.h>
#include <platform/DeviceInstanceInfoProvider.h>

using chip::Credentials::SetDeviceAttestationCredentialsProvider;
using chip::DeviceLayer::SetCommissionableDataProvider;
using chip::DeviceLayer::SetDeviceInstanceInfoProvider;

namespace chip::app::AllDevices {

CHIP_ERROR SilabsAppTask::InitCredentials()
{
    // Replace all 3 providers to use Silabs provisioning.
    static DeviceLayer::Silabs::Provision::Storage sStorage;
    ReturnErrorOnFailure(sStorage.Initialize());
    SetDeviceInstanceInfoProvider(&sStorage);
    SetCommissionableDataProvider(&sStorage);
    SetDeviceAttestationCredentialsProvider(&sStorage);
    return CHIP_NO_ERROR;
}

} // namespace chip::app::AllDevices
