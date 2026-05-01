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

#pragma once

/**********************************************************
 * Includes
 *********************************************************/

#include <stdbool.h>
#include <stdint.h>

#include <cstring>

#include "AppEvent.h"
#include "BaseApplication.h"
#include "SilabsLockTypes.h"
#include <app/ConcreteAttributePath.h>
#include <app/clusters/door-lock-server/door-lock-server.h>
#include <ble/Ble.h>
#include <cmsis_os2.h>
#include <lib/core/CHIPError.h>
#include <lib/core/CHIPPersistentStorageDelegate.h>
#include <lib/support/DefaultStorageKeyAllocator.h>
#include <lib/support/TypeTraits.h>
#include <platform/CHIPDeviceLayer.h>

/**********************************************************
 * Defines
 *********************************************************/

// Application-defined error codes in the CHIP_ERROR space.
#define APP_ERROR_EVENT_QUEUE_FAILED CHIP_APPLICATION_ERROR(0x01)
#define APP_ERROR_CREATE_TASK_FAILED CHIP_APPLICATION_ERROR(0x02)
#define APP_ERROR_UNHANDLED_EVENT CHIP_APPLICATION_ERROR(0x03)
#define APP_ERROR_CREATE_TIMER_FAILED CHIP_APPLICATION_ERROR(0x04)
#define APP_ERROR_START_TIMER_FAILED CHIP_APPLICATION_ERROR(0x05)
#define APP_ERROR_STOP_TIMER_FAILED CHIP_APPLICATION_ERROR(0x06)
#define APP_ERROR_ALLOCATION_FAILED CHIP_APPLICATION_ERROR(0x07)
#if defined(ENABLE_CHIP_SHELL)
#define APP_ERROR_TOO_MANY_SHELL_ARGUMENTS CHIP_APPLICATION_ERROR(0x08)
#endif // ENABLE_CHIP_SHELL

/**********************************************************
 * AppTask Declaration
 *********************************************************/

class AppTask : public BaseApplication
{

public:
    AppTask() = default;

    static AppTask & GetAppTask();

    /**
     * @brief AppTask task main loop function
     *
     * @param pvParameter FreeRTOS task parameter
     */
    static void AppTaskMain(void * pvParameter);

    CHIP_ERROR StartAppTask();

    /** Door lock actuator / cluster actions (numeric values match legacy LockManager::Action_t for AppEvent storage). */
    enum class LockAction : uint8_t
    {
        kLock = 0,
        kUnlock,
        kUnlatch,
        kInvalid,
    };

    void ActionRequest(int32_t aActor, LockAction aAction);
    static void ActionInitiated(LockAction aAction, int32_t aActor);
    static void ActionCompleted(LockAction aAction);

    bool InitiateLockAction(int32_t aActor, LockAction aAction);

    void SetCallbacks(void (*onInitiated)(LockAction, int32_t), void (*onCompleted)(LockAction));

    bool NextState();
    bool IsActionInProgress();

    bool Lock(chip::EndpointId endpointId, const chip::app::DataModel::Nullable<chip::FabricIndex> & fabricIdx,
              const chip::app::DataModel::Nullable<chip::NodeId> & nodeId, const chip::Optional<chip::ByteSpan> & pin,
              chip::app::Clusters::DoorLock::OperationErrorEnum & err);
    bool Unlock(chip::EndpointId endpointId, const chip::app::DataModel::Nullable<chip::FabricIndex> & fabricIdx,
                const chip::app::DataModel::Nullable<chip::NodeId> & nodeId, const chip::Optional<chip::ByteSpan> & pin,
                chip::app::Clusters::DoorLock::OperationErrorEnum & err);
    bool Unbolt(chip::EndpointId endpointId, const chip::app::DataModel::Nullable<chip::FabricIndex> & fabricIdx,
                const chip::app::DataModel::Nullable<chip::NodeId> & nodeId, const chip::Optional<chip::ByteSpan> & pin,
                chip::app::Clusters::DoorLock::OperationErrorEnum & err);

    bool GetUser(chip::EndpointId endpointId, uint16_t userIndex, EmberAfPluginDoorLockUserInfo & user);
    bool SetUser(chip::EndpointId endpointId, uint16_t userIndex, chip::FabricIndex creator, chip::FabricIndex modifier,
                 const chip::CharSpan & userName, uint32_t uniqueId, UserStatusEnum userStatus, UserTypeEnum usertype,
                 CredentialRuleEnum credentialRule, const CredentialStruct * credentials, size_t totalCredentials);

