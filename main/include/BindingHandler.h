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

#pragma once

#include "app-common/zap-generated/ids/Clusters.h"
#include "app-common/zap-generated/ids/Commands.h"
#include "lib/core/CHIPError.h"
#include <variant>
#include <memory>

CHIP_ERROR InitBindingHandler();
void SwitchWorkerFunction(intptr_t context);
void BindingWorkerFunction(intptr_t context);

// Global pointer that contains the result of a read interaction
extern std::unique_ptr<std::variant<uint16_t, bool>> result_ptr;

// Type definition for data that can be given to a write interaction
typedef std::variant<uint16_t, bool> Data;

// Struct that is used as the data in combination with bindings
struct BindingCommandData
{
    chip::EndpointId localEndpointId = 2;
    chip::CommandId commandId;
    chip::AttributeId attributeId;
    chip::ClusterId clusterId;
    Data data;
    bool readAttribute = false;
    bool writeAttribute = false;
    bool isGroup = false;
};
