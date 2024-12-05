/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
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

#include "BindingHandler.h"

#include "AppConfig.h"
#include "app/CommandSender.h"
#include "app/clusters/bindings/BindingManager.h"
#include "app/server/Server.h"
#include "controller/InvokeInteraction.h"
#include "platform/CHIPDeviceLayer.h"
#include <app/clusters/bindings/bindings.h>
#include <lib/support/CodeUtils.h>

using namespace chip;
using namespace chip::app;
using chip::app::Clusters::LevelControl::MoveModeEnum;
using chip::app::Clusters::LevelControl::OptionsBitmap;
using chip::app::Clusters::LevelControl::StepModeEnum;

namespace {

void ProcessOnOffUnicastBindingCommand(CommandId commandId, const EmberBindingTableEntry & binding,
                                       Messaging::ExchangeManager * exchangeMgr, const SessionHandle & sessionHandle)
{
    auto onSuccess = [](const ConcreteCommandPath & commandPath, const StatusIB & status, const auto & dataResponse) {
        ChipLogProgress(NotSpecified, "OnOff command succeeds");
    };

    auto onFailure = [](CHIP_ERROR error) {
        ChipLogError(NotSpecified, "OnOff command failed: %" CHIP_ERROR_FORMAT, error.Format());
    };

    switch (commandId)
    {
    case Clusters::OnOff::Commands::Toggle::Id:
        Clusters::OnOff::Commands::Toggle::Type toggleCommand;
        Controller::InvokeCommandRequest(exchangeMgr, sessionHandle, binding.remote, toggleCommand, onSuccess, onFailure);
        break;

    case Clusters::OnOff::Commands::On::Id:
        Clusters::OnOff::Commands::On::Type onCommand;
        Controller::InvokeCommandRequest(exchangeMgr, sessionHandle, binding.remote, onCommand, onSuccess, onFailure);
        break;

    case Clusters::OnOff::Commands::Off::Id:
        Clusters::OnOff::Commands::Off::Type offCommand;
        Controller::InvokeCommandRequest(exchangeMgr, sessionHandle, binding.remote, offCommand, onSuccess, onFailure);
        break;
    }
}

void ProcessOnOffGroupBindingCommand(CommandId commandId, const EmberBindingTableEntry & binding)
{
    Messaging::ExchangeManager & exchangeMgr = Server::GetInstance().GetExchangeManager();

    switch (commandId)
    {
    case Clusters::OnOff::Commands::Toggle::Id:
        Clusters::OnOff::Commands::Toggle::Type toggleCommand;
        Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId, toggleCommand);
        break;

    case Clusters::OnOff::Commands::On::Id:
        Clusters::OnOff::Commands::On::Type onCommand;
        Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId, onCommand);

        break;

    case Clusters::OnOff::Commands::Off::Id:
        Clusters::OnOff::Commands::Off::Type offCommand;
        Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId, offCommand);
        break;
    }
}

