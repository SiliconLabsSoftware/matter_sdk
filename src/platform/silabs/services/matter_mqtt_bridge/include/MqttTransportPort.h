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

#pragma once

#include <lib/core/CHIPError.h>
#include <lib/support/Span.h>
#include <platform/silabs/services/mqtt_client.h>

/** Abstraction over MQTT publish/subscribe and cross-thread work scheduling. */
class MqttTransportPort
{
public:
    virtual ~MqttTransportPort() = default;

    virtual bool IsConnected() const = 0;

    virtual void SetSubscriptionCallback(chip::DeviceLayer::Silabs::MqttSubscriptionCallback callback, void * context) = 0;

    virtual CHIP_ERROR Publish(const char * topic, chip::ByteSpan payload, chip::DeviceLayer::Silabs::MqttQoS qos, bool retained,
                               chip::DeviceLayer::Silabs::MqttOperationCallback callback = nullptr,
                               void * context                                            = nullptr) = 0;

    virtual CHIP_ERROR Subscribe(const char * topic, chip::DeviceLayer::Silabs::MqttQoS qos,
                                 chip::DeviceLayer::Silabs::MqttOperationCallback callback = nullptr,
                                 void * context                                            = nullptr) = 0;

    virtual CHIP_ERROR ScheduleWork(chip::DeviceLayer::Silabs::MqttWorkFn fn, const void * data, size_t len) = 0;
};

/** Default implementation backed by chip::DeviceLayer::Silabs::MqttClient. */
class MqttClientTransportPort final : public MqttTransportPort
{
public:
    void Bind(chip::DeviceLayer::Silabs::MqttClient & client);

    bool IsConnected() const override;
    void SetSubscriptionCallback(chip::DeviceLayer::Silabs::MqttSubscriptionCallback callback, void * context) override;
    CHIP_ERROR Publish(const char * topic, chip::ByteSpan payload, chip::DeviceLayer::Silabs::MqttQoS qos, bool retained,
                       chip::DeviceLayer::Silabs::MqttOperationCallback callback, void * context) override;
    CHIP_ERROR Subscribe(const char * topic, chip::DeviceLayer::Silabs::MqttQoS qos,
                         chip::DeviceLayer::Silabs::MqttOperationCallback callback, void * context) override;
    CHIP_ERROR ScheduleWork(chip::DeviceLayer::Silabs::MqttWorkFn fn, const void * data, size_t len) override;

private:
    chip::DeviceLayer::Silabs::MqttClient * mClient = nullptr;
};
