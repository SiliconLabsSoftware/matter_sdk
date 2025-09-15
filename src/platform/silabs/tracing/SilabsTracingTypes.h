/***************************************************************************
 * @file SilabsTracing.h
 * @brief Instrumenting for matter operation tracing for the Silicon Labs platform.
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/
#pragma once

#include <stdint.h>

namespace chip {
namespace Tracing {
namespace Silabs {

// Enum for the different operation to trace
enum class TimeTraceOperation : uint8_t
{
    kSpake2p,
    kPake1,
    kPake2,
    kPake3,
    kOperationalCredentials,
    kAttestationVerification,
    kCSR,
    kNOC,
    kTransportLayer,
    kTransportSetup,
    kFindOperational,
    kCaseSession,
    kSigma1,
    kSigma2,
    kSigma3,
    kOTA,
    kImageUpload,
    kImageVerification,
    kAppApplyTime,
    kBootup,
    kSilabsInit,
    kMatterInit,
    kAppInit,
    kBufferFull,
    kNumTraces,
    kUnknown,
};

enum class OperationType : uint8_t
{
    kBegin,
    kEnd,
    kInstant,
};

TimeTraceOperation StringToTimeTraceOperation(const char * str)
{
    for (auto ttOp = 0; ttOp < to_underlying(TimeTraceOperation::kNumTraces); ttOp++)
    {
        TimeTraceOperation op = static_cast<TimeTraceOperation>(ttOp);
        if (strcmp(str, TimeTraceOperationToString(op)) == 0)
        {
            return op;
        }
    }
    return TimeTraceOperation::kNumTraces;
}
const char * TimeTraceOperationToString(TimeTraceOperation operation)
{
    switch (operation)
    {
    case TimeTraceOperation::kSpake2p:
        return "Spake2p";
    case TimeTraceOperation::kPake1:
        return "Pake1";
    case TimeTraceOperation::kPake2:
        return "Pake2";
    case TimeTraceOperation::kPake3:
        return "Pake3";
    case TimeTraceOperation::kOperationalCredentials:
        return "OperationalCredentials";
    case TimeTraceOperation::kAttestationVerification:
        return "AttestationVerification";
    case TimeTraceOperation::kCSR:
        return "CSR";
    case TimeTraceOperation::kNOC:
        return "NOC";
    case TimeTraceOperation::kTransportLayer:
        return "TransportLayer";
    case TimeTraceOperation::kTransportSetup:
        return "TransportSetup";
    case TimeTraceOperation::kFindOperational:
        return "FindOperational";
    case TimeTraceOperation::kCaseSession:
        return "CaseSession";
    case TimeTraceOperation::kSigma1:
        return "Sigma1";
    case TimeTraceOperation::kSigma2:
        return "Sigma2";
    case TimeTraceOperation::kSigma3:
        return "Sigma3";
    case TimeTraceOperation::kOTA:
        return "OTA";
    case TimeTraceOperation::kImageUpload:
        return "ImageUpload";
    case TimeTraceOperation::kImageVerification:
        return "ImageVerification";
    case TimeTraceOperation::kAppApplyTime:
        return "AppApplyTime";
    case TimeTraceOperation::kBootup:
        return "Bootup";
    case TimeTraceOperation::kSilabsInit:
        return "SilabsInit";
    case TimeTraceOperation::kMatterInit:
        return "MatterInit";
    case TimeTraceOperation::kAppInit:
        return "AppInit";
    case TimeTraceOperation::kNumTraces:
        return "NumTraces";
    case TimeTraceOperation::kBufferFull:
        return "BufferFull";
    default:
        return "Unknown";
    }
}

const char * TimeTraceOperationToString(size_t operation)
{
    if (operation >= to_underlying(TimeTraceOperation::kNumTraces))
    {
        size_t index = operation - to_underlying(TimeTraceOperation::kNumTraces);
        VerifyOrReturnValue(index < SilabsTracer::kMaxAppOperationKeys, "Unknown");
        return SilabsTracer::Instance().GetAppOperationKey(index);
    }
    else
    {
        return TimeTraceOperationToString(static_cast<TimeTraceOperation>(operation));
    }
}

const char * OperationTypeToString(OperationType type)
{
    switch (type)
    {
    case OperationType::kBegin:
        return "Begin";
    case OperationType::kEnd:
        return "End";
    case OperationType::kInstant:
        return "Instant";
    default:
        return "Unknown";
    }
}

} // namespace Silabs
} // namespace Tracing
} // namespace chip
