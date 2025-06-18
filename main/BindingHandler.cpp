/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
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

#include "BindingHandler.h"
#include "app/CommandSender.h"
#include "app/clusters/bindings/BindingManager.h"
#include "app/server/Server.h"
#include "controller/InvokeInteraction.h"
#include "controller/ReadInteraction.h"
#include "controller/WriteInteraction.h"
#include "platform/CHIPDeviceLayer.h"
#include <app/clusters/bindings/bindings.h>
#include <lib/support/CodeUtils.h>
#include <variant>
#include <optional>

using namespace chip;
using namespace chip::app;

// Globally defined pointer
// This pointer will contain the result of a Read action
std::unique_ptr<std::variant<uint16_t, bool>> result_ptr = nullptr;

namespace {

/**
 * Function used to send a write interaction to a cluster in the binding table
 */
template <typename T>
void ProcessWriteAttribute(ClusterId clusterId, AttributeId attributeId, T& value, const EmberBindingTableEntry & binding,
                                        Messaging::ExchangeManager * exchangeMgr, const SessionHandle & sessionHandle)
{
    auto onSuccess = [](const app::ConcreteAttributePath &) {
        ChipLogProgress(NotSpecified, "Write Attribute Success");
    };

    auto onError = [](const app::ConcreteAttributePath * path, CHIP_ERROR err) {
        ChipLogError(NotSpecified, "Write Attribute Failure: %" CHIP_ERROR_FORMAT, err.Format());
    };

    auto onDone = [](app::WriteClient *) {
        ChipLogProgress(NotSpecified, "Write Attribute Done");
    };

    Controller::WriteAttribute<T>(sessionHandle, binding.remote, clusterId, attributeId, value, onSuccess, onError, NullOptional, onDone);
}

/**
 * Function used to send a read interaction to a cluster in the binding table
 * The result of the read interaction gets written into the global result pointer
 */
template <typename T>
void ProcessReadAttribute(ClusterId clusterId, AttributeId attributeId, const EmberBindingTableEntry & binding,
                                       Messaging::ExchangeManager * exchangeMgr, const SessionHandle & sessionHandle)
{
    auto onSuccess = [](const ConcreteDataAttributePath & attributePath, const auto & dataResponse) {
        result_ptr = std::make_unique<std::variant<uint16_t, bool>>(dataResponse);
        ChipLogProgress(NotSpecified, "Read attribute succeeded");
    };

    auto onFailure = [](const ConcreteDataAttributePath * attributePath, CHIP_ERROR error) {
        ChipLogError(NotSpecified, "Read attribute failed: %" CHIP_ERROR_FORMAT, error.Format());
    };
    Controller::ReadAttribute<T>(exchangeMgr, sessionHandle, binding.remote, clusterId, attributeId, onSuccess, onFailure);
}

/**
 * Function used to send a unicast invoke interaction to a cluster in the binding table
 * Note that currently no function is provided to flexibly send such requests
 */
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

/**
 * Function used to send a multicast invoke interaction to a group in the binding table
 * Note that currently no function is provided to flexibly send such requests
 */
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

/**
 * Function called to process a interaction in connection with a binding
 */
void StateChangedHandler(const EmberBindingTableEntry & binding, OperationalDeviceProxy * peer_device, void * context)
{
    ChipLogProgress(DeviceLayer, "Light Switch Changed Handler - Status Changes!");
    VerifyOrReturn(context != nullptr, ChipLogError(NotSpecified, "OnDeviceConnectedFn: context is null"));
    BindingCommandData * data = static_cast<BindingCommandData *>(context);
    
    // Check if a write interaction should be used
    if (data->writeAttribute) {
        // Check which data type should be used
        if (std::holds_alternative<uint16_t>(data->data)) {
            ProcessWriteAttribute(data->clusterId, data->attributeId, std::get<uint16_t>(data->data), binding, peer_device->GetExchangeManager(), peer_device->GetSecureSession().Value());
        } else if (std::holds_alternative<bool>(data->data)) {
            ProcessWriteAttribute(data->clusterId, data->attributeId, std::get<bool>(data->data), binding, peer_device->GetExchangeManager(), peer_device->GetSecureSession().Value());
        }
    }
    // Check if a read interaction should be used
    else if (data->readAttribute) {
        // Check which data type should be used
        if (std::holds_alternative<uint16_t>(data->data)) {
            ProcessReadAttribute<uint16_t>(data->clusterId, data->attributeId, binding, peer_device->GetExchangeManager(), peer_device->GetSecureSession().Value());
        } else if (std::holds_alternative<bool>(data->data)) {
            ProcessReadAttribute<bool>(data->clusterId, data->attributeId, binding, peer_device->GetExchangeManager(), peer_device->GetSecureSession().Value());
        }
    }
    // Check if a unicast invoke interaction should be used
    else if (binding.type == EMBER_UNICAST_BINDING && !data->isGroup)
    {
        switch (data->clusterId)
        {
        case Clusters::OnOff::Id:
            VerifyOrDie(peer_device != nullptr && peer_device->ConnectionReady());
            ProcessOnOffUnicastBindingCommand(data->commandId, binding, peer_device->GetExchangeManager(),
                                              peer_device->GetSecureSession().Value());
            break;
        }
    }
    // Check if a multicast invoke interaction should be used
    else if (binding.type == EMBER_MULTICAST_BINDING && data->isGroup)
    {
        switch (data->clusterId)
        {
        case Clusters::OnOff::Id:
            ProcessOnOffGroupBindingCommand(data->commandId, binding);
            break;
        }
    }
}

/**
 * Callback function used to delete the BindingCommandData
 */
void ContextReleaseHandler(void * context)
{
    VerifyOrReturn(context != nullptr, ChipLogError(NotSpecified, "Invalid context for Light switch context release handler"));

    Platform::Delete(static_cast<BindingCommandData *>(context));
}

/**
 * Function used to initialize the binding handler
 */
void InitBindingHandlerInternal(intptr_t arg)
{
    auto & server = chip::Server::GetInstance();
    chip::BindingManager::GetInstance().Init(
        { &server.GetFabricTable(), server.GetCASESessionManager(), &server.GetPersistentStorage() });
    chip::BindingManager::GetInstance().RegisterBoundDeviceChangedHandler(StateChangedHandler);
    chip::BindingManager::GetInstance().RegisterBoundDeviceContextReleaseHandler(ContextReleaseHandler);
}

} // namespace