void ProcessLevelControlUnicastBindingCommand(BindingCommandData * data, const EmberBindingTableEntry & binding,
                                              OperationalDeviceProxy * peer_device)
{
    auto onSuccess = [](const ConcreteCommandPath & commandPath, const StatusIB & status, const auto & dataResponse) {
        ChipLogProgress(NotSpecified, "LevelControl command succeeds");
    };

    auto onFailure = [](CHIP_ERROR error) {
        ChipLogError(NotSpecified, "LevelControl command failed: %" CHIP_ERROR_FORMAT, error.Format());
    };

    VerifyOrDie(peer_device != nullptr && peer_device->ConnectionReady());

    Clusters::LevelControl::Commands::MoveToLevel::Type moveToLevelCommand;
    Clusters::LevelControl::Commands::Move::Type moveCommand;
    Clusters::LevelControl::Commands::Step::Type stepCommand;
    Clusters::LevelControl::Commands::Stop::Type stopCommand;
    Clusters::LevelControl::Commands::MoveToLevelWithOnOff::Type moveToLevelWithOnOffCommand;
    Clusters::LevelControl::Commands::MoveWithOnOff::Type moveWithOnOffCommand;
    Clusters::LevelControl::Commands::StepWithOnOff::Type stepWithOnOffCommand;
    Clusters::LevelControl::Commands::StopWithOnOff::Type stopWithOnOffCommand;

    switch (data->commandId)
    {
    case Clusters::LevelControl::Commands::MoveToLevel::Id:
        moveToLevelCommand.level           = static_cast<uint8_t>(data->args[0]);
        moveToLevelCommand.transitionTime  = static_cast<DataModel::Nullable<uint16_t>>(data->args[1]);
        moveToLevelCommand.optionsMask     = static_cast<chip::BitMask<OptionsBitmap>>(data->args[2]);
        moveToLevelCommand.optionsOverride = static_cast<chip::BitMask<OptionsBitmap>>(data->args[3]);
        Controller::InvokeCommandRequest(peer_device->GetExchangeManager(), peer_device->GetSecureSession().Value(), binding.remote,
                                         moveToLevelCommand, onSuccess, onFailure);
        break;

    case Clusters::LevelControl::Commands::Move::Id:
        moveCommand.moveMode        = static_cast<MoveModeEnum>(data->args[0]);
        moveCommand.rate            = static_cast<DataModel::Nullable<uint8_t>>(data->args[1]);
        moveCommand.optionsMask     = static_cast<chip::BitMask<OptionsBitmap>>(data->args[2]);
        moveCommand.optionsOverride = static_cast<chip::BitMask<OptionsBitmap>>(data->args[3]);
        Controller::InvokeCommandRequest(peer_device->GetExchangeManager(), peer_device->GetSecureSession().Value(), binding.remote,
                                         moveCommand, onSuccess, onFailure);
        break;

    case Clusters::LevelControl::Commands::Step::Id:
        stepCommand.stepMode        = static_cast<StepModeEnum>(data->args[0]);
        stepCommand.stepSize        = static_cast<uint8_t>(data->args[1]);
        stepCommand.transitionTime  = static_cast<DataModel::Nullable<uint16_t>>(data->args[2]);
        stepCommand.optionsMask     = static_cast<chip::BitMask<OptionsBitmap>>(data->args[3]);
        stepCommand.optionsOverride = static_cast<chip::BitMask<OptionsBitmap>>(data->args[4]);
        Controller::InvokeCommandRequest(peer_device->GetExchangeManager(), peer_device->GetSecureSession().Value(), binding.remote,
                                         stepCommand, onSuccess, onFailure);
        break;

    case Clusters::LevelControl::Commands::Stop::Id:
        stopCommand.optionsMask     = static_cast<chip::BitMask<OptionsBitmap>>(data->args[0]);
        stopCommand.optionsOverride = static_cast<chip::BitMask<OptionsBitmap>>(data->args[1]);
        Controller::InvokeCommandRequest(peer_device->GetExchangeManager(), peer_device->GetSecureSession().Value(), binding.remote,
                                         stopCommand, onSuccess, onFailure);
        break;

    case Clusters::LevelControl::Commands::MoveToLevelWithOnOff::Id:
        moveToLevelWithOnOffCommand.level           = static_cast<uint8_t>(data->args[0]);
        moveToLevelWithOnOffCommand.transitionTime  = static_cast<DataModel::Nullable<uint16_t>>(data->args[1]);
        moveToLevelWithOnOffCommand.optionsMask     = static_cast<chip::BitMask<OptionsBitmap>>(data->args[2]);
        moveToLevelWithOnOffCommand.optionsOverride = static_cast<chip::BitMask<OptionsBitmap>>(data->args[3]);
        Controller::InvokeCommandRequest(peer_device->GetExchangeManager(), peer_device->GetSecureSession().Value(), binding.remote,
                                         moveToLevelWithOnOffCommand, onSuccess, onFailure);
        break;

    case Clusters::LevelControl::Commands::MoveWithOnOff::Id:
        moveWithOnOffCommand.moveMode        = static_cast<MoveModeEnum>(data->args[0]);
        moveWithOnOffCommand.rate            = static_cast<DataModel::Nullable<uint8_t>>(data->args[1]);
        moveWithOnOffCommand.optionsMask     = static_cast<chip::BitMask<OptionsBitmap>>(data->args[2]);
        moveWithOnOffCommand.optionsOverride = static_cast<chip::BitMask<OptionsBitmap>>(data->args[3]);
        Controller::InvokeCommandRequest(peer_device->GetExchangeManager(), peer_device->GetSecureSession().Value(), binding.remote,
                                         moveWithOnOffCommand, onSuccess, onFailure);
        break;

    case Clusters::LevelControl::Commands::StepWithOnOff::Id:
        stepWithOnOffCommand.stepMode        = static_cast<StepModeEnum>(data->args[0]);
        stepWithOnOffCommand.stepSize        = static_cast<uint8_t>(data->args[1]);
        stepWithOnOffCommand.transitionTime  = static_cast<DataModel::Nullable<uint16_t>>(data->args[2]);
        stepWithOnOffCommand.optionsMask     = static_cast<chip::BitMask<OptionsBitmap>>(data->args[3]);
        stepWithOnOffCommand.optionsOverride = static_cast<chip::BitMask<OptionsBitmap>>(data->args[4]);
        Controller::InvokeCommandRequest(peer_device->GetExchangeManager(), peer_device->GetSecureSession().Value(), binding.remote,
                                         stepWithOnOffCommand, onSuccess, onFailure);
        break;

    case Clusters::LevelControl::Commands::StopWithOnOff::Id:
        stopWithOnOffCommand.optionsMask     = static_cast<chip::BitMask<OptionsBitmap>>(data->args[0]);
        stopWithOnOffCommand.optionsOverride = static_cast<chip::BitMask<OptionsBitmap>>(data->args[1]);
        Controller::InvokeCommandRequest(peer_device->GetExchangeManager(), peer_device->GetSecureSession().Value(), binding.remote,
                                         stopWithOnOffCommand, onSuccess, onFailure);
        break;
    }
}

