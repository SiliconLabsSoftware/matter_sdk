#include "SilabsTracing.h"
#include <cstring>
#include <lib/support/CodeUtils.h>
#include <lib/support/PersistentData.h>

#if !CONFIG_BUILD_FOR_HOST_UNIT_TEST
#include <silabs_utils.h> // for isLogInitialized
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

static constexpr size_t kPersistentTimeTrackerBufferMax = SERIALIZED_TIME_TRACKERS_SIZE_BYTES;

struct TimeTrackerStorage : public TimeTracker, PersistentData<kPersistentTimeTrackerBufferMax>
{
    // TODO implement the Persistent Array class and use it here for logging the watermark array
};

SilabsTracer SilabsTracer::sInstance;

SilabsTracer::SilabsTracer()
{
    Init();
}

void SilabsTracer::Init()
{
    // Initialize the trace buffer and time trackers
    memset(mTraceBuffer, 0, sizeof(mTraceBuffer));
    memset(mWatermark, 0, sizeof(mWatermark));
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
}

void SilabsTracer::TimeTraceEnd(TimeTraceOperation aOperation, CHIP_ERROR error)
{
    if (error != CHIP_NO_ERROR)
    {
        // Log the error, no need to update the watermark for now as we can get the error count from the
        // total count - successfull count. We might want to have a separate count for errors in the future.
        TimeTraceInstant(aOperation);
        return;
    }

    // Calculate the duration and update the time tracker
    auto & tracker   = mWatermark[static_cast<size_t>(aOperation)];
    tracker.mEndTime = System::SystemClock().GetMonotonicTimestamp();
    auto duration    = (tracker.mEndTime - tracker.mStartTime).count();

    tracker.mTotalDuration += System::Clock::Milliseconds64(duration);

    if (duration > tracker.mMaxTimeMs.count())
    {
        tracker.mMaxTimeMs = System::Clock::Milliseconds32(duration);
    }

    if (tracker.mMinTimeMs.count() == 0 || duration < tracker.mMinTimeMs.count())
    {
        tracker.mMinTimeMs = System::Clock::Milliseconds32(duration);
    }

    tracker.mSuccessfullCount++;
    tracker.mMovingAverage = System::Clock::Milliseconds32(tracker.mTotalDuration.count() / tracker.mSuccessfullCount);
    if (duration > tracker.mMovingAverage.count())
    {
        tracker.mCountAboveAvg++;
    }
}

void SilabsTracer::TimeTraceInstant(TimeTraceOperation aOperation, CHIP_ERROR error)
{
    if (CHIP_NO_ERROR == error)
    {
        // Will be used to implement count for instant events that do not require watermarks such as failures and others
        // For now, just log in that order the timestamp and the operation
        ChipLogProgress(DeviceLayer, "Time: %llu, Instant Trace: %u, ", System::SystemClock().GetMonotonicTimestamp().count(),
                        static_cast<uint8_t>(aOperation));
    }
    else
    {
        // Log the error with timestamp and operation
        ChipLogError(DeviceLayer, "Time: %llu, Instant Trace: %u, Error: %" CHIP_ERROR_FORMAT,
                     System::SystemClock().GetMonotonicTimestamp().count(), static_cast<uint8_t>(aOperation), error.Format());
    }
}

void SilabsTracer::OutputTraces()
{
    if (isLogInitialized())
    {
        // Output the traces from the buffer
        for (size_t i = 0; i < kNumTraces; ++i)
        {
            auto & tracker = mWatermark[i];
            ChipLogProgress(DeviceLayer,
                            "Trace %zu: TotalCount=%u, SuccessFullCount=%u, MaxTime=%u, MinTime=%u, AvgTime=%u, CountAboveAvg=%u",
                            i, tracker.mTotalCount, tracker.mSuccessfullCount, tracker.mMaxTimeMs.count(),
                            tracker.mMinTimeMs.count(), tracker.mMovingAverage.count(), tracker.mCountAboveAvg);
        }
    }
    else
    {
        // Buffer the traces if logs are not initialized
        static size_t bufferIndex = 0;
        if (bufferIndex < kBufferedTracesNumber - 1)
        {
            for (size_t i = 0; i < kNumTraces; ++i)
            {
                auto & tracker = mWatermark[i];
                // TODO use c++ copy string
                snprintf(reinterpret_cast<char *>(mTraceBuffer[bufferIndex]), kBufferedTraceSize,
                         "Trace %zu: TotalCount=%u, SuccessFullCount=%u, MaxTime=%u, MinTime=%u, AvgTime=%u, CountAboveAvg=%u", i,
                         tracker.mTotalCount, tracker.mSuccessfullCount, tracker.mMaxTimeMs.count(), tracker.mMinTimeMs.count(),
                         tracker.mMovingAverage.count(), tracker.mCountAboveAvg);
                bufferIndex++;
            }
        }
        else
        {
            snprintf(reinterpret_cast<char *>(mTraceBuffer[bufferIndex]), kBufferedTraceSize, "Buffer overflow: logs were lost");
        }
    }
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

} // namespace Silabs
} // namespace Tracing
} // namespace chip
