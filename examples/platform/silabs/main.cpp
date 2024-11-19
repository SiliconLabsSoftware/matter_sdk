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
#include "sl_component_catalog.h"
#include "sl_system_init.h"
#include "sl_system_kernel.h"
#include <MatterConfig.h>

#include <matter/tracing/build_config.h>
#if MATTER_TRACING_ENABLED
#include <platform/silabs/tracing/SilabsTracing.h>
#endif // MATTER_TRACING_ENABLED

#if MATTER_TRACING_ENABLED
using TimeTraceOperation = chip::Tracing::Silabs::TimeTraceOperation;
using SilabsTracer       = chip::Tracing::Silabs::SilabsTracer;
#endif // MATTER_TRACING_ENABLED

int main(void)
{
#if MATTER_TRACING_ENABLED
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kBootup);
#endif // MATTER_TRACING_ENABLED
#if MATTER_TRACING_ENABLED
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kSilabsInit);
#endif // MATTER_TRACING_ENABLED
    sl_system_init();
#if MATTER_TRACING_ENABLED
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kSilabsInit);
#endif // MATTER_TRACING_ENABLED
#if MATTER_TRACING_ENABLED
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kMatterInit);
#endif // MATTER_TRACING_ENABLED
    // Initialize the application. For example, create periodic timer(s) or
    // task(s) if the kernel is present.
    SilabsMatterConfig::AppInit();
}