    bool GetCredential(chip::EndpointId endpointId, uint16_t credentialIndex, CredentialTypeEnum credentialType,
                       EmberAfPluginDoorLockCredentialInfo & credential);

    bool SetCredential(chip::EndpointId endpointId, uint16_t credentialIndex, chip::FabricIndex creator, chip::FabricIndex modifier,
                       DlCredentialStatus credentialStatus, CredentialTypeEnum credentialType, const chip::ByteSpan & credentialData);

    DlStatus GetWeekdaySchedule(chip::EndpointId endpointId, uint8_t weekdayIndex, uint16_t userIndex,
                                EmberAfPluginDoorLockWeekDaySchedule & schedule);

    DlStatus SetWeekdaySchedule(chip::EndpointId endpointId, uint8_t weekdayIndex, uint16_t userIndex, DlScheduleStatus status,
                                DaysMaskMap daysMask, uint8_t startHour, uint8_t startMinute, uint8_t endHour, uint8_t endMinute);

    DlStatus GetYeardaySchedule(chip::EndpointId endpointId, uint8_t yearDayIndex, uint16_t userIndex,
                                EmberAfPluginDoorLockYearDaySchedule & schedule);

    DlStatus SetYeardaySchedule(chip::EndpointId endpointId, uint8_t yearDayIndex, uint16_t userIndex, DlScheduleStatus status,
                                uint32_t localStartTime, uint32_t localEndTime);

    DlStatus GetHolidaySchedule(chip::EndpointId endpointId, uint8_t holidayIndex, EmberAfPluginDoorLockHolidaySchedule & schedule);

    DlStatus SetHolidaySchedule(chip::EndpointId endpointId, uint8_t holidayIndex, DlScheduleStatus status, uint32_t localStartTime,
                                uint32_t localEndTime, OperatingModeEnum operatingMode);

    bool IsValidUserIndex(uint16_t userIndex);
    bool IsValidCredentialIndex(uint16_t credentialIndex, CredentialTypeEnum type);
    bool IsValidCredentialType(CredentialTypeEnum type);
    bool IsValidWeekdayScheduleIndex(uint8_t scheduleIndex);
    bool IsValidYeardayScheduleIndex(uint8_t scheduleIndex);
    bool IsValidHolidayScheduleIndex(uint8_t scheduleIndex);

    void UnlockAfterUnlatch();

    static void UpdateClusterStateAfterUnlatch(intptr_t context);

    /**
     * @brief Event handler when a button is pressed
     * Function posts an event for button processing
     *
     * @param buttonHandle APP_LIGHT_SWITCH or APP_FUNCTION_BUTTON
     * @param btnAction button action - SL_SIMPLE_BUTTON_PRESSED,
     *                  SL_SIMPLE_BUTTON_RELEASED or SL_SIMPLE_BUTTON_DISABLED
     */
    static void ButtonEventHandler(uint8_t button, uint8_t btnAction);

    /**
     * @brief Handle lock update event
     *
     * @param aEvent event received
     */
    static void LockActionEventHandler(AppEvent * aEvent);

    void DMPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                       uint8_t * value);

protected:
    /**
     * @brief Override of BaseApplication::AppInit() virtual method, called by BaseApplication::Init()
     *
     * @return CHIP_ERROR
     */
    CHIP_ERROR AppInit() override;

