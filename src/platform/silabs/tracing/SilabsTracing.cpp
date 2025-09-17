/***************************************************************************
 * @file SilabsTracing.cpp
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
#include "SilabsTracing.h"
#include <cstdio> // for snprintf
#include <cstring>
#include <lib/support/CodeUtils.h>
#include <lib/support/PersistentData.h>
#include <string> // Include the necessary header for std::string

#if defined(SL_RAIL_LIB_MULTIPROTOCOL_SUPPORT) && SL_RAIL_LIB_MULTIPROTOCOL_SUPPORT
#include <rail.h>
// RAIL_GetTime() returns time in usec
#define SILABS_GET_TIME() System::Clock::Milliseconds32(RAIL_GetTime() / 1000)
#define SILABS_GET_DURATION(tracker)                                                                                               \
    (tracker.mEndTime < tracker.mStartTime)                                                                                        \
        ? (tracker.mEndTime + System::Clock::Milliseconds32((UINT32_MAX / 1000)) - tracker.mStartTime)                             \
        : tracker.mEndTime - tracker.mStartTime
#else
#define SILABS_GET_TIME() System::SystemClock().GetMonotonicTimestamp()
#define SILABS_GET_DURATION(tracker) tracker.mEndTime - tracker.mStartTime
#endif

#if defined(SILABS_LOG_OUT_UART) && SILABS_LOG_OUT_UART
#include "uart.h"
#endif

#if !CONFIG_BUILD_FOR_HOST_UNIT_TEST
#include <platform/silabs/Logging.h> // for isLogInitialized
#endif

// The following functions needs to be implemented by the application to allows logging or storing the traces /
// metrics. If the application does not implement them, the traces will simply be ignored.
bool __attribute__((weak)) isLogInitialized()
{
    return false;
}

namespace chip {
namespace Tracing {
namespace Silabs {

int FormatTimeStamp(std::chrono::milliseconds time, MutableCharSpan & buffer)
{
    using namespace std::chrono;
    auto h = duration_cast<hours>(time);
    time -= duration_cast<milliseconds>(h);
    auto m = duration_cast<minutes>(time);
    time -= duration_cast<milliseconds>(m);
    auto s = duration_cast<seconds>(time);
    time -= duration_cast<milliseconds>(s);

    return snprintf(buffer.data(), buffer.size(), "%02d:%02d:%02d.%03d", static_cast<int>(h.count()), static_cast<int>(m.count()),
                    static_cast<int>(s.count()), static_cast<int>(time.count()));
}

namespace {
constexpr size_t kPersistentTimeTrackerBufferMax = SERIALIZED_TIME_TRACKERS_SIZE_BYTES;
} // namespace
struct TimeTrackerStorage : public TimeTracker, PersistentData<kPersistentTimeTrackerBufferMax>
{
    // TODO implement the Persistent Array class and use it here for logging the metric array
};

int TimeTracker::ToCharArray(MutableCharSpan & buffer) const
{
    // Note: We mimic the snprintf behavior where the output of the function is the number of characters that WOULD be
    // written to the buffer regardless of the success or failure of the operation. This is to allow the caller to know
    // the size of the buffer required to store the output by calling this function with a buffer of size 0.
    switch (mType)
    {
    case OperationType::kBegin: {
        int offset = snprintf(buffer.data(), buffer.size(),
                              "TimeTracker - %-8s | %-32s | Status: %" PRIx32 " | Start: ", OperationTypeToString(mType),
                              SilabsTracer::Instance().OperationIndexToString(mOperation), mError.AsInteger());

        MutableCharSpan subSpan;

        if (offset < static_cast<int>(buffer.size()))
            subSpan = buffer.SubSpan(static_cast<size_t>(offset));
        offset += FormatTimeStamp(mStartTime, subSpan);

        return offset;
    }
    case OperationType::kEnd: {
        int offset = snprintf(buffer.data(), buffer.size(),
                              "TimeTracker - %-8s | %-32s | Status: %" PRIx32 " | Start: ", OperationTypeToString(mType),
                              SilabsTracer::Instance().OperationIndexToString(mOperation), mError.AsInteger());

        MutableCharSpan subSpan;

        // Print Start
        if (offset < static_cast<int>(buffer.size()))
            subSpan = buffer.SubSpan(static_cast<size_t>(offset));
        offset += FormatTimeStamp(mStartTime, subSpan);

        // Print End
        if (offset < static_cast<int>(buffer.size()))
            subSpan = buffer.SubSpan(static_cast<size_t>(offset));
        offset += snprintf(subSpan.data(), subSpan.size(), "| End: ");

        if (offset < static_cast<int>(buffer.size()))
            subSpan = buffer.SubSpan(static_cast<size_t>(offset));
        offset += FormatTimeStamp(mEndTime, subSpan);

        // Print Duration
        if (offset < static_cast<int>(buffer.size()))
            subSpan = buffer.SubSpan(static_cast<size_t>(offset));
        offset += snprintf(subSpan.data(), subSpan.size(), "| Duration: ");

        if (offset < static_cast<int>(buffer.size()))
            subSpan = buffer.SubSpan(static_cast<size_t>(offset));
        offset += FormatTimeStamp(mEndTime - mStartTime, subSpan);

        return offset;
    }
    case OperationType::kInstant: {
        int offset = snprintf(buffer.data(), buffer.size(),
                              "TimeTracker - %-8s | %-32s | Status: %" PRIx32 " | Time: ", OperationTypeToString(mType),
                              SilabsTracer::Instance().OperationIndexToString(mOperation), mError.AsInteger());

        MutableCharSpan subSpan;

        if (offset < static_cast<int>(buffer.size()))
            subSpan = buffer.SubSpan(static_cast<size_t>(offset));
        offset += FormatTimeStamp(mStartTime, subSpan);

        return offset;
    }
    default:
        return snprintf(buffer.data(), buffer.size(), "TimeTracker - Unknown operation type");
    }
}

TimeTraceOperation SilabsTracer::StringToTimeTraceOperation(const char * aOperation)
{
    for (size_t i = 0; i < kNumTraces; i++)
    {
        TimeTraceOperation op = static_cast<TimeTraceOperation>(i);
        if (strcmp(aOperation, TimeTraceOperationToString(op)) == 0)
            return op;
    }
    return TimeTraceOperation::kNumTraces;
}

const char * SilabsTracer::OperationIndexToString(size_t aOperationIdx)
{
    if (aOperationIdx < kNumTraces)
    {
        return TimeTraceOperationToString(static_cast<TimeTraceOperation>(aOperationIdx));
    }
    else
    {
        static char buf[chip::Tracing::Silabs::SilabsTracer::NamedTrace::kMaxGroupLength +
                        chip::Tracing::Silabs::SilabsTracer::NamedTrace::kMaxLabelLength + 2];

        size_t namedTraceIdx = aOperationIdx - kNumTraces;
        if (namedTraceIdx >= kMaxNamedTraces) // Validate Index won't be out of bounds
        {
            snprintf(buf, sizeof(buf), "InvalidTrace");
            return buf;
        }

        const auto & trace = mNamedTraces[namedTraceIdx];
        snprintf(buf, sizeof(buf), "%s:%s", trace.group, trace.label);
        return buf;
    }
}

SilabsTracer SilabsTracer::sInstance;

SilabsTracer::SilabsTracer()
{
    VerifyOrDie(Init() == CHIP_NO_ERROR);
}

void SilabsTracer::TraceBufferClear()
{
    // Clear the time tracker list
    while (mTimeTrackerList.head != nullptr)
    {
        mTimeTrackerList.Remove(0);
    }
    mBufferedTrackerCount = 0;
}

CHIP_ERROR SilabsTracer::Init()
{
    TraceBufferClear();

    // Initialize the time trackers
    memset(mLatestTimeTrackers, 0, sizeof(mLatestTimeTrackers));
    memset(mMetrics, 0, sizeof(mMetrics));
    memset(mNamedTraces, 0, sizeof(mNamedTraces));

    return CHIP_NO_ERROR;
}

CHIP_ERROR SilabsTracer::StartMetricsStorage(PersistentStorageDelegate * storage)
{
    VerifyOrReturnError(nullptr != storage, CHIP_ERROR_INCORRECT_STATE);
    mStorage = storage;
    return CHIP_NO_ERROR;
}

CHIP_ERROR SilabsTracer::TimeTraceBegin(TimeTraceOperation aOperation)
{
    // Log the start time of the operation
    auto & tracker = mLatestTimeTrackers[to_underlying(aOperation)];

    // Corner case since no hardware clock is available at this point
    if (aOperation == TimeTraceOperation::kBootup || aOperation == TimeTraceOperation::kSilabsInit)
    {
        tracker.mStartTime = System::Clock::Milliseconds32(0);
    }
    else
    {
        tracker.mStartTime = SILABS_GET_TIME();
    }

    tracker.mOperation = to_underlying(aOperation);
    tracker.mType      = OperationType::kBegin;
    tracker.mError     = CHIP_NO_ERROR;

    auto & metric = mMetrics[to_underlying(aOperation)];
    metric.mTotalCount++;

    return OutputTrace(tracker);
}

CHIP_ERROR SilabsTracer::TimeTraceEnd(TimeTraceOperation aOperation, CHIP_ERROR error)
{
    auto & tracker   = mLatestTimeTrackers[to_underlying(aOperation)];
    tracker.mEndTime = SILABS_GET_TIME();
    tracker.mType    = OperationType::kEnd;
    tracker.mError   = error;

    if (error == CHIP_NO_ERROR)
    {
        // Calculate the duration and update the time tracker
        auto duration = SILABS_GET_DURATION(tracker);

        auto & metric = mMetrics[to_underlying(aOperation)];
        metric.mSuccessfullCount++;
        metric.mMovingAverage = System::Clock::Milliseconds32(
            (metric.mMovingAverage.count() * (metric.mSuccessfullCount - 1) + duration.count()) / metric.mSuccessfullCount);

        // New Max?
        if (duration > metric.mMaxTimeMs)
            metric.mMaxTimeMs = System::Clock::Milliseconds32(duration);

        // New Min?
        if (metric.mMinTimeMs.count() == 0 || duration < metric.mMinTimeMs)
            metric.mMinTimeMs = System::Clock::Milliseconds32(duration);

        // Above Average?
        if (duration > metric.mMovingAverage)
            metric.mCountAboveAvg++;
    }
    return OutputTrace(tracker);
}

CHIP_ERROR SilabsTracer::TimeTraceInstant(TimeTraceOperation aOperation, CHIP_ERROR error)
{
    TimeTracker tracker;

    tracker.mStartTime = SILABS_GET_TIME();
    tracker.mEndTime   = tracker.mStartTime;
    tracker.mOperation = to_underlying(aOperation);
    tracker.mType      = OperationType::kInstant;
    tracker.mError     = error;

    return OutputTrace(tracker);
}

CHIP_ERROR SilabsTracer::TimeTraceInstant(const char * label, const char * group, CHIP_ERROR error)
{
    int16_t mIndex = FindOrCreateTrace(label, group);
    if (mIndex >= 0)
    {
        auto & trace = mNamedTraces[mIndex];

        trace.tracker.mStartTime = SILABS_GET_TIME();
        trace.metric.mTotalCount++;
        trace.tracker.mOperation = kNumTraces + mIndex;
        trace.tracker.mEndTime   = trace.tracker.mStartTime;
        trace.tracker.mType      = OperationType::kInstant;
        trace.tracker.mError     = error;

        return OutputTrace(trace.tracker);
    }
    else
    {
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }
}

CHIP_ERROR SilabsTracer::NamedTraceBegin(const char * label, const char * group)
{
    int16_t mIndex = FindOrCreateTrace(label, group);
    if (mIndex >= 0)
    {
        auto & trace = mNamedTraces[mIndex];

        trace.tracker.mStartTime = SILABS_GET_TIME();
        trace.metric.mTotalCount++;
        trace.tracker.mOperation = kNumTraces + mIndex;
        trace.tracker.mType      = OperationType::kBegin;
        trace.tracker.mError     = CHIP_NO_ERROR;

        return OutputTrace(trace.tracker);
    }
    else
    {
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }
}

CHIP_ERROR SilabsTracer::NamedTraceEnd(const char * label, const char * group)
{
    int16_t mIndex = FindExistingTrace(label, group);
    if (mIndex < 0)
    {
        // Did not find a NamedTraceBegin
        return CHIP_ERROR_NOT_FOUND;
    }

    auto & trace = mNamedTraces[mIndex];
    if (trace.tracker.mType == OperationType::kBegin)
    {
        trace.tracker.mEndTime = SILABS_GET_TIME();
        trace.tracker.mType    = OperationType::kEnd;

        auto duration = SILABS_GET_DURATION(trace.tracker);

        trace.metric.mSuccessfullCount++;
        trace.metric.mMovingAverage = System::Clock::Milliseconds32(
            (trace.metric.mMovingAverage.count() * (trace.metric.mSuccessfullCount - 1) + duration.count()) /
            trace.metric.mSuccessfullCount);

        // New Max?
        if (duration > trace.metric.mMaxTimeMs)
            trace.metric.mMaxTimeMs = duration;

        // New Min?
        if (trace.metric.mSuccessfullCount == 1 || duration < trace.metric.mMinTimeMs)
            trace.metric.mMinTimeMs = duration;

        // Above Average?
        if (duration > trace.metric.mMovingAverage)
            trace.metric.mCountAboveAvg++;

        return OutputTrace(trace.tracker);
    }
    return CHIP_ERROR_NOT_FOUND;
}

CHIP_ERROR SilabsTracer::OutputTimeTracker(const TimeTracker & tracker)
{
    VerifyOrReturnError(isLogInitialized(), CHIP_ERROR_UNINITIALIZED);

    char buffer[kMaxTraceSize];
    MutableCharSpan span(buffer);
    tracker.ToCharArray(span);
    ChipLogProgress(DeviceLayer, "%s", buffer);

    return CHIP_NO_ERROR;
}

CHIP_ERROR SilabsTracer::OutputTrace(const TimeTracker & tracker)
{
    // We allow error here as we want to buffer even if the logs are currently uninitialized
    OutputTimeTracker(tracker);

    if (mBufferedTrackerCount < kMaxBufferedTraces - 1)
    {
        mTimeTrackerList.Insert(tracker);
        mBufferedTrackerCount++;
    }
    else if (mBufferedTrackerCount == kMaxBufferedTraces - 1)
    {
        // Save a tracker with TimeTraceOperation::kNumTraces and CHIP_ERROR_BUFFER_TOO_SMALL to indicate that the
        // buffer is full
        TimeTracker resourceExhaustedTracker = tracker;
        resourceExhaustedTracker.mStartTime  = SILABS_GET_TIME();
        resourceExhaustedTracker.mEndTime    = resourceExhaustedTracker.mStartTime;
        resourceExhaustedTracker.mOperation  = to_underlying(TimeTraceOperation::kBufferFull);
        resourceExhaustedTracker.mType       = OperationType::kInstant;
        resourceExhaustedTracker.mError      = CHIP_ERROR_BUFFER_TOO_SMALL;

        mTimeTrackerList.Insert(resourceExhaustedTracker);
        mBufferedTrackerCount++;

        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }
    else
    {
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }

    return CHIP_NO_ERROR;
}
CHIP_ERROR SilabsTracer::OutputMetric(size_t aOperationIdx)
{
    VerifyOrReturnError(isLogInitialized(), CHIP_ERROR_UNINITIALIZED);
    if (aOperationIdx < kNumTraces)
    {
        ChipLogProgress(DeviceLayer,
                        "| Operation: %-25s| MaxTime:%-5" PRIu32 "| MinTime:%-5" PRIu32 "| AvgTime:%-5" PRIu32
                        "| TotalCount:%-8" PRIu32 ", SuccessFullCount:%-8" PRIu32 "| CountAboveAvg:%-8" PRIu32 "|",
                        TimeTraceOperationToString(static_cast<TimeTraceOperation>(aOperationIdx)),
                        mMetrics[aOperationIdx].mMaxTimeMs.count(), mMetrics[aOperationIdx].mMinTimeMs.count(),
                        mMetrics[aOperationIdx].mMovingAverage.count(), mMetrics[aOperationIdx].mTotalCount,
                        mMetrics[aOperationIdx].mSuccessfullCount, mMetrics[aOperationIdx].mCountAboveAvg);
        return CHIP_NO_ERROR;
    }
    else
    {

        const auto & trace = mNamedTraces[aOperationIdx - kNumTraces];
        ChipLogProgress(DeviceLayer,
                        "| Op: %-15s:%-16s| MaxTime:%-5" PRIu32 "| MinTime:%-5" PRIu32 "| AvgTime:%-5" PRIu32
                        "| TotalCount:%-8" PRIu32 ", SuccessFullCount:%-8" PRIu32 "| CountAboveAvg:%-8" PRIu32 "|",
                        trace.group, trace.label, trace.metric.mMaxTimeMs.count(), trace.metric.mMinTimeMs.count(),
                        trace.metric.mMovingAverage.count(), trace.metric.mTotalCount, trace.metric.mSuccessfullCount,
                        trace.metric.mCountAboveAvg);
        return CHIP_NO_ERROR;
    }

    return CHIP_ERROR_INVALID_ARGUMENT;
}

CHIP_ERROR SilabsTracer::OutputMetric(char * aOperation)
{
    TimeTraceOperation operationKey = StringToTimeTraceOperation(aOperation);
    VerifyOrReturnError(isLogInitialized(), CHIP_ERROR_UNINITIALIZED);
    if (operationKey != TimeTraceOperation::kNumTraces)
    {

        return OutputMetric(to_underlying(operationKey));
    }
    else
    {
        // Check if it is a named trace
        // Parse aOperation as "group:label"
        const char * colon = strchr(aOperation, ':');
        if (colon == nullptr)
        {
            ChipLogError(DeviceLayer, "Invalid Metrics TimeTraceOperation");
            return CHIP_ERROR_INVALID_ARGUMENT;
        }

        size_t groupLen = colon - aOperation;
        size_t labelLen = strlen(colon + 1);

        // Copy group and label into temporary buffers
        char groupBuf[NamedTrace::kMaxGroupLength] = { 0 };
        char labelBuf[NamedTrace::kMaxLabelLength] = { 0 };

        if (groupLen >= sizeof(groupBuf))
            groupLen = sizeof(groupBuf) - 1;
        if (labelLen >= sizeof(labelBuf))
            labelLen = sizeof(labelBuf) - 1;

        memcpy(groupBuf, aOperation, groupLen);
        groupBuf[groupLen] = '\0';
        memcpy(labelBuf, colon + 1, labelLen);
        labelBuf[labelLen] = '\0';

        int16_t idx = FindExistingTrace(labelBuf, groupBuf);
        if (idx < 0)
        {
            ChipLogError(DeviceLayer, "Invalid Metrics TimeTraceOperation");
            return CHIP_ERROR_INVALID_ARGUMENT;
        }
        return OutputMetric(idx + kNumTraces);
    }
    ChipLogError(DeviceLayer, "Invalid Metrics TimeTraceOperation");
    return CHIP_ERROR_INVALID_ARGUMENT;
}

CHIP_ERROR SilabsTracer::OutputAllMetrics()
{
    CHIP_ERROR err;
    for (size_t i = 0; i < kNumTraces; ++i)
    {
        err = OutputMetric(i);
        if (err != CHIP_NO_ERROR)
        {
            return err;
        }
    }
    for (size_t i = 0; i < kMaxNamedTraces; i++)
    {
        if (mNamedTraces[i].labelLen == 0 && mNamedTraces[i].groupLen == 0)
        {
            // Beginning of empty items, can stop printing.
            break;
        }
        err = OutputMetric(i + kNumTraces);
        if (err != CHIP_NO_ERROR)
        {
            return err;
        }
    }

    return err;
}

CHIP_ERROR SilabsTracer::OutputAllCurrentOperations()
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    // Print defined operations
    for (size_t i = 0; i < kNumTraces; i++)
    {
        ChipLogProgress(DeviceLayer, "Operation: %-25s", TimeTraceOperationToString(static_cast<TimeTraceOperation>(i)));
    }

    // Print named operations
    for (size_t i = 0; i < SilabsTracer::kMaxNamedTraces; i++)
    {
        const auto & trace = mNamedTraces[i];

        if (trace.labelLen == 0 && trace.groupLen == 0)
            break; // No more named traces, rest of the array is empty

        ChipLogProgress(DeviceLayer, "Operation: %-15s:%-16s", trace.group, trace.label);
    }

    return err;
}

CHIP_ERROR SilabsTracer::TraceBufferFlushAll()
{
    auto * current = mTimeTrackerList.head;
    while (current != nullptr)
    {
        // We do not want to loose the traces if the logs are not initialized
        ReturnErrorOnFailure(OutputTimeTracker(current->mValue));
        current = current->mpNext;
    }

    TraceBufferClear();
    return CHIP_NO_ERROR;
}

CHIP_ERROR SilabsTracer::TraceBufferFlushByOperation(size_t aOperationIdx)
{
    auto * current = mTimeTrackerList.head;
    auto * prev    = static_cast<chip::SingleLinkedListNode<TimeTracker> *>(nullptr);
    while (current != nullptr)
    {
        if (current->mValue.mOperation == aOperationIdx)
        {
            ReturnErrorOnFailure(OutputTimeTracker(current->mValue));

            if (prev == nullptr)
            {
                mTimeTrackerList.head = current->mpNext;
            }
            else
            {
                prev->mpNext = current->mpNext;
            }

            auto * temp = current;
            current     = current->mpNext;
            free(temp);
            mBufferedTrackerCount--;
        }
        else
        {
            prev    = current;
            current = current->mpNext;
        }
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR SilabsTracer::TraceBufferFlushByOperation(CharSpan & appOperationKey)
{
    size_t index = 0;

    TimeTraceOperation operationKey = StringToTimeTraceOperation(appOperationKey.data());
    VerifyOrReturnError(isLogInitialized(), CHIP_ERROR_UNINITIALIZED);
    if (operationKey != TimeTraceOperation::kNumTraces)
    {
        index = to_underlying(operationKey);
    }
    else
    {
        // Check if it is a named trace
        // Parse appOperationKey as "group:label"
        const char * colon = strchr(appOperationKey.data(), ':');
        if (colon == nullptr)
        {
            ChipLogError(DeviceLayer, "Invalid Flush TimeTraceOperation");
            return CHIP_ERROR_INVALID_ARGUMENT;
        }

        size_t groupLen = colon - appOperationKey.data();
        size_t labelLen = strlen(colon + 1);

        // Copy group and label into temporary buffers
        char groupBuf[NamedTrace::kMaxGroupLength] = { 0 };
        char labelBuf[NamedTrace::kMaxLabelLength] = { 0 };

        if (groupLen >= sizeof(groupBuf))
            groupLen = sizeof(groupBuf) - 1;
        if (labelLen >= sizeof(labelBuf))
            labelLen = sizeof(labelBuf) - 1;

        memcpy(groupBuf, appOperationKey.data(), groupLen);
        groupBuf[groupLen] = '\0';
        memcpy(labelBuf, colon + 1, labelLen);
        labelBuf[labelLen] = '\0';

        int16_t idx = FindExistingTrace(labelBuf, groupBuf);
        if (idx < 0)
        {
            ChipLogError(DeviceLayer, "Invalid Flush TimeTraceOperation");
            return CHIP_ERROR_INVALID_ARGUMENT;
        }
        index = (idx + kNumTraces);
    }
    return SilabsTracer::Instance().TraceBufferFlushByOperation(index);
}

// Save the traces in the NVM
CHIP_ERROR SilabsTracer::SaveMetrics()
{
    VerifyOrReturnError(nullptr != mStorage, CHIP_ERROR_INCORRECT_STATE);
    // TODO implement the save of the metrics in the NVM
    return CHIP_NO_ERROR;
}

CHIP_ERROR SilabsTracer::LoadMetrics()
{
    VerifyOrReturnError(nullptr != mStorage, CHIP_ERROR_INCORRECT_STATE);
    // TODO implement the load of the metrics from the NVM
    return CHIP_NO_ERROR;
}

CHIP_ERROR SilabsTracer::GetTraceByOperation(size_t aOperationIdx, MutableCharSpan & buffer)
{
    auto * current = mTimeTrackerList.head;
    while (current != nullptr)
    {
        if (current->mValue.mOperation == aOperationIdx)
        {
            // Found matching trace, format it
            int required = current->mValue.ToCharArray(buffer);
            if (required > static_cast<int>(buffer.size()))
                return CHIP_ERROR_BUFFER_TOO_SMALL;

            return CHIP_NO_ERROR;
        }

        current = current->mpNext;
    }

    return CHIP_ERROR_NOT_FOUND;
}

// Overload for string-based operation lookup
CHIP_ERROR SilabsTracer::GetTraceByOperation(const char * aOperation, MutableCharSpan & buffer)
{
    TimeTraceOperation operationKey = StringToTimeTraceOperation(aOperation);
    if (operationKey != TimeTraceOperation::kNumTraces)
    {
        return GetTraceByOperation(to_underlying(operationKey), buffer);
    }

    // Check if it is a named trace
    const char * colon = strchr(aOperation, ':');
    if (colon == nullptr)
    {
        ChipLogError(DeviceLayer, "Invalid Trace Operation format");
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    size_t groupLen = colon - aOperation;
    size_t labelLen = strlen(colon + 1);

    // Copy group and label into temporary buffers
    char groupBuf[NamedTrace::kMaxGroupLength] = { 0 };
    char labelBuf[NamedTrace::kMaxLabelLength] = { 0 };

    if (groupLen >= sizeof(groupBuf))
        groupLen = sizeof(groupBuf) - 1;
    if (labelLen >= sizeof(labelBuf))
        labelLen = sizeof(labelBuf) - 1;

    memcpy(groupBuf, aOperation, groupLen);
    groupBuf[groupLen] = '\0';
    memcpy(labelBuf, colon + 1, labelLen);
    labelBuf[labelLen] = '\0';

    int16_t idx = FindExistingTrace(labelBuf, groupBuf);
    if (idx < 0)
    {
        return CHIP_ERROR_NOT_FOUND;
    }

    return GetTraceByOperation(idx + kNumTraces, buffer);
}

int16_t SilabsTracer::FindOrCreateTrace(const char * label, const char * group)
{
    int8_t index = FindExistingTrace(label, group);
    if (index >= 0)
    {
        return index;
    }

    // Find first empty slot
    for (size_t i = 0; i < kMaxNamedTraces; i++)
    {
        if (mNamedTraces[i].labelLen == 0)
        {
            auto & trace    = mNamedTraces[i];
            size_t labelLen = strlen(label);
            size_t groupLen = strlen(group);

            // Truncate label and group if too long
            if (labelLen >= NamedTrace::kMaxLabelLength)
            {
                labelLen = NamedTrace::kMaxLabelLength - 1;
            }
            if (groupLen >= NamedTrace::kMaxGroupLength)
            {
                groupLen = NamedTrace::kMaxGroupLength - 1;
            }

            memcpy(trace.label, label, labelLen);
            trace.label[labelLen] = '\0';
            trace.labelLen        = labelLen;

            memcpy(trace.group, group, groupLen);
            trace.group[groupLen] = '\0';
            trace.groupLen        = groupLen;

            return i;
        }
    }
    return -1;
}

int16_t SilabsTracer::FindExistingTrace(const char * label, const char * group)
{
    for (size_t i = 0; i < kMaxNamedTraces; ++i)
    {
        const auto & t = mNamedTraces[i];
        if (t.labelLen == 0)
            return -1; // empty slot

        // prefix semantics: stored must fit within incoming, then bytes must match
        if (std::memcmp(t.group, group, t.groupLen) == 0 && std::memcmp(t.label, label, t.labelLen) == 0)
        {
            return i;
        }
    }
    return -1;
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
