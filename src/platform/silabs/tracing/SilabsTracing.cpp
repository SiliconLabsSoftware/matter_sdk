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
    memset(mTimeTrackers, 0, sizeof(mTimeTrackers));
    memset(mWatermarks, 0, sizeof(mWatermarks));

    return CHIP_NO_ERROR;
}

CHIP_ERROR SilabsTracer::StartWatermarksStorage(PersistentStorageDelegate * storage)
{
    VerifyOrReturnError(nullptr != storage, CHIP_ERROR_INCORRECT_STATE);
    mStorage = storage;
    return CHIP_NO_ERROR;
}

void SilabsTracer::TimeTraceBegin(TimeTraceOperation aOperation)
{
    // Log the start time of the operation
    auto & tracker     = mTimeTrackers[static_cast<size_t>(aOperation)];
    tracker.mStartTime = System::SystemClock().GetMonotonicTimestamp();
    tracker.mOperation = aOperation;
    tracker.mType      = OperationType::kBegin;
    tracker.mError     = CHIP_NO_ERROR;

    auto & watermark = mWatermarks[static_cast<size_t>(aOperation)];
    watermark.mTotalCount++;

    OutputTrace(tracker);
}

void SilabsTracer::TimeTraceEnd(TimeTraceOperation aOperation, CHIP_ERROR error)
{
    auto & tracker   = mTimeTrackers[static_cast<size_t>(aOperation)];
    tracker.mEndTime = System::SystemClock().GetMonotonicTimestamp();
    tracker.mType    = OperationType::kEnd;
    tracker.mError   = error;

    if (error == CHIP_NO_ERROR)
    {
        // Calculate the duration and update the time tracker
        auto duration = tracker.mEndTime - tracker.mStartTime;

        auto & watermark = mWatermarks[static_cast<size_t>(aOperation)];
        watermark.mSuccessfullCount++;
        watermark.mMovingAverage = System::Clock::Milliseconds32(
            (watermark.mMovingAverage.count() * (watermark.mSuccessfullCount - 1) + duration.count()) /
            watermark.mSuccessfullCount);

        if (duration > watermark.mMaxTimeMs)
        {
            watermark.mMaxTimeMs = System::Clock::Milliseconds32(duration);
        }

        if (watermark.mMinTimeMs.count() == 0 || duration < watermark.mMinTimeMs)
        {
            watermark.mMinTimeMs = System::Clock::Milliseconds32(duration);
        }

        if (duration > watermark.mMovingAverage)
        {
            watermark.mCountAboveAvg++;
        }
    }

    OutputTrace(tracker);
}

void SilabsTracer::TimeTraceInstant(TimeTraceOperation aOperation, CHIP_ERROR error)
{
    auto & tracker     = mTimeTrackers[static_cast<size_t>(aOperation)];
    tracker.mStartTime = System::SystemClock().GetMonotonicTimestamp();
    tracker.mEndTime   = tracker.mStartTime;
    tracker.mOperation = aOperation;
    tracker.mType      = OperationType::kInstant;
    tracker.mError     = error;
    OutputTrace(tracker);
}

CHIP_ERROR SilabsTracer::OutputTimeTracker(const TimeTracker & tracker)
{
    VerifyOrReturnError(isLogInitialized(), CHIP_ERROR_UNINITIALIZED);
    ChipLogProgress(DeviceLayer,
                    "TimeTracker - StartTime: %" PRIu32 ", EndTime: %" PRIu32 ", Operation: %" PRIu8 ", Type: %" PRIu8
                    ", Error: %" CHIP_ERROR_FORMAT,
                    tracker.mStartTime.count(), tracker.mEndTime.count(), static_cast<uint8_t>(tracker.mOperation),
                    static_cast<uint8_t>(tracker.mType), tracker.mError.Format());
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
    else
    {
        // Save a tracker with 0xFFFF to indicate that the buffer is full
        TimeTracker resourceExhaustedTracker = tracker;
        resourceExhaustedTracker.mStartTime  = System::SystemClock().GetMonotonicTimestamp();
        resourceExhaustedTracker.mEndTime    = resourceExhaustedTracker.mStartTime;
        resourceExhaustedTracker.mOperation  = TimeTraceOperation::kNumTraces;
        resourceExhaustedTracker.mType       = OperationType::kInstant;
        resourceExhaustedTracker.mError      = CHIP_ERROR_BUFFER_TOO_SMALL;
        mTimeTrackerList.Insert(resourceExhaustedTracker);
        return CHIP_ERROR_BUFFER_TOO_SMALL;
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR SilabsTracer::OutputWaterMark(TimeTraceOperation aOperation)
{
    size_t index     = static_cast<size_t>(aOperation);
    auto & watermark = mWatermarks[index];

    VerifyOrReturnError(isLogInitialized(), CHIP_ERROR_UNINITIALIZED);
    ChipLogProgress(DeviceLayer,
                    "Trace %zu: TotalCount=%" PRIu32 ", SuccessFullCount=%" PRIu32 ", MaxTime=%" PRIu32 ", MinTime=%" PRIu32
                    ", AvgTime=%" PRIu32 ", CountAboveAvg=%" PRIu32 "",
                    index, watermark.mTotalCount, watermark.mSuccessfullCount, watermark.mMaxTimeMs.count(),
                    watermark.mMinTimeMs.count(), watermark.mMovingAverage.count(), watermark.mCountAboveAvg);

    return CHIP_NO_ERROR;
}

CHIP_ERROR SilabsTracer::OutputAllWaterMarks()
{
    for (size_t i = 0; i < kNumTraces; ++i)
    {
        CHIP_ERROR err = OutputWaterMark(static_cast<TimeTraceOperation>(i));
        if (err != CHIP_NO_ERROR)
        {
            return err;
        }
    }
    return CHIP_NO_ERROR;
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

CHIP_ERROR SilabsTracer::TraceBufferFlushByOperation(TimeTraceOperation aOperation)
{

    auto * current = mTimeTrackerList.head;
    auto * prev    = static_cast<chip::SingleLinkedListNode<TimeTracker> *>(nullptr);
    while (current != nullptr)
    {
        if (current->mValue.mOperation == aOperation)
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
    count = mBufferedTrackerCount;
    return CHIP_NO_ERROR;
}

CHIP_ERROR SilabsTracer::GetTraceByIndex(size_t index, const char *& trace) const
{
    auto * current = mTimeTrackerList.head;
    for (size_t i = 0; i < index && current != nullptr; ++i)
    {
        current = current->mpNext;
    }
    VerifyOrReturnError(current != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    trace = reinterpret_cast<const char *>(&current->mValue);
    return CHIP_NO_ERROR;
}
#endif

} // namespace Silabs
} // namespace Tracing
} // namespace chip
