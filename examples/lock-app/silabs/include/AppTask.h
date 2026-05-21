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

#include <stdbool.h>
#include <stdint.h>

#include <algorithm>
#include <cstring>

#include "AppEvent.h"
#include "BaseApplication.h"
#include "CHIPProjectConfig.h"
#include <FreeRTOS.h>
#include <app/ConcreteAttributePath.h>
#include <app/clusters/door-lock-server/door-lock-server.h>
#include <ble/Ble.h>
#include <cmsis_os2.h>
#include <lib/core/CHIPError.h>
#include <lib/core/CHIPPersistentStorageDelegate.h>
#include <lib/support/DefaultStorageKeyAllocator.h>
#include <lib/support/TypeTraits.h>
#include <platform/CHIPDeviceLayer.h>
#include <timers.h>

/** @brief Lock-app-specific application error codes*/
/** @{ */
#define APP_ERROR_ALLOCATION_FAILED CHIP_APPLICATION_ERROR(0x07)
#if defined(ENABLE_CHIP_SHELL)
#define APP_ERROR_TOO_MANY_SHELL_ARGUMENTS CHIP_APPLICATION_ERROR(0x08)
#endif // ENABLE_CHIP_SHELL
/** @} */

/**
 * @brief Door-lock application task.
 *
 * Customer code overrides AppTask behavior via `AppTaskImpl<>` `*Impl()` hooks;
 * see `AppTaskImpl.h` for the full override contract. This header exposes only
 * the entry points that the AppTask main loop, `BaseApplication`, and the
 * DoorLock cluster plugin layer need.
 */
class AppTask : public BaseApplication
{
public:
    AppTask() = default;

    /** @brief Singleton accessor; returns the active `CustomerAppTask` instance. */
    static AppTask & GetAppTask();

    /** @brief Door Lock cluster resource limits read at init. */
    struct LockParam
    {
        uint16_t numberOfUsers                  = 0;
        uint8_t numberOfCredentialsPerUser      = 0;
        uint8_t numberOfWeekdaySchedulesPerUser = 0;
        uint8_t numberOfYeardaySchedulesPerUser = 0;
        uint8_t numberOfHolidaySchedules        = 0;
    };

    /** @brief Fluent builder for `LockParam` (used from `InitLockImpl()` overrides). */
    class ParamBuilder
    {
    public:
        /** @brief Sets `numberOfUsers`. */
        ParamBuilder & SetNumberOfUsers(uint16_t numberOfUsers)
        {
            mLockParam.numberOfUsers = numberOfUsers;
            return *this;
        }
        /** @brief Sets `numberOfCredentialsPerUser`. */
        ParamBuilder & SetNumberOfCredentialsPerUser(uint8_t numberOfCredentialsPerUser)
        {
            mLockParam.numberOfCredentialsPerUser = numberOfCredentialsPerUser;
            return *this;
        }
        /** @brief Sets `numberOfWeekdaySchedulesPerUser`. */
        ParamBuilder & SetNumberOfWeekdaySchedulesPerUser(uint8_t numberOfWeekdaySchedulesPerUser)
        {
            mLockParam.numberOfWeekdaySchedulesPerUser = numberOfWeekdaySchedulesPerUser;
            return *this;
        }
        /** @brief Sets `numberOfYeardaySchedulesPerUser`. */
        ParamBuilder & SetNumberOfYeardaySchedulesPerUser(uint8_t numberOfYeardaySchedulesPerUser)
        {
            mLockParam.numberOfYeardaySchedulesPerUser = numberOfYeardaySchedulesPerUser;
            return *this;
        }
        /** @brief Sets `numberOfHolidaySchedules`. */
        ParamBuilder & SetNumberOfHolidaySchedules(uint8_t numberOfHolidaySchedules)
        {
            mLockParam.numberOfHolidaySchedules = numberOfHolidaySchedules;
            return *this;
        }
        /** @brief Returns the assembled `LockParam`. */
        LockParam GetLockParam() { return mLockParam; }

    private:
        LockParam mLockParam;
    };

    /** @brief Actuator / cluster action for a lock operation. */
    enum class LockAction : uint8_t
    {
        kLock = 0,
        kUnlock,
        kUnlatch,
        kInvalid,
    };

    /** @brief Work item for a lock, unlock, or unlatch request and its cluster attribution. */
    struct LockRequest
    {
        chip::EndpointId endpointId                                   = chip::kInvalidEndpointId;
        LockAction action                                             = LockAction::kInvalid;
        chip::app::Clusters::DoorLock::DlLockState targetClusterState = chip::app::Clusters::DoorLock::DlLockState::kLocked;
        chip::app::DataModel::Nullable<chip::FabricIndex> fabricIdx;
        chip::app::DataModel::Nullable<chip::NodeId> nodeId;
        chip::app::DataModel::Nullable<uint16_t> userIndex;
        LockOpCredentials credential{};
        bool hasCredential   = false;
        bool isUnboltUnlatch = false;
        bool isButtonAction  = false;
    };

