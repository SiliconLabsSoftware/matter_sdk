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

#include "AppTask.h"
#include "AppConfig.h"
#include "AppEvent.h"
#include "CustomerAppTask.h"

#ifdef DISPLAY_ENABLED
#include "ThermostatUI.h"
#include "lcd.h"
#endif // DISPLAY_ENABLED

#include "thermostat-delegate-impl.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/clusters/thermostat-server/thermostat-server.h>
#include <app/server/Server.h>
#include <app/util/attribute-storage.h>
#include <cmsis_os2.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/PlatformError.h>
#include <platform/silabs/platformAbstraction/SilabsPlatform.h>

#if defined(SL_MATTER_USE_SI70XX_SENSOR) && SL_MATTER_USE_SI70XX_SENSOR
#include "Si70xxSensor.h"
#endif // defined(SL_MATTER_USE_SI70XX_SENSOR) && SL_MATTER_USE_SI70XX_SENSOR

#ifdef SL_MATTER_ENABLE_AWS
#include "MatterAwsControl.h"
#endif // SL_MATTER_ENABLE_AWS

#define APP_FUNCTION_BUTTON 0

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace ::chip::DeviceLayer;

namespace ThermAttr = chip::app::Clusters::Thermostat::Attributes;

namespace {

CustomerAppTask & appInstance()
{
    return CustomerAppTask::GetAppTask();
}

constexpr EndpointId kThermostatEndpoint = 1;
constexpr uint16_t kSensorTimerPeriodMs  = 30000; // 30s timer period
constexpr uint16_t kMinTemperatureDelta  = 50;    // 0.5 degree Celcius

#if !(defined(SL_MATTER_USE_SI70XX_SENSOR) && (SL_MATTER_USE_SI70XX_SENSOR))
constexpr uint16_t kSimulatedReadingFrequency = (60000 / kSensorTimerPeriodMs); // Change Simulated number at each minutes
int16_t sSimulatedTemp[]                      = { 2300, 2400, 2800, 2550, 2200, 2125, 2100, 2600, 1800, 2700 };
uint8_t sNbOfRepetition = 0;
uint8_t sSimulatedIndex = 0;
#endif // !(defined(SL_MATTER_USE_SI70XX_SENSOR) && (SL_MATTER_USE_SI70XX_SENSOR))

int8_t sCurrentTempCelsius     = 0;
int8_t sCoolingCelsiusSetPoint = 0;
int8_t sHeatingCelsiusSetPoint = 0;
uint8_t sThermMode             = 0;

osTimerId_t sSensorTimer = nullptr;
int16_t sLastTemperature = 0;

int8_t ConvertToPrintableTemp(int16_t temperature)
{
    constexpr uint8_t kRoundUpValue = 50;

    // Round up the temperature as we won't print decimals on LCD.
    if (temperature < 0)
    {
        temperature -= kRoundUpValue;
    }
    else
    {
        temperature += kRoundUpValue;
    }

    return static_cast<int8_t>(temperature / 100);
}

void AttributeChangeHandler(EndpointId endpointId, AttributeId attributeId, uint8_t * value, uint16_t size)
{
    switch (attributeId)
    {
    case ThermAttr::LocalTemperature::Id: {
        int8_t temp = ConvertToPrintableTemp(*reinterpret_cast<int16_t *>(value));
        SILABS_LOG("Local temp %d", temp);
        sCurrentTempCelsius = temp;
    }
    break;

    case ThermAttr::OccupiedCoolingSetpoint::Id: {
        int8_t coolingTemp = ConvertToPrintableTemp(*reinterpret_cast<int16_t *>(value));
        SILABS_LOG("CoolingSetpoint %d", coolingTemp);
        sCoolingCelsiusSetPoint = coolingTemp;
    }
    break;

    case ThermAttr::OccupiedHeatingSetpoint::Id: {
        int8_t heatingTemp = ConvertToPrintableTemp(*reinterpret_cast<int16_t *>(value));
        SILABS_LOG("HeatingSetpoint %d", heatingTemp);
        sHeatingCelsiusSetPoint = heatingTemp;
    }
    break;

    case ThermAttr::SystemMode::Id: {
        SILABS_LOG("SystemMode %d", static_cast<uint8_t>(*value));
        uint8_t mode = static_cast<uint8_t>(*value);
        if (sThermMode != mode)
        {
            sThermMode = mode;
        }
    }
    break;

    default:
        SILABS_LOG("Unhandled thermostat attribute %x", attributeId);
        return;
    }

    appInstance().UpdateThermoStatUI();
}

} // namespace

void emberAfThermostatClusterInitCallback(EndpointId endpoint)
{
    auto & delegate = ThermostatDelegate::GetInstance();
    SetDefaultDelegate(endpoint, &delegate);
}