/**
 * Worker function that can be invoked with BindingCommandData to send a read, write or invoke interaction to a cluster
 */
void SwitchWorkerFunction(intptr_t context)
{
    VerifyOrReturn(context != 0, ChipLogError(NotSpecified, "SwitchWorkerFunction - Invalid work data"));
    BindingCommandData * data = reinterpret_cast<BindingCommandData *>(context);
    BindingManager::GetInstance().NotifyBoundClusterChanged(data->localEndpointId, data->clusterId, static_cast<void *>(data));
}

/**
 * Worker function for the binding cluster used to add new entries
 */
void BindingWorkerFunction(intptr_t context)
{
    VerifyOrReturn(context != 0, ChipLogError(NotSpecified, "BindingWorkerFunction - Invalid work data"));
    EmberBindingTableEntry * entry = reinterpret_cast<EmberBindingTableEntry *>(context);
    AddBindingEntry(*entry);

    Platform::Delete(entry);
}

/**
 * Function used to initialize the binding handler
 */
CHIP_ERROR InitBindingHandler()
{
    // The initialization of binding manager will try establishing connection with unicast peers
    // so it requires the Server instance to be correctly initialized. Post the init function to
    // the event queue so that everything is ready when initialization is conducted.
    chip::DeviceLayer::PlatformMgr().ScheduleWork(InitBindingHandlerInternal);
    return CHIP_NO_ERROR;
}
