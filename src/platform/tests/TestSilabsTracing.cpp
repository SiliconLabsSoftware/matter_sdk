/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
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
 *    @file
 *      This file implements a unit test suite for the Platform Time
 *      code functionality.
 *
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pw_unit_test/framework.h>

#include <lib/core/StringBuilderAdapters.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/UnitTestUtils.h>
#include <system/SystemClock.h>

#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <platform/silabs/tracing/SilabsTracing.h>

using namespace chip;
using namespace chip::Logging;
using namespace chip::System;
using namespace chip::System::Clock::Literals;

using namespace chip::Tracing::Silabs;

namespace {
chip::System::Clock::Internal::MockClock gMockClock;
chip::System::Clock::ClockBase * gRealClock;
} // namespace

// =================================
//      Unit tests
// =================================

class TestSilabsTracing : public ::testing::Test
{
public:
    static void SetUpTestSuite()
    {
        ASSERT_EQ(chip::Platform::MemoryInit(), CHIP_NO_ERROR);

        gRealClock = &chip::System::SystemClock();
        chip::System::Clock::Internal::SetSystemClockForTesting(&gMockClock);
    }

    static void TearDownTestSuite()
    {
        chip::System::Clock::Internal::SetSystemClockForTesting(gRealClock);
        chip::Platform::MemoryShutdown();
    }
};

TEST_F(TestSilabsTracing, TestTimeTrackerMethods)
{
    gMockClock.SetMonotonic(0_ms64);
    SilabsTracer::Instance().Init();

    // Start tracking time for a specific event
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kSpake2p);
    // Simulate some delay or work
    gMockClock.AdvanceMonotonic(100_ms64);
    // Stop tracking time for the event
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kSpake2p);

    // Retrieve the tracked time
    auto timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kSpake2p);
    auto trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();

    // Check that the tracked time is within an expected range
    EXPECT_EQ(trackedTime, uint32_t(100));

    // Verify the count, moving average, highest, and lowest functionalities
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(trackedTime));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(trackedTime));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(trackedTime));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    // Repeat and verify the count and moving average, high and low got updated properly
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kSpake2p);
    gMockClock.AdvanceMonotonic(150_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kSpake2p);

    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kSpake2p);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(150));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(2));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(125));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(150));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(2));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(1));

    // Repeat for another event to verify multiple tracking works
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kPake1);
    gMockClock.AdvanceMonotonic(50_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kPake1);

    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kPake1);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(50));

    // Verify the count, moving average, highest, and lowest functionalities
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), trackedTime);
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), trackedTime);
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), trackedTime);
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    // Repeat Again for first event to verify multiple tracking works
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kSpake2p);
    gMockClock.AdvanceMonotonic(200_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kSpake2p);

    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kSpake2p);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(200));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(3));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(150));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(200));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(3));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(2));

    // Verify a double start to simulate a failure
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kSpake2p);
    gMockClock.AdvanceMonotonic(150_ms64);
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kSpake2p);
    gMockClock.AdvanceMonotonic(110_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kSpake2p);

    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kSpake2p);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(110));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(5));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(140));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(200));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(4));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(2));
}

TEST_F(TestSilabsTracing, TestBootupSequence)
{
    gMockClock.SetMonotonic(0_ms64);
    SilabsTracer::Instance().Init();

    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kBootup);
    // Simulate Silabs Init
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kSilabsInit);
    gMockClock.AdvanceMonotonic(200_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kSilabsInit);

    // Simulate Matter Init
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kMatterInit);
    gMockClock.AdvanceMonotonic(300_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kMatterInit);

    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kBootup);

    // Verify the time tracker values for each operation
    auto timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kBootup);
    auto trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(500));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(500));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(500));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(500));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kSilabsInit);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(200));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(200));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(200));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(200));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kMatterInit);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(300));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(300));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(300));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(300));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    // Simulate a second simulation where we have a reboot during Silabs Init
    gMockClock.SetMonotonic(0_ms64);
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kBootup);

    // Simulate Silabs Init
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kSilabsInit);
    gMockClock.AdvanceMonotonic(150_ms64);

    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kBootup);
    // Simulate Silabs Init
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kSilabsInit);
    gMockClock.AdvanceMonotonic(350_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kSilabsInit);

    // Simulate Matter Init
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kMatterInit);
    gMockClock.AdvanceMonotonic(250_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kMatterInit);

    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kBootup);

    // Verify the time tracker values for each operation after reboot
    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kBootup);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(600));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(3));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(550));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(600));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(500));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(2));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(1));

    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kSilabsInit);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(350));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(3));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(275));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(350));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(200));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(2));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(1));

    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kMatterInit);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(250));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(2));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(275));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(300));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(250));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(2));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));
}