CHIP_ERROR AppTask::AppInit()
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    chip::DeviceLayer::Silabs::GetPlatform().SetButtonsCb(&CustomerAppTask::ButtonEventHandler);

#ifdef DISPLAY_ENABLED
    GetLCD().SetCustomUI(ThermostatUI::DrawUI);
#endif

    err = appInstance().InitThermostat();
    if (err != CHIP_NO_ERROR)
    {
        SILABS_LOG("InitThermostat() failed");
        appError(err);
    }

    return err;
}

CHIP_ERROR AppTask::InitThermostat()
{
    sSensorTimer = osTimerNew(&CustomerAppTask::SensorTimerEventHandler, osTimerPeriodic, nullptr, nullptr);
    if (sSensorTimer == nullptr)
    {
        SILABS_LOG("sSensorTimer timer create failed");
        return APP_ERROR_CREATE_TIMER_FAILED;
    }

#if defined(SL_MATTER_USE_SI70XX_SENSOR) && SL_MATTER_USE_SI70XX_SENSOR
    sl_status_t status = Si70xxSensor::Init();
    if (status != SL_STATUS_OK)
    {
        SILABS_LOG("Failed to Init Sensor with error code: %lx", status);
        return MATTER_PLATFORM_ERROR(status);
    }
#endif // defined(SL_MATTER_USE_SI70XX_SENSOR) && SL_MATTER_USE_SI70XX_SENSOR

    DataModel::Nullable<int16_t> temp;
    int16_t heatingSetpoint = 0;
    int16_t coolingSetpoint = 0;
    Thermostat::SystemModeEnum systemMode;

    PlatformMgr().LockChipStack();
    ThermAttr::LocalTemperature::Get(kThermostatEndpoint, temp);
    ThermAttr::OccupiedCoolingSetpoint::Get(kThermostatEndpoint, &coolingSetpoint);
    ThermAttr::OccupiedHeatingSetpoint::Get(kThermostatEndpoint, &heatingSetpoint);
    ThermAttr::SystemMode::Get(kThermostatEndpoint, &systemMode);
    PlatformMgr().UnlockChipStack();

    sCurrentTempCelsius     = ConvertToPrintableTemp(temp.IsNull() ? static_cast<int16_t>(0) : temp.Value());
    sHeatingCelsiusSetPoint = ConvertToPrintableTemp(coolingSetpoint);
    sCoolingCelsiusSetPoint = ConvertToPrintableTemp(heatingSetpoint);

    switch (systemMode)
    {
    case Thermostat::SystemModeEnum::kOff:
        sThermMode = 0;
        break;
    case Thermostat::SystemModeEnum::kAuto:
        sThermMode = 1;
        break;
    case Thermostat::SystemModeEnum::kCool:
        sThermMode = 3;
        break;
    case Thermostat::SystemModeEnum::kHeat:
        sThermMode = 4;
        break;
    case Thermostat::SystemModeEnum::kEmergencyHeat:
        sThermMode = 5;
        break;
    case Thermostat::SystemModeEnum::kPrecooling:
        sThermMode = 6;
        break;
    case Thermostat::SystemModeEnum::kFanOnly:
        sThermMode = 7;
        break;
    case Thermostat::SystemModeEnum::kDry:
        sThermMode = 8;
        break;
    case Thermostat::SystemModeEnum::kSleep:
        sThermMode = 9;
        break;
    default:
        sThermMode = 2;
        break;
    }

    appInstance().UpdateThermoStatUI();

    AppTask::SensorTimerEventHandler(nullptr);
    osTimerStart(sSensorTimer, pdMS_TO_TICKS(kSensorTimerPeriodMs));

    return CHIP_NO_ERROR;
}

CHIP_ERROR AppTask::StartAppTask()
{
    return BaseApplication::StartAppTask(&AppTask::AppTaskMain);
}

void AppTask::AppTaskMain(void * pvParameter)
{
    AppEvent event;
    osMessageQueueId_t sAppEventQueue = *(static_cast<osMessageQueueId_t *>(pvParameter));

    CHIP_ERROR err = GetAppTask().Init();
    if (err != CHIP_NO_ERROR)
    {
        SILABS_LOG("AppTask.Init() failed");
        appError(err);
    }

#if !(defined(CHIP_CONFIG_ENABLE_ICD_SERVER) && CHIP_CONFIG_ENABLE_ICD_SERVER)
    GetAppTask().StartStatusLEDTimer();
#endif

    SILABS_LOG("App Task started");
    while (true)
    {
        osStatus_t eventReceived = osMessageQueueGet(sAppEventQueue, &event, nullptr, osWaitForever);
        while (eventReceived == osOK)
        {
            GetAppTask().DispatchEvent(&event);
            eventReceived = osMessageQueueGet(sAppEventQueue, &event, nullptr, 0);
        }
    }
}