void ProcessLevelControlGroupBindingCommand(BindingCommandData * data, const EmberBindingTableEntry & binding)
{
    Messaging::ExchangeManager & exchangeMgr = Server::GetInstance().GetExchangeManager();

    Clusters::LevelControl::Commands::MoveToLevel::Type moveToLevelCommand;
    Clusters::LevelControl::Commands::Move::Type moveCommand;
    Clusters::LevelControl::Commands::Step::Type stepCommand;
    Clusters::LevelControl::Commands::Stop::Type stopCommand;
    Clusters::LevelControl::Commands::MoveToLevelWithOnOff::Type moveToLevelWithOnOffCommand;
    Clusters::LevelControl::Commands::MoveWithOnOff::Type moveWithOnOffCommand;
    Clusters::LevelControl::Commands::StepWithOnOff::Type stepWithOnOffCommand;
    Clusters::LevelControl::Commands::StopWithOnOff::Type stopWithOnOffCommand;

    switch (data->commandId)
    {
    case Clusters::LevelControl::Commands::MoveToLevel::Id:
        moveToLevelCommand.level           = static_cast<uint8_t>(data->args[0]);
        moveToLevelCommand.transitionTime  = static_cast<DataModel::Nullable<uint16_t>>(data->args[1]);
        moveToLevelCommand.optionsMask     = static_cast<chip::BitMask<OptionsBitmap>>(data->args[2]);
        moveToLevelCommand.optionsOverride = static_cast<chip::BitMask<OptionsBitmap>>(data->args[3]);
        Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId, moveToLevelCommand);
        break;

    case Clusters::LevelControl::Commands::Move::Id:
        moveCommand.moveMode        = static_cast<MoveModeEnum>(data->args[0]);
        moveCommand.rate            = static_cast<DataModel::Nullable<uint8_t>>(data->args[1]);
        moveCommand.optionsMask     = static_cast<chip::BitMask<OptionsBitmap>>(data->args[2]);
        moveCommand.optionsOverride = static_cast<chip::BitMask<OptionsBitmap>>(data->args[3]);
        Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId, moveCommand);
        break;

    case Clusters::LevelControl::Commands::Step::Id:
        stepCommand.stepMode        = static_cast<StepModeEnum>(data->args[0]);
        stepCommand.stepSize        = static_cast<uint8_t>(data->args[1]);
        stepCommand.transitionTime  = static_cast<DataModel::Nullable<uint16_t>>(data->args[2]);
        stepCommand.optionsMask     = static_cast<chip::BitMask<OptionsBitmap>>(data->args[3]);
        stepCommand.optionsOverride = static_cast<chip::BitMask<OptionsBitmap>>(data->args[4]);
        Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId, stepCommand);
        break;

    case Clusters::LevelControl::Commands::Stop::Id:
        stopCommand.optionsMask     = static_cast<chip::BitMask<OptionsBitmap>>(data->args[0]);
        stopCommand.optionsOverride = static_cast<chip::BitMask<OptionsBitmap>>(data->args[1]);
        Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId, stopCommand);
        break;

    case Clusters::LevelControl::Commands::MoveToLevelWithOnOff::Id:
        moveToLevelWithOnOffCommand.level           = static_cast<uint8_t>(data->args[0]);
        moveToLevelWithOnOffCommand.transitionTime  = static_cast<DataModel::Nullable<uint16_t>>(data->args[1]);
        moveToLevelWithOnOffCommand.optionsMask     = static_cast<chip::BitMask<OptionsBitmap>>(data->args[2]);
        moveToLevelWithOnOffCommand.optionsOverride = static_cast<chip::BitMask<OptionsBitmap>>(data->args[3]);
        Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId, moveToLevelWithOnOffCommand);
        break;

    case Clusters::LevelControl::Commands::MoveWithOnOff::Id:
        moveWithOnOffCommand.moveMode        = static_cast<MoveModeEnum>(data->args[0]);
        moveWithOnOffCommand.rate            = static_cast<DataModel::Nullable<uint8_t>>(data->args[1]);
        moveWithOnOffCommand.optionsMask     = static_cast<chip::BitMask<OptionsBitmap>>(data->args[2]);
        moveWithOnOffCommand.optionsOverride = static_cast<chip::BitMask<OptionsBitmap>>(data->args[3]);
        Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId, moveWithOnOffCommand);
        break;

    case Clusters::LevelControl::Commands::StepWithOnOff::Id:
        stepWithOnOffCommand.stepMode        = static_cast<StepModeEnum>(data->args[0]);
        stepWithOnOffCommand.stepSize        = static_cast<uint8_t>(data->args[1]);
        stepWithOnOffCommand.transitionTime  = static_cast<DataModel::Nullable<uint16_t>>(data->args[2]);
        stepWithOnOffCommand.optionsMask     = static_cast<chip::BitMask<OptionsBitmap>>(data->args[3]);
        stepWithOnOffCommand.optionsOverride = static_cast<chip::BitMask<OptionsBitmap>>(data->args[4]);
        Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId, stepWithOnOffCommand);
        break;

    case Clusters::LevelControl::Commands::StopWithOnOff::Id:
        stopWithOnOffCommand.optionsMask     = static_cast<chip::BitMask<OptionsBitmap>>(data->args[0]);
        stopWithOnOffCommand.optionsOverride = static_cast<chip::BitMask<OptionsBitmap>>(data->args[1]);
        Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId, stopWithOnOffCommand);
        break;
    }
}

