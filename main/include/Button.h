/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
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

/**
 * This is part of the implementation for sending commands to Matter devices with a button
 * Note that this is only used for debugging and not actually part of the functionality of the bridge
 */

#pragma once

#include "driver/gpio.h"
#include "esp_log.h"

// Button class used for the implementation of a button connected to the microcontroller
class Button
{
public:
    typedef void (*ButtonPressCallback)(void);

    void Init(void);
    void SetButtonPressCallback(ButtonPressCallback button_callback);
};