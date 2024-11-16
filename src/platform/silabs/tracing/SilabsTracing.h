/*
 *    Copyright (c) 2024 Project CHIP Authors
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

#include <cstddef>
#include <cstdlib>
#include <lib/core/CHIPError.h>
#include <lib/core/CHIPPersistentStorageDelegate.h>
#include <lib/support/LinkedList.h>
#include <stdint.h>
#include <system/SystemClock.h>

#ifndef SERIALIZED_TIME_TRACKERS_SIZE_BYTES
// Default size, watermarks store 6 uint32_t, which is 24 bytes
// We currently have 19 operations to track, so 19 * 24 = 456 bytes
// 512 bytes should be enough including the serialization overhead
#define SERIALIZED_TIME_TRACKERS_SIZE_BYTES 512
#endif

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
    kNumTraces,
};

enum class OperationType : uint8_t
{
    kBegin,
    kEnd,
    kInstant,
};

struct TimeTracker
{
    // Temporary values
    System::Clock::Milliseconds32 mStartTime;
    System::Clock::Milliseconds32 mEndTime;
    TimeTraceOperation mOperation;
    OperationType mType;
    CHIP_ERROR mError;
};

struct Watermark
{
    // Values that will be stored in the NVM
    System::Clock::Milliseconds32 mMovingAverage; // Successful operation average time
    System::Clock::Milliseconds32 mMaxTimeMs;     // Successful operation max time
    System::Clock::Milliseconds32 mMinTimeMs;     // Successful operation min time
    uint32_t mTotalCount;                         // Total number of times the operation was initiated
    uint32_t mSuccessfullCount;                   // Number of times the operation was completed without error
    uint32_t mCountAboveAvg;                      // Number of times the operation was above the average time
};

// This class instantiates a buffer to store the traces before logs are enabled.
//
class SilabsTracer
{
    static constexpr size_t kNumTraces         = to_underlying(TimeTraceOperation::kNumTraces);
    static constexpr size_t kMaxBufferedTraces = 64;

public:
    static SilabsTracer & Instance() { return sInstance; }

    CHIP_ERROR Init();

    CHIP_ERROR StartWatermarksStorage(PersistentStorageDelegate * storage);

    void TimeTraceBegin(TimeTraceOperation aOperation);
    void TimeTraceEnd(TimeTraceOperation aOperation, CHIP_ERROR error = CHIP_NO_ERROR);
    void TimeTraceInstant(TimeTraceOperation aOperation, CHIP_ERROR error = CHIP_NO_ERROR);

    // This Either Logs the traces or stores them in a buffer
    CHIP_ERROR OutputTimeTracker(const TimeTracker & tracker);
    CHIP_ERROR OutputWaterMark(TimeTraceOperation aOperation);
    CHIP_ERROR OutputAllWaterMarks();
    CHIP_ERROR TraceBufferFlushAll();
    CHIP_ERROR TraceBufferFlushByOperation(TimeTraceOperation aOperation);

    // prevent copy constructor and assignment operator
    SilabsTracer(SilabsTracer const &)             = delete;
    SilabsTracer & operator=(SilabsTracer const &) = delete;

    // Methods to get the time trackers metrics values
    TimeTracker GetTimeTracker(TimeTraceOperation aOperation) { return mLatestTimeTrackers[to_underlying(aOperation)]; }
    Watermark GetWatermark(TimeTraceOperation aOperation) { return mWatermarks[to_underlying(aOperation)]; }

    // Method to save the time trackers in the NVM, this will likely be time consuming and should not be called frequently
    CHIP_ERROR SaveWatermarks();

    // Method to load the time trackers from the NVM, this will be called after the initialization so the bootup watermarks need to
    // be added to the loaded values
    CHIP_ERROR LoadWatermarks();

#if CONFIG_BUILD_FOR_HOST_UNIT_TEST
    CHIP_ERROR GetTraceCount(size_t & count) const;
    CHIP_ERROR GetTraceByIndex(size_t index, const char *& trace) const;
#endif

private:
    struct TimeTrackerList
    {
        chip::SingleLinkedListNode<TimeTracker> * head = nullptr;

        void Insert(const TimeTracker & tracker)
        {
            auto * newNode =
                static_cast<chip::SingleLinkedListNode<TimeTracker> *>(malloc(sizeof(chip::SingleLinkedListNode<TimeTracker>)));
            if (newNode != nullptr)
            {
                newNode->mValue = tracker;
                newNode->mpNext = head;
                head            = newNode;
            }
        }

        void Remove(size_t index)
        {
            if (index == 0 && head != nullptr)
            {
                auto * temp = head;
                head        = head->mpNext;
                free(temp);
                return;
            }

            auto * current = head;
            for (size_t i = 0; i < index - 1 && current != nullptr; ++i)
            {
                current = current->mpNext;
            }

            if (current && current->mpNext)
            {
                auto * temp     = current->mpNext;
                current->mpNext = current->mpNext->mpNext;
                free(temp);
            }
        }
    };

    // Singleton class with a static instance
    static SilabsTracer sInstance;

    SilabsTracer();

    // List of past time trackers
    TimeTrackerList mTimeTrackerList;

    // Time trackers to store time stamps for ongoing operations
    TimeTracker mLatestTimeTrackers[kNumTraces];

    // Watermarks for each operation
    Watermark mWatermarks[kNumTraces];

    PersistentStorageDelegate * mStorage = nullptr;

    size_t mBufferedTrackerCount = 0;

    void TraceBufferClear();
    CHIP_ERROR OutputTrace(const TimeTracker & tracker);
};

} // namespace Silabs
} // namespace Tracing
} // namespace chip
