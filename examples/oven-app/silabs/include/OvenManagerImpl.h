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

#pragma once

#include "CRTPHelpers.h"
#include "OvenManager.h"

/**
 * @brief CRTP base for oven OvenManager, exposing override hooks for customizable APIs.
 *
 * Each public method dispatches to `Derived::*Impl()`. Overrides are optional: default
 * `*Impl()` implementations in the private section forward to `OvenManager`. Override in
 * `Derived` only for the behaviors you want to customize.
 *
 * @tparam Derived The derived class type (CRTP pattern)
 */
template <typename Derived>
class OvenManagerImpl : public OvenManager
{
public:
    // Initialize OvenManager and associated endpoints
    void Init() { CRTP_OPTIONAL_VOID_DISPATCH(OvenManagerImpl, Derived, InitImpl); }

    // Handle OnOff cluster attribute changes
    void OnOffAttributeChangeHandler(chip::EndpointId endpointId, chip::AttributeId attributeId, uint8_t * value, uint16_t size)
    {
        CRTP_OPTIONAL_VOID_DISPATCH(OvenManagerImpl, Derived, OnOffAttributeChangeHandlerImpl, endpointId, attributeId, value,
                                    size);
    }

    // Handle OvenMode cluster attribute changes
    void OvenModeAttributeChangeHandler(chip::EndpointId endpointId, chip::AttributeId attributeId, uint8_t * value, uint16_t size)
    {
        CRTP_OPTIONAL_VOID_DISPATCH(OvenManagerImpl, Derived, OvenModeAttributeChangeHandlerImpl, endpointId, attributeId, value,
                                    size);
    }

    // Check whether a transition between two oven modes is blocked
    bool IsTransitionBlocked(uint8_t fromMode, uint8_t toMode) override
    {
        CRTP_OPTIONAL_DISPATCH_ARGS(OvenManagerImpl, Derived, IsTransitionBlockedImpl, fromMode, toMode);
    }

private:
    friend Derived;

    // Default *Impl() hooks, each forwards to the matching OvenManager method
    // Override the corresponding hook in CustomerAppManager to customize behavior

    void InitImpl() { OvenManager::Init(); }

    void OnOffAttributeChangeHandlerImpl(chip::EndpointId endpointId, chip::AttributeId attributeId, uint8_t * value, uint16_t size)
    {
        OvenManager::OnOffAttributeChangeHandler(endpointId, attributeId, value, size);
    }

    void OvenModeAttributeChangeHandlerImpl(chip::EndpointId endpointId, chip::AttributeId attributeId, uint8_t * value,
                                            uint16_t size)
    {
        OvenManager::OvenModeAttributeChangeHandler(endpointId, attributeId, value, size);
    }

    bool IsTransitionBlockedImpl(uint8_t fromMode, uint8_t toMode) { return OvenManager::IsTransitionBlocked(fromMode, toMode); }
};

/**
 * @brief Common alias so the shared CustomerAppManager resolves per-app.
 *
 * The shared CustomerAppManager includes "AppManagerImpl.h" and derives 
 * from `AppManagerImpl<CustomerAppManager>`.
 */
template <typename D>
using AppManagerImpl = OvenManagerImpl<D>;