void AppTask::UpdateThermoStatUI()
{
#ifdef DISPLAY_ENABLED
    ThermostatUI::SetMode(sThermMode);
    ThermostatUI::SetHeatingSetPoint(sHeatingCelsiusSetPoint);
    ThermostatUI::SetCoolingSetPoint(sCoolingCelsiusSetPoint);
    ThermostatUI::SetCurrentTemp(sCurrentTempCelsius);

#ifdef SL_WIFI
    if (ConnectivityMgr().IsWiFiStationProvisioned())
#else
    if (ConnectivityMgr().IsThreadProvisioned())
#endif // !SL_WIFI
    {
        GetLCD().WriteDemoUI(false); // State doesn't matter
    }
#else
    SILABS_LOG("Thermostat Status - M:%d T:%d'C H:%d'C C:%d'C", sThermMode, sCurrentTempCelsius, sHeatingCelsiusSetPoint,
               sCoolingCelsiusSetPoint);
#endif // DISPLAY_ENABLED
}

void AppTask::ButtonEventHandler(uint8_t button, uint8_t btnAction)
{
    AppEvent aEvent           = {};
    aEvent.Type               = AppEvent::kEventType_Button;
    aEvent.ButtonEvent.Action = btnAction;

    if (button == APP_FUNCTION_BUTTON)
    {
        aEvent.Handler = BaseApplication::ButtonHandler;
        appInstance().PostEvent(&aEvent);
    }
}

void AppTask::SensorTimerEventHandler(void * /* arg */)
{
    AppEvent event;
    event.Type    = AppEvent::kEventType_Timer;
    event.Handler = &CustomerAppTask::TemperatureUpdateEventHandler;
    appInstance().PostEvent(&event);
}

void AppTask::TemperatureUpdateEventHandler(AppEvent * /* aEvent */)
{
    int16_t temperature = 0;

#if defined(SL_MATTER_USE_SI70XX_SENSOR) && SL_MATTER_USE_SI70XX_SENSOR
    int32_t tempSum   = 0;
    uint16_t humidity = 0;

    for (uint8_t i = 0; i < 100; i++)
    {
        if (SL_STATUS_OK != Si70xxSensor::GetSensorData(humidity, temperature))
        {
            SILABS_LOG("Failed to read Temperature !!!");
        }
        tempSum += temperature;
    }
    temperature = static_cast<int16_t>(tempSum / 100);
#else
    if (sSimulatedIndex >= MATTER_ARRAY_SIZE(sSimulatedTemp))
    {
        sSimulatedIndex = 0;
    }
    temperature = sSimulatedTemp[sSimulatedIndex];

    sNbOfRepetition++;
    if (sNbOfRepetition >= kSimulatedReadingFrequency)
    {
        sSimulatedIndex++;
        sNbOfRepetition = 0;
    }
#endif // defined(SL_MATTER_USE_SI70XX_SENSOR) && SL_MATTER_USE_SI70XX_SENSOR

    SILABS_LOG("Sensor Temp is : %d", temperature);

    MarkAttributeDirty reportState = MarkAttributeDirty::kNo;
    if ((temperature >= (sLastTemperature + kMinTemperatureDelta)) || temperature <= (sLastTemperature - kMinTemperatureDelta))
    {
        reportState = MarkAttributeDirty::kIfChanged;
    }

    sLastTemperature = temperature;
    PlatformMgr().LockChipStack();
    Thermostat::Attributes::LocalTemperature::Set(kThermostatEndpoint, temperature, reportState);
    PlatformMgr().UnlockChipStack();
}

void AppTask::DMPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                            uint8_t * value)
{
    ClusterId clusterId     = attributePath.mClusterId;
    AttributeId attributeId = attributePath.mAttributeId;
    ChipLogDetail(Zcl, "Cluster callback: " ChipLogFormatMEI, ChipLogValueMEI(clusterId));

    if (clusterId == Identify::Id)
    {
        ChipLogProgress(Zcl, "Identify attribute ID: " ChipLogFormatMEI " Type: %u Value: %u, length %u",
                        ChipLogValueMEI(attributeId), type, *value, size);
    }
    else if (clusterId == Thermostat::Id)
    {
        AttributeChangeHandler(attributePath.mEndpointId, attributeId, value, size);
#ifdef SL_MATTER_ENABLE_AWS
        matterAws::control::AttributeHandler(attributePath.mEndpointId, attributeId);
#endif // SL_MATTER_ENABLE_AWS
    }
}