private:
    enum class LockActuatorState : uint8_t
    {
        kLockInitiated = 0,
        kLockCompleted,
        kUnlockInitiated,
        kUnlatchInitiated,
        kUnlockCompleted,
        kUnlatchCompleted,
    };

    struct UnlatchContext
    {
        static constexpr uint8_t kMaxPinLength = UINT8_MAX;
        uint8_t mPinBuffer[kMaxPinLength];
        uint8_t mPinLength = 0;
        chip::EndpointId mEndpointId = chip::kInvalidEndpointId;
        chip::app::DataModel::Nullable<chip::FabricIndex> mFabricIdx;
        chip::app::DataModel::Nullable<chip::NodeId> mNodeId;
        chip::app::Clusters::DoorLock::OperationErrorEnum mErr;

        void Update(chip::EndpointId endpointId, const chip::app::DataModel::Nullable<chip::FabricIndex> & fabricIdx,
                    const chip::app::DataModel::Nullable<chip::NodeId> & nodeId, const chip::Optional<chip::ByteSpan> & pin,
                    chip::app::Clusters::DoorLock::OperationErrorEnum & err)
        {
            mEndpointId = endpointId;
            mFabricIdx  = fabricIdx;
            mNodeId     = nodeId;
            mErr        = err;

            if (pin.HasValue())
            {
                memcpy(mPinBuffer, pin.Value().data(), pin.Value().size());
                mPinLength = static_cast<uint8_t>(pin.Value().size());
            }
            else
            {
                memset(mPinBuffer, 0, kMaxPinLength);
                mPinLength = 0;
            }
        }
    };

    CHIP_ERROR InitLockDomain(chip::app::DataModel::Nullable<chip::app::Clusters::DoorLock::DlLockState> state,
                              SilabsDoorLock::LockInitParams::LockParam lockParam, chip::PersistentStorageDelegate * storage);

    bool MigrateLockConfig(const SilabsDoorLock::LockInitParams::LockParam & params);

    bool SetLockState(chip::EndpointId endpointId, const chip::app::DataModel::Nullable<chip::FabricIndex> & fabricIdx,
                      const chip::app::DataModel::Nullable<chip::NodeId> & nodeId, chip::app::Clusters::DoorLock::DlLockState lockState,
                      const chip::Optional<chip::ByteSpan> & pin, chip::app::Clusters::DoorLock::OperationErrorEnum & err);
    const char * LockStateToString(chip::app::Clusters::DoorLock::DlLockState lockState) const;

    void CancelTimer();
    void StartTimer(uint32_t aTimeoutMs);

    static void TimerEventHandler(void * timerCbArg);
    static void ActuatorMovementTimerEventHandler(AppEvent * aEvent);

    static chip::StorageKeyName LockUserEndpoint(chip::EndpointId endpoint, uint16_t userIndex)
    {
        return chip::StorageKeyName::Formatted("g/e/%x/lu/%x", endpoint, userIndex);
    }
    static chip::StorageKeyName LockCredentialEndpoint(chip::EndpointId endpoint, CredentialTypeEnum credentialType,
                                                       uint16_t credentialIndex)
    {
        return chip::StorageKeyName::Formatted("g/e/%x/ct/%x/lc/%x", endpoint, chip::to_underlying(credentialType), credentialIndex);
    }
    static chip::StorageKeyName LockUserCredentialMap(uint16_t userIndex)
    {
        return chip::StorageKeyName::Formatted("g/lu/%x/lc", userIndex);
    }
    static chip::StorageKeyName LockUserWeekDayScheduleEndpoint(chip::EndpointId endpoint, uint16_t userIndex,
                                                                uint16_t scheduleIndex)
    {
        return chip::StorageKeyName::Formatted("g/e/%x/lu/%x/lw/%x", endpoint, userIndex, scheduleIndex);
    }
    static chip::StorageKeyName LockUserYearDayScheduleEndpoint(chip::EndpointId endpoint, uint16_t userIndex,
                                                                uint16_t scheduleIndex)
    {
        return chip::StorageKeyName::Formatted("g/e/%x/lu/%x/ly/%x", endpoint, userIndex, scheduleIndex);
    }
    static chip::StorageKeyName LockHolidayScheduleEndpoint(chip::EndpointId endpoint, uint16_t scheduleIndex)
    {
        return chip::StorageKeyName::Formatted("g/e/%x/lh/%x", endpoint, scheduleIndex);
    }

    UnlatchContext mUnlatchContext;
    chip::EndpointId mCurrentEndpointId = chip::kInvalidEndpointId;
    LockActuatorState mLockActuatorState = LockActuatorState::kLockCompleted;

    void (*mActionInitiated_CB)(LockAction, int32_t aActor) = nullptr;
    void (*mActionCompleted_CB)(LockAction)               = nullptr;

    osTimerId_t mLockTimer = nullptr;

    SilabsDoorLock::LockInitParams::LockParam mLockParams;

    chip::PersistentStorageDelegate * mStorage = nullptr;
    /**
     * @brief Update Cluster State
     *
     * @param context current context
     */
    static void UpdateClusterState(intptr_t context);
};
