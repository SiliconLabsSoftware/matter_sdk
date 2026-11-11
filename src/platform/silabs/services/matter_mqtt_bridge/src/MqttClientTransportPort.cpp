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

#include "MqttTransportPort.h"

#include <lib/support/CodeUtils.h>

// Thin adapter from MqttTransportPort to Silabs::MqttClient.

void MqttClientTransportPort::Bind(chip::DeviceLayer::Silabs::MqttClient & client)
{
    mClient = &client;
}

bool MqttClientTransportPort::IsConnected() const
{
    return mClient != nullptr && mClient->IsConnected();
}

void MqttClientTransportPort::SetSubscriptionCallback(chip::DeviceLayer::Silabs::MqttSubscriptionCallback callback, void * context)
{
    VerifyOrReturn(mClient != nullptr);
    mClient->SetSubscriptionCallback(callback, context);
}

// Forwards to MqttClient; @p topic must stay valid until the queued publish finishes (null-terminated C string).
CHIP_ERROR MqttClientTransportPort::Publish(const char * topic, chip::ByteSpan payload, chip::DeviceLayer::Silabs::MqttQoS qos,
                                              bool retained, chip::DeviceLayer::Silabs::MqttOperationCallback callback, void * context)
{
    VerifyOrReturnError(mClient != nullptr, CHIP_ERROR_INCORRECT_STATE);
    return mClient->Publish(topic, payload, qos, retained, callback, context);
}

CHIP_ERROR MqttClientTransportPort::Subscribe(const char * topic, chip::DeviceLayer::Silabs::MqttQoS qos,
                                              chip::DeviceLayer::Silabs::MqttOperationCallback callback, void * context)
{
    VerifyOrReturnError(mClient != nullptr, CHIP_ERROR_INCORRECT_STATE);
    return mClient->Subscribe(topic, qos, callback, context);
}

CHIP_ERROR MqttClientTransportPort::ScheduleWork(chip::DeviceLayer::Silabs::MqttWorkFn fn, const void * data, size_t len)
{
    VerifyOrReturnError(mClient != nullptr, CHIP_ERROR_INCORRECT_STATE);
    return mClient->ScheduleWork(fn, data, len);
}