    /**
     * @brief AppTask task main loop function.
     *
     * @param pvParameter FreeRTOS task parameter.
     */
    static void AppTaskMain(void * pvParameter);

    /** @brief Start the AppTask FreeRTOS thread. */
    CHIP_ERROR StartAppTask();

    /**
     * @brief Event handler when a button is pressed.
     *
     * @param button    APP_LOCK_SWITCH or APP_FUNCTION_BUTTON.
     * @param btnAction SL_SIMPLE_BUTTON_PRESSED, SL_SIMPLE_BUTTON_RELEASED, or SL_SIMPLE_BUTTON_DISABLED.
     */
    static void ButtonEventHandler(uint8_t button, uint8_t btnAction);

    /** @brief `MatterPostAttributeChangeCallback` hook for Door Lock attribute changes. */
    void DMPostAttributeChangeCallback(const chip::app::ConcreteAttributePath & attributePath, uint8_t type, uint16_t size,
                                       uint8_t * value);

    /** @brief AppTask handler for lock-button presses. */
    static void LockButtonActionHandler(AppEvent * aEvent);

    /** @brief Unlatch timer callback; schedules `UnlockAfterUnlatch` on the chip thread. */
    static void UnlatchCallback(void * argument);

    /** @brief AppTask handler for actuator-movement timer completion. */
    static void ActuatorMovementEventHandler(AppEvent * aEvent);

    /** @brief AppTask handler for `kEventType_Lock`; runs `InitiateLockAction`. */
    static void LockActionEventHandler(AppEvent * aEvent);

    /** @brief AppTask handler for `kEventType_LockRequest`; drains the staged request. */
    static void LockRequestEventHandler(AppEvent * aEvent);

    /** @brief Routes a `LockRequest` through the actuator and cluster state machine. */
    void HandleLockRequestOnAppTask(const LockRequest & request);

    /** @brief Door Lock Lock command handler; validates PIN and stages a `LockRequest`. */
    bool DMDoorLockOnDoorLockCommand(chip::EndpointId endpointId,
                                     const chip::app::DataModel::Nullable<chip::FabricIndex> & fabricIdx,
                                     const chip::app::DataModel::Nullable<chip::NodeId> & nodeId,
                                     const chip::Optional<chip::ByteSpan> & pinCode,
                                     chip::app::Clusters::DoorLock::OperationErrorEnum & err);

    /** @brief Door Lock Unlock command handler; may stage unlatch when `SupportsUnbolt`. */
    bool DMDoorLockOnDoorUnlockCommand(chip::EndpointId endpointId,
                                       const chip::app::DataModel::Nullable<chip::FabricIndex> & fabricIdx,
                                       const chip::app::DataModel::Nullable<chip::NodeId> & nodeId,
                                       const chip::Optional<chip::ByteSpan> & pinCode,
                                       chip::app::Clusters::DoorLock::OperationErrorEnum & err);

    /** @brief Door Lock Unbolt command handler; stages an unlock `LockRequest`. */
    bool DMDoorLockOnDoorUnboltCommand(chip::EndpointId endpointId,
                                       const chip::app::DataModel::Nullable<chip::FabricIndex> & fabricIdx,
                                       const chip::app::DataModel::Nullable<chip::NodeId> & nodeId,
                                       const chip::Optional<chip::ByteSpan> & pinCode,
                                       chip::app::Clusters::DoorLock::OperationErrorEnum & err);

    /** @brief Reads a credential from persistent storage. */
    bool DMDoorLockGetCredential(chip::EndpointId endpointId, uint16_t credentialIndex, CredentialTypeEnum credentialType,
                                 EmberAfPluginDoorLockCredentialInfo & credential);

    /** @brief Writes a credential to persistent storage. */
    bool DMDoorLockSetCredential(chip::EndpointId endpointId, uint16_t credentialIndex, chip::FabricIndex creator,
                                 chip::FabricIndex modifier, DlCredentialStatus credentialStatus, CredentialTypeEnum credentialType,
                                 const chip::ByteSpan & credentialData);

    /** @brief Reads a user (and credential map) from persistent storage. */
    bool DMDoorLockGetUser(chip::EndpointId endpointId, uint16_t userIndex, EmberAfPluginDoorLockUserInfo & user);