void LightSwitchChangedHandler(const EmberBindingTableEntry & binding, OperationalDeviceProxy * peer_device, void * context)
{
    VerifyOrReturn(context != nullptr, ChipLogError(NotSpecified, "OnDeviceConnectedFn: context is null"));
    BindingCommandData * data = static_cast<BindingCommandData *>(context);

    if (binding.type == MATTER_MULTICAST_BINDING && data->isGroup)
    {
        switch (data->clusterId)
        {
        case Clusters::OnOff::Id:
            ProcessOnOffGroupBindingCommand(data->commandId, binding);
            break;
        case Clusters::LevelControl::Id:
            ProcessLevelControlGroupBindingCommand(data, binding);
            break;
        }
    }
    else if (binding.type == MATTER_UNICAST_BINDING && !data->isGroup)
    {
        switch (data->clusterId)
        {
        case Clusters::OnOff::Id:
            VerifyOrDie(peer_device != nullptr && peer_device->ConnectionReady());
            ProcessOnOffUnicastBindingCommand(data->commandId, binding, peer_device->GetExchangeManager(),
                                              peer_device->GetSecureSession().Value());
            break;
        case Clusters::LevelControl::Id:
            ProcessLevelControlUnicastBindingCommand(data, binding, peer_device);
            break;
        }
    }
}

void LightSwitchContextReleaseHandler(void * context)
{
    VerifyOrReturn(context != nullptr, ChipLogError(NotSpecified, "LightSwitchContextReleaseHandler: context is null"));
    Platform::Delete(static_cast<BindingCommandData *>(context));
}

void InitBindingHandlerInternal(intptr_t arg)
{
    auto & server = chip::Server::GetInstance();
    chip::BindingManager::GetInstance().Init(
        { &server.GetFabricTable(), server.GetCASESessionManager(), &server.GetPersistentStorage() });
    chip::BindingManager::GetInstance().RegisterBoundDeviceChangedHandler(LightSwitchChangedHandler);
    chip::BindingManager::GetInstance().RegisterBoundDeviceContextReleaseHandler(LightSwitchContextReleaseHandler);
}

} // namespace

/********************************************************
 * Switch functions
 *********************************************************/

void SwitchWorkerFunction(intptr_t context)
{
    VerifyOrReturn(context != 0, ChipLogError(NotSpecified, "SwitchWorkerFunction - Invalid work data"));

    BindingCommandData * data = reinterpret_cast<BindingCommandData *>(context);
    BindingManager::GetInstance().NotifyBoundClusterChanged(data->localEndpointId, data->clusterId, static_cast<void *>(data));

    Platform::Delete(data);
}

void BindingWorkerFunction(intptr_t context)
{
    VerifyOrReturn(context != 0, ChipLogError(NotSpecified, "BindingWorkerFunction - Invalid work data"));

    EmberBindingTableEntry * entry = reinterpret_cast<EmberBindingTableEntry *>(context);
    AddBindingEntry(*entry);

    Platform::Delete(entry);
}

CHIP_ERROR InitBindingHandler()
{
    // The initialization of binding manager will try establishing connection with unicast peers
    // so it requires the Server instance to be correctly initialized. Post the init function to
    // the event queue so that everything is ready when initialization is conducted.
    chip::DeviceLayer::PlatformMgr().ScheduleWork(InitBindingHandlerInternal);
    return CHIP_NO_ERROR;
}