TEST_F(TestSilabsTracing, TestCommissioning)
{
    gMockClock.SetMonotonic(0_ms64);
    SilabsTracer::Instance().Init();

    // Simulate Spake2p steps
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kSpake2p);
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kPake1);
    gMockClock.AdvanceMonotonic(50_ms64);
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kPake1);
    gMockClock.AdvanceMonotonic(100_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kPake1);

    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kPake2);
    gMockClock.AdvanceMonotonic(150_ms64);
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kPake2);
    gMockClock.AdvanceMonotonic(200_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kPake2);

    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kPake3);
    gMockClock.AdvanceMonotonic(200_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kPake3);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kSpake2p);

    // Verify the time tracker values for Spake2p
    auto timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kSpake2p);
    auto trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(700));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(700));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(700));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(700));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    // Verify the time tracker values for Pake1
    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kPake1);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(100));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(2));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    // Verify the time tracker values for Pake2
    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kPake2);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(200));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(2));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(200));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(200));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(200));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    // Verify the time tracker values for Pake3
    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kPake3);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(200));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(200));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(200));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(200));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    // Simulate Operational Credentials steps
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kOperationalCredentials);
    gMockClock.AdvanceMonotonic(300_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kOperationalCredentials);

    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kOperationalCredentials);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(300));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(300));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(300));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(300));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    // Simulate Transport Layer steps
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kTransportLayer);
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kTransportSetup);
    gMockClock.AdvanceMonotonic(100_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kTransportSetup);

    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kFindOperational);
    gMockClock.AdvanceMonotonic(150_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kFindOperational);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kTransportLayer);

    // Verify the time tracker values for Transport Layer
    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kTransportLayer);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(250));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(250));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(250));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(250));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    // Verify the time tracker values for Transport Setup
    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kTransportSetup);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(100));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    // Verify the time tracker values for Find Operational
    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kFindOperational);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(150));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(150));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(150));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(150));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    // Simulate Case Session steps
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kCaseSession);
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kSigma1);
    gMockClock.AdvanceMonotonic(100_ms64);
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kSigma1);
    gMockClock.AdvanceMonotonic(100_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kSigma1);

    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kSigma2);
    gMockClock.AdvanceMonotonic(150_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kSigma2);

    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kSigma3);
    gMockClock.AdvanceMonotonic(200_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kSigma3);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kCaseSession);

    // Verify the time tracker values for Case Session
    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kCaseSession);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(550));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(550));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(550));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(550));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    // Verify the time tracker values for Sigma1
    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kSigma1);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(100));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(2));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    // Verify the time tracker values for Sigma2
    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kSigma2);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(150));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(150));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(150));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(150));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    // Verify the time tracker values for Sigma3
    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kSigma3);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(200));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(200));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(200));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(200));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));
}

TEST_F(TestSilabsTracing, TestOTA)
{
    gMockClock.SetMonotonic(0_ms64);
    SilabsTracer::Instance().Init();

    // Simulate OTA steps
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kOTA);
    gMockClock.AdvanceMonotonic(100_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kOTA);

    // Verify the time tracker values for OTA
    auto timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kOTA);
    auto trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(100));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    // Simulate OTA steps with failure
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kOTA);
    gMockClock.AdvanceMonotonic(150_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kOTA, CHIP_ERROR_INTERNAL);

    // Verify the time tracker values for OTA after failure
    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kOTA);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(0));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(2));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    // Simulate Bootup steps after OTA failure
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kBootup);
    gMockClock.AdvanceMonotonic(200_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kBootup);

    // Verify the time tracker values for Bootup
    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kBootup);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(200));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(200));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(200));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(200));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(1));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(0));

    // Simulate subsequent OTA steps that succeed
    SilabsTracer::Instance().TimeTraceBegin(TimeTraceOperation::kOTA);
    gMockClock.AdvanceMonotonic(120_ms64);
    SilabsTracer::Instance().TimeTraceEnd(TimeTraceOperation::kOTA);

    // Verify the time tracker values for OTA after success
    timeTracker = SilabsTracer::Instance().GetTimeTracker(TimeTraceOperation::kOTA);
    trackedTime = timeTracker.mEndTime.count() - timeTracker.mStartTime.count();
    EXPECT_EQ(trackedTime, uint32_t(120));
    EXPECT_EQ(timeTracker.mTotalCount, uint32_t(3));
    EXPECT_EQ(timeTracker.mMovingAverage.count(), uint32_t(110));
    EXPECT_EQ(timeTracker.mMaxTimeMs.count(), uint32_t(120));
    EXPECT_EQ(timeTracker.mMinTimeMs.count(), uint32_t(100));
    EXPECT_EQ(timeTracker.mSuccessfullCount, uint32_t(2));
    EXPECT_EQ(timeTracker.mCountAboveAvg, uint32_t(1));
}
