#include "SilabsTracing.h"
#include <cstring>
#include <lib/support/CodeUtils.h>
#include <lib/support/PersistentData.h>

#if !CONFIG_BUILD_FOR_HOST_UNIT_TEST
#include <Logging.h> // for isLogInitialized
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

// TODO add string mapping to log the operation names and types

static constexpr size_t kPersistentTimeTrackerBufferMax = SERIALIZED_TIME_TRACKERS_SIZE_BYTES;

struct TimeTrackerStorage : public TimeTracker, PersistentData<kPersistentTimeTrackerBufferMax>
{
    // TODO implement the Persistent Array class and use it here for logging the watermark array
};

SilabsTracer SilabsTracer::sInstance;

SilabsTracer::SilabsTracer()
{
    VerifyOrDie(Init() == CHIP_NO_ERROR);
}

CHIP_ERROR SilabsTracer::TraceBufferClear()
{
    // Free the allocated memory for the trace buffer
    if (nullptr != mTraceBuffer)
    {
        for (size_t i = 0; i < kBufferedTracesNumber; ++i)
        {
            free(mTraceBuffer[i]);
        }
        free(mTraceBuffer);
        mTraceBuffer = nullptr;
        mBufferIndex = 0;
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR SilabsTracer::Init()
{
    // Allows to use Init as a Reset function
    ReturnErrorOnFailure(TraceBufferClear());

    // Allocate memory for the trace buffer
    mTraceBuffer = static_cast<uint8_t **>(malloc(kBufferedTracesNumber * sizeof(uint8_t *)));
    VerifyOrReturnError(nullptr != mTraceBuffer, CHIP_ERROR_NO_MEMORY);

    for (size_t i = 0; i < kBufferedTracesNumber; ++i)
    {
        mTraceBuffer[i] = static_cast<uint8_t *>(malloc(kBufferedTraceSize * sizeof(uint8_t)));
        VerifyOrReturnError(nullptr != mTraceBuffer[i], CHIP_ERROR_NO_MEMORY);
        memset(mTraceBuffer[i], 0, kBufferedTraceSize);
    }

    // Initialize the time trackers
    memset(mWatermark, 0, sizeof(mWatermark));

    return CHIP_NO_ERROR;
}

CHIP_ERROR SilabsTracer::StartLogging()
{
    return TraceBufferFlush();
}

CHIP_ERROR SilabsTracer::StartTraceStorage(PersistentStorageDelegate * storage)
{
    VerifyOrReturnError(nullptr != storage, CHIP_ERROR_INCORRECT_STATE);
    mStorage = storage;
    return CHIP_NO_ERROR;
}

void SilabsTracer::TimeTraceBegin(TimeTraceOperation aOperation)
{
    // Log the start time of the operation
    auto & tracker = mWatermark[static_cast<size_t>(aOperation)];
    tracker.mTotalCount++;
    tracker.mStartTime = System::SystemClock().GetMonotonicTimestamp();
    OutputTrace(aOperation, OperationType::kBegin, CHIP_NO_ERROR);
}

void SilabsTracer::TimeTraceEnd(TimeTraceOperation aOperation, CHIP_ERROR error)
{
    if (error != CHIP_NO_ERROR)
    {
        OutputTrace(aOperation, OperationType::kEnd, error);
        return;
    }

    // Calculate the duration and update the time tracker
    auto & tracker   = mWatermark[static_cast<size_t>(aOperation)];
    tracker.mEndTime = System::SystemClock().GetMonotonicTimestamp();
    auto duration    = tracker.mEndTime - tracker.mStartTime;

    tracker.mTotalDuration += System::Clock::Milliseconds64(duration);

    if (duration > tracker.mMaxTimeMs)
    {
        tracker.mMaxTimeMs = System::Clock::Milliseconds32(duration);
    }

    if (tracker.mMinTimeMs.count() == 0 || duration < tracker.mMinTimeMs)
    {
        tracker.mMinTimeMs = System::Clock::Milliseconds32(duration);
    }

    tracker.mSuccessfullCount++;
    tracker.mMovingAverage = System::Clock::Milliseconds32(tracker.mTotalDuration / tracker.mSuccessfullCount);
    if (duration > tracker.mMovingAverage)
    {
        tracker.mCountAboveAvg++;
    }

    OutputTrace(aOperation, OperationType::kEnd, CHIP_NO_ERROR);
}

void SilabsTracer::TimeTraceInstant(TimeTraceOperation aOperation, CHIP_ERROR error)
{
    OutputTrace(aOperation, OperationType::kInstant, error);
}

CHIP_ERROR SilabsTracer::OutputTrace(TimeTraceOperation aOperation, OperationType type, CHIP_ERROR error)
{
    uint64_t timestamp = System::SystemClock().GetMonotonicTimestamp().count();
    uint8_t operation  = static_cast<uint8_t>(aOperation);
    uint8_t opType     = static_cast<uint8_t>(type);

    if (isLogInitialized())
    {
        if (error == CHIP_NO_ERROR)
        {
            ChipLogProgress(DeviceLayer, "Time: %" PRIu64 ", Operation: %" PRIu32 ", Type: %" PRIu32 "", timestamp, operation,
                            opType);
        }
        else
        {
            ChipLogError(DeviceLayer, "Time: %" PRIu64 ", Operation: %" PRIu32 ", Type: %" PRIu32 ", Error: %" CHIP_ERROR_FORMAT,
                         timestamp, operation, opType, error.Format());
            return CHIP_ERROR_INTERNAL;
        }
    }
    else
    {
        if (mBufferIndex < kBufferedTracesNumber - 1)
        {
            if (error == CHIP_NO_ERROR)
            {
                snprintf(reinterpret_cast<char *>(mTraceBuffer[mBufferIndex]), kBufferedTraceSize,
                         "Time: %" PRIu64 ", Operation: %" PRIu32 ", Type: %" PRIu32 "", timestamp, operation, opType);
            }
            else
            {
                snprintf(reinterpret_cast<char *>(mTraceBuffer[mBufferIndex]), kBufferedTraceSize,
                         "Time: %" PRIu64 ", Operation: %" PRIu32 ", Type: %" PRIu32 ", Error: %" CHIP_ERROR_FORMAT, timestamp,
                         operation, opType, error.Format());
            }
            mBufferIndex++;
        }
        else
        {
            snprintf(reinterpret_cast<char *>(mTraceBuffer[mBufferIndex]), kBufferedTraceSize, "Buffer overflow: logs were lost");
        }
    }

    return CHIP_NO_ERROR;
}

void SilabsTracer::BufferTrace(size_t index, const TimeTracker & tracker, CHIP_ERROR error)
{
    if (mBufferIndex < kBufferedTracesNumber - 1)
    {
        if (error == CHIP_NO_ERROR)
        {
            snprintf(reinterpret_cast<char *>(mTraceBuffer[mBufferIndex]), kBufferedTraceSize,
                     "Trace %zu: TotalCount=%" PRIu32 ", SuccessFullCount=%" PRIu32 ", MaxTime=%" PRIu32 ", MinTime=%" PRIu32
                     ", AvgTime=%" PRIu32 ", CountAboveAvg=%" PRIu32 "",
                     index, tracker.mTotalCount, tracker.mSuccessfullCount, tracker.mMaxTimeMs.count(), tracker.mMinTimeMs.count(),
                     tracker.mMovingAverage.count(), tracker.mCountAboveAvg);
        }
        else
        {
            snprintf(reinterpret_cast<char *>(mTraceBuffer[mBufferIndex]), kBufferedTraceSize,
                     "Trace %zu: Error: %" CHIP_ERROR_FORMAT, index, error.Format());
        }
        mBufferIndex++;
    }
    else
    {
        snprintf(reinterpret_cast<char *>(mTraceBuffer[mBufferIndex]), kBufferedTraceSize, "Buffer overflow: logs were lost");
    }
}

CHIP_ERROR SilabsTracer::OutputWaterMark(TimeTraceOperation aOperation, CHIP_ERROR error)
{
    size_t index   = static_cast<size_t>(aOperation);
    auto & tracker = mWatermark[index];

    if (isLogInitialized())
    {
        if (error == CHIP_NO_ERROR)
        {
            ChipLogProgress(DeviceLayer,
                            "Trace %zu: TotalCount=%" PRIu32 ", SuccessFullCount=%" PRIu32 ", MaxTime=%" PRIu32 ", MinTime=%" PRIu32
                            ", AvgTime=%" PRIu32 ", CountAboveAvg=%" PRIu32 "",
                            index, tracker.mTotalCount, tracker.mSuccessfullCount, tracker.mMaxTimeMs.count(),
                            tracker.mMinTimeMs.count(), tracker.mMovingAverage.count(), tracker.mCountAboveAvg);
        }
        else
        {
            ChipLogError(DeviceLayer, "Trace %zu: Error: %" CHIP_ERROR_FORMAT, index, error.Format());
            return CHIP_ERROR_INTERNAL;
        }
    }
    else
    {
        BufferTrace(index, tracker, error);
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR SilabsTracer::OutputAllWaterMarks()
{
    for (size_t i = 0; i < kNumTraces; ++i)
    {
        CHIP_ERROR err = OutputWaterMark(static_cast<TimeTraceOperation>(i), CHIP_NO_ERROR);
        if (err != CHIP_NO_ERROR)
        {
            return err;
        }
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR SilabsTracer::TraceBufferFlush()
{
    if (isLogInitialized() && (nullptr != mTraceBuffer))
    {
        for (size_t i = 0; i < kBufferedTracesNumber; ++i)
        {
            if (mTraceBuffer[i][0] != '\0') // Check if the buffer is not empty
            {
                ChipLogProgress(DeviceLayer, "%s", mTraceBuffer[i]);
            }
        }
    }

    return TraceBufferClear();
}

// Save the traces in the NVM
CHIP_ERROR SilabsTracer::SaveWatermarks()
{
    VerifyOrReturnError(nullptr != mStorage, CHIP_ERROR_INCORRECT_STATE);
    // TODO implement the save of the watermarks in the NVM
    return CHIP_NO_ERROR;
}

CHIP_ERROR SilabsTracer::LoadWatermarks()
{
    VerifyOrReturnError(nullptr != mStorage, CHIP_ERROR_INCORRECT_STATE);
    // TODO implement the load of the watermarks from the NVM
    return CHIP_NO_ERROR;
}

#if CONFIG_BUILD_FOR_HOST_UNIT_TEST
CHIP_ERROR SilabsTracer::GetTraceCount(size_t & count) const
{
    VerifyOrReturnError(nullptr != mTraceBuffer, CHIP_ERROR_INCORRECT_STATE);
    count = mBufferIndex;
    return CHIP_NO_ERROR;
}

CHIP_ERROR SilabsTracer::GetTraceByIndex(size_t index, const char *& trace) const
{
    VerifyOrReturnError(nullptr != mTraceBuffer, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(index < mBufferIndex, CHIP_ERROR_INVALID_ARGUMENT);
    trace = reinterpret_cast<const char *>(mTraceBuffer[index]);
    return CHIP_NO_ERROR;
}
#endif

} // namespace Silabs
} // namespace Tracing
} // namespace chip
