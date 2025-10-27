/*
 *  Oven Binding Handler - derived from light-switch-app BindingHandler
 */
#pragma once

#include <app-common/zap-generated/ids/Clusters.h>
#include <app-common/zap-generated/ids/Commands.h>
#include <lib/core/CHIPError.h>
#include <lib/core/CHIPCallback.h>
#include <app/util/basic-types.h>

struct OnOffBindingContext
{
    chip::EndpointId localEndpointId;
    chip::CommandId commandId;
};

// Initialize binding manager (schedules init work)
CHIP_ERROR InitOvenBindingHandler();

// Notify that CookTop OnOff cluster changed using pre-filled context
void CookTopOnOffBindingTrigger(OnOffBindingContext * context);