    /** @brief Writes a user (and credential map) to persistent storage. */
    bool DMDoorLockSetUser(chip::EndpointId endpointId, uint16_t userIndex, chip::FabricIndex creator, chip::FabricIndex modifier,
                           const chip::CharSpan & userName, uint32_t uniqueId, UserStatusEnum userStatus, UserTypeEnum usertype,
                           CredentialRuleEnum credentialRule, const CredentialStruct * credentials, size_t totalCredentials);

    /** @brief Reads a weekday schedule entry from persistent storage. */
    DlStatus DMDoorLockGetWeekDaySchedule(chip::EndpointId endpointId, uint8_t weekdayIndex, uint16_t userIndex,
                                          EmberAfPluginDoorLockWeekDaySchedule & schedule);

    /** @brief Writes a weekday schedule entry to persistent storage. */
    DlStatus DMDoorLockSetWeekDaySchedule(chip::EndpointId endpointId, uint8_t weekdayIndex, uint16_t userIndex,
                                          DlScheduleStatus status, DaysMaskMap daysMask, uint8_t startHour, uint8_t startMinute,
                                          uint8_t endHour, uint8_t endMinute);

    /** @brief Reads a year-day schedule entry from persistent storage. */
    DlStatus DMDoorLockGetYearDaySchedule(chip::EndpointId endpointId, uint8_t yearDayIndex, uint16_t userIndex,
                                          EmberAfPluginDoorLockYearDaySchedule & schedule);

    /** @brief Writes a year-day schedule entry to persistent storage. */
    DlStatus DMDoorLockSetYearDaySchedule(chip::EndpointId endpointId, uint8_t yearDayIndex, uint16_t userIndex,
                                          DlScheduleStatus status, uint32_t localStartTime, uint32_t localEndTime);

    /** @brief Reads a holiday schedule entry from persistent storage. */
    DlStatus DMDoorLockGetHolidaySchedule(chip::EndpointId endpointId, uint8_t holidayIndex,
                                          EmberAfPluginDoorLockHolidaySchedule & holidaySchedule);

    /** @brief Writes a holiday schedule entry to persistent storage. */
    DlStatus DMDoorLockSetHolidaySchedule(chip::EndpointId endpointId, uint8_t holidayIndex, DlScheduleStatus status,
                                          uint32_t localStartTime, uint32_t localEndTime, OperatingModeEnum operatingMode);

    /** @brief Auto-relock callback; posts a lock action to the AppTask queue. */
    void DMDoorLockOnAutoRelock(chip::EndpointId endpointId);

    /** @brief Validates a PIN against stored credentials. */
    bool ValidatePin(chip::EndpointId endpointId, const chip::Optional<chip::ByteSpan> & pin,
                     chip::app::DataModel::Nullable<uint16_t> & outUserIndex, LockOpCredentials & outCred, bool & outHasCred,
                     chip::app::Clusters::DoorLock::OperationErrorEnum & err);

    /** @brief Pushes `LockState` with remote attribution; caller must hold the chip stack lock. */
    void PushClusterLockState(chip::EndpointId endpointId, chip::app::Clusters::DoorLock::DlLockState lockState,
                              const chip::app::DataModel::Nullable<chip::FabricIndex> & fabricIdx,
                              const chip::app::DataModel::Nullable<chip::NodeId> & nodeId,
                              const chip::app::DataModel::Nullable<uint16_t> & userIndex, const LockOpCredentials * cred,
                              bool hasCred);

    /** @brief Starts an actuator transition; returns false if busy or invalid. */
    bool InitiateLockAction(LockAction aAction, bool fromButton = false);

    /** @brief Chip-thread continuation after unlatch hold; posts the unlock leg. */
    static void UnlockAfterUnlatch(intptr_t context);

protected:
    /** @brief Override of `BaseApplication::AppInit()`. */
    CHIP_ERROR AppInit() override;

    /** @brief Brings up the lock domain (limits, storage, timers, LED). */
    CHIP_ERROR InitLock();

    /** @brief Chip-thread work item: push `LockState` with `kManual` (schedule via `PlatformMgr().ScheduleWork`). */
    static void UpdateClusterState(intptr_t context);

    /** @brief Serialized weekday schedule record for KVS. */
    struct WeekDayScheduleInfo
    {
        DlScheduleStatus status;
        EmberAfPluginDoorLockWeekDaySchedule schedule;
    };

    /** @brief Serialized year-day schedule record for KVS. */
    struct YearDayScheduleInfo
    {
        DlScheduleStatus status;
        EmberAfPluginDoorLockYearDaySchedule schedule;
    };

    /** @brief Serialized holiday schedule record for KVS. */
    struct HolidayScheduleInfo
    {
        DlScheduleStatus status;
        EmberAfPluginDoorLockHolidaySchedule schedule;
    };

