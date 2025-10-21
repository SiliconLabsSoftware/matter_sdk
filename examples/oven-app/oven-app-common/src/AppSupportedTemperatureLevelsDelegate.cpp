/*
 *
 *    Copyright (c) 2025 Project CHIP Authors
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

#include <AppSupportedTemperatureLevelsDelegate.h>
#include <app/clusters/temperature-control-server/supported-temperature-levels-manager.h>
#include <lib/support/logging/CHIPLogging.h>

using namespace chip;
using namespace chip::app::Clusters::TemperatureControlledCabinet;
using namespace chip::app::DataModel;
using namespace chip::app::Clusters;

// Define the temperature level options for the oven
CharSpan AppSupportedTemperatureLevelsDelegate::temperatureLevelOptions[3] = { CharSpan::fromCharString("Low"),
                                                                               CharSpan::fromCharString("Medium"),
                                                                               CharSpan::fromCharString("High") };

// Define supported temperature levels by endpoint
const AppSupportedTemperatureLevelsDelegate::EndpointPair AppSupportedTemperatureLevelsDelegate::supportedOptionsByEndpoints[2] = {
    AppSupportedTemperatureLevelsDelegate::EndpointPair(4, temperatureLevelOptions, 3), // CookSurface endpoint 4
    AppSupportedTemperatureLevelsDelegate::EndpointPair(5, temperatureLevelOptions, 3)  // CookSurface endpoint 5
};

uint8_t AppSupportedTemperatureLevelsDelegate::Size()
{
    ChipLogProgress(AppServer, "AppSupportedTemperatureLevelsDelegate::Size() called for endpoint %d", mEndpoint);
    for (auto & endpointPair : supportedOptionsByEndpoints)
    {
        if (endpointPair.mEndpointId == mEndpoint)
        {
            ChipLogProgress(AppServer, "Found endpoint %d with size %d", mEndpoint, endpointPair.mSize);
            return endpointPair.mSize;
        }
    }
    ChipLogProgress(AppServer, "No matching endpoint found for %d", mEndpoint);
    return 0;
}

CHIP_ERROR AppSupportedTemperatureLevelsDelegate::Next(MutableCharSpan & item)
{
    ChipLogProgress(AppServer, "AppSupportedTemperatureLevelsDelegate::Next() called for endpoint %d, index %d", mEndpoint, mIndex);
    for (auto & endpointPair : supportedOptionsByEndpoints)
    {
        if (endpointPair.mEndpointId == mEndpoint)
        {
            if (mIndex < endpointPair.mSize)
            {
                ChipLogProgress(AppServer, "Returning temperature level: %.*s",
                                static_cast<int>(endpointPair.mTemperatureLevels[mIndex].size()),
                                endpointPair.mTemperatureLevels[mIndex].data());
                CHIP_ERROR err = CopyCharSpanToMutableCharSpan(endpointPair.mTemperatureLevels[mIndex], item);
                if (err == CHIP_NO_ERROR)
                {
                    mIndex++;
                }
                return err;
            }
            ChipLogProgress(AppServer, "List exhausted at index %d", mIndex);
            return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
        }
    }
    ChipLogProgress(AppServer, "No matching endpoint found for %d in Next()", mEndpoint);
    return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
}