    /** @brief Serialized user record for KVS. */
    struct LockUserInfo
    {
        char userName[DOOR_LOCK_MAX_USER_NAME_SIZE];
        uint32_t userNameSize;
        uint32_t userUniqueId;
        UserStatusEnum userStatus;
        UserTypeEnum userType;
        CredentialRuleEnum credentialRule;
        chip::EndpointId endpointId;
        chip::FabricIndex createdBy;
        chip::FabricIndex lastModifiedBy;
        uint16_t currentCredentialCount;
    };

    /** @brief Serialized credential record for KVS. */
    struct LockCredentialInfo
    {
        DlCredentialStatus status;
        CredentialTypeEnum credentialType;
        chip::FabricIndex createdBy;
        chip::FabricIndex lastModifiedBy;
        uint8_t credentialData[SilabsDoorLockConfig::ResourceRanges::kMaxCredentialSize];
        uint32_t credentialDataSize;
    };

    static constexpr uint16_t kLockUserInfoSize        = sizeof(LockUserInfo);
    static constexpr uint16_t kLockCredentialInfoSize  = sizeof(LockCredentialInfo);
    static constexpr uint16_t kCredentialStructSize    = sizeof(CredentialStruct);
    static constexpr uint16_t kWeekDayScheduleInfoSize = sizeof(WeekDayScheduleInfo);
    static constexpr uint16_t kYearDayScheduleInfoSize = sizeof(YearDayScheduleInfo);
    static constexpr uint16_t kHolidayScheduleInfoSize = sizeof(HolidayScheduleInfo);

    /** @brief Actuator state-machine phase. */
    enum class LockActuatorState : uint8_t
    {
        kLockInitiated = 0,
        kLockCompleted,
        kUnlockInitiated,
        kUnlatchInitiated,
        kUnlockCompleted,
        kUnlatchCompleted,
    };

    /** @brief Returns the current actuator phase. */
    LockActuatorState GetActuatorState() const { return mLockActuatorState; }

    /** @brief True while a lock transition is in flight. */
    bool IsActuatorBusy() const;

    /** @brief Chip-thread producer: stage a `LockRequest` and wake AppTask (newest-wins). */
    static void EnqueueLockRequest(const LockRequest & request);

    /** @brief Consumer: copy the staged `LockRequest` into `out` and clear the slot. */
    static bool TryDrainStagedLockRequest(LockRequest & out);

    /** @brief IM attribution carried from unlatch through the auto-unlock leg. */
    struct UnlatchContext
    {
        chip::EndpointId mEndpointId = chip::kInvalidEndpointId;
        chip::app::DataModel::Nullable<chip::FabricIndex> mFabricIdx;
        chip::app::DataModel::Nullable<chip::NodeId> mNodeId;
        chip::app::DataModel::Nullable<uint16_t> mUserIndex;
        LockOpCredentials mCredential{};
        bool mHasCredential = false;

        /** @brief Copies controller attribution into this context. */
        void Update(chip::EndpointId endpointId, const chip::app::DataModel::Nullable<chip::FabricIndex> & fabricIdx,
                    const chip::app::DataModel::Nullable<chip::NodeId> & nodeId,
                    const chip::app::DataModel::Nullable<uint16_t> & userIndex, const LockOpCredentials & cred, bool hasCred)
        {
            mEndpointId    = endpointId;
            mFabricIdx     = fabricIdx;
            mNodeId        = nodeId;
            mUserIndex     = userIndex;
            mCredential    = cred;
            mHasCredential = hasCred;
        }
    };

    UnlatchContext mUnlatchContext;
    LockRequest mPendingRequest;
    bool mHasPendingRequest = false;
    LockRequest mActiveRemoteAction;
    bool mHasActiveRemoteAction = false;
    LockActuatorState mLockActuatorState = LockActuatorState::kLockCompleted;

private:
    /** @brief Lock-domain init: cluster limits, migration, timers, and initial state. */
    CHIP_ERROR InitLockDomain(chip::app::DataModel::Nullable<chip::app::Clusters::DoorLock::DlLockState> state, LockParam lockParam,
                              chip::PersistentStorageDelegate * storage);

    /** @brief Migrates legacy lock KVS layout when resource limits change. */
    bool MigrateLockConfig(const LockParam & params);

    /** @brief `mLockTimer` callback; posts actuator-movement completion to AppTask. */
    static void TimerEventHandler(void * timerCbArg);

    /** @brief Posts a `kEventType_Lock` event to the AppTask queue. */
    static void PostLockActionEvent(int32_t actor, LockAction action);

    static LockRequest sStagedLockRequest;
    static bool sStagedLockRequestValid;

    static osMutexId_t sLockSharedStateMutex;

    osTimerId_t mLockTimer = nullptr;

    LockParam mLockParams;
    chip::PersistentStorageDelegate * mStorage = nullptr;
};
