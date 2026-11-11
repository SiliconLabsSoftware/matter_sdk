/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
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
#include <pw_unit_test/framework.h>

#include <app/MessageDef/StatusIB.h>
#include <app/clusters/on-off-server/OnOffLightingCluster.h>
#include <app/clusters/scenes-server/ScenesIntegrationDelegate.h>
#include <app/data-model-provider/tests/ReadTesting.h>
#include <app/persistence/AttributePersistence.h>
#include <app/server-cluster/ServerClusterInterface.h>
#include <app/server-cluster/testing/AttributeTesting.h>
#include <app/server-cluster/testing/ClusterTester.h>
#include <clusters/OnOff/ClusterId.h>
#include <clusters/OnOff/Enums.h>
#include <clusters/OnOff/Metadata.h>
#include <lib/core/CHIPError.h>
#include <lib/support/TimerDelegateMock.h>
#include <lib/support/TypeTraits.h>
#include <lib/support/logging/TextOnlyLogging.h>

#include <vector>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::OnOff;
using namespace chip::Testing;

using chip::Protocols::InteractionModel::Status;

namespace {

constexpr EndpointId kTestEndpointId = 1;

[[maybe_unused]] const char * AsStr(bool v)
{
    return v ? "TRUE" : "FALSE";
}

[[maybe_unused]] const char * StartupOnOffAsStr(std::optional<uint8_t> v)
{
    if (!v.has_value())
    {
        return "NONE";
    }
    switch (*v)
    {
    case to_underlying(StartUpOnOffEnum::kOn):
        return "ON";
    case to_underlying(StartUpOnOffEnum::kOff):
        return "OFF";
    case to_underlying(StartUpOnOffEnum::kToggle):
        return "TOGGLE";
    default:
        return "INVALID";
    }
}

class MockScenesIntegrationDelegate : public chip::scenes::ScenesIntegrationDelegate
{
public:
    struct Call
    {
        FabricIndex fabricIndex;
    };
    std::vector<Call> storeCalls;
    std::vector<Call> recallCalls;
    int markInvalidCalls        = 0;
    int groupWillBeRemovedCalls = 0;

    CHIP_ERROR GroupWillBeRemoved(FabricIndex fabricIndex, GroupId groupId) override
    {
        groupWillBeRemovedCalls++;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR MakeSceneInvalidForAllFabrics() override
    {
        markInvalidCalls++;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR StoreCurrentGlobalScene(FabricIndex fabricIndex) override
    {
        storeCalls.push_back({ fabricIndex });
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR RecallGlobalScene(FabricIndex fabricIndex) override
    {
        recallCalls.push_back({ fabricIndex });
        return CHIP_NO_ERROR;
    }
};

class MockOnOffDelegate : public OnOffDelegate
{
public:
    bool mOnOff         = false;
    bool mCalled        = false;
    bool mStartupCalled = false;

    void Reset()
    {
        mOnOff         = false;
        mCalled        = false;
        mStartupCalled = false;
    }

    void OnOnOffChanged(bool on) override
    {
        mOnOff  = on;
        mCalled = true;
    }

    void OnOffStartup(bool on) override
    {
        mOnOff         = on;
        mStartupCalled = true;
    }
};

class MockOnOffEffectDelegate : public OnOffEffectDelegate
{
public:
    EffectIdentifierEnum mEffectId              = EffectIdentifierEnum::kDelayedAllOff;
    bool mCalled                                = false;
    DataModel::ActionReturnStatus mReturnStatus = Status::Success;

    DataModel::ActionReturnStatus TriggerDelayedAllOff(DelayedAllOffEffectVariantEnum) override
    {
        mEffectId = EffectIdentifierEnum::kDelayedAllOff;
        mCalled   = true;
        return mReturnStatus;
    }

    DataModel::ActionReturnStatus TriggerDyingLight(DyingLightEffectVariantEnum) override
    {
        mEffectId = EffectIdentifierEnum::kDyingLight;
        mCalled   = true;
        return mReturnStatus;
    }
};

class CountingTimerDelegate : public TimerDelegateMock
{
public:
    CriticalFailure StartTimer(TimerContext * context, System::Clock::Timeout timeout) override
    {
        ++mStartCount;
        return TimerDelegateMock::StartTimer(context, timeout);
    }

    System::Clock::Timestamp GetCurrentMonotonicTimestamp() override
    {
        const auto now        = TimerDelegateMock::GetCurrentMonotonicTimestamp();
        const auto advance    = mAdvanceAfterNextRead;
        mAdvanceAfterNextRead = System::Clock::kZero;
        if (advance != System::Clock::kZero)
        {
            AdvanceClock(advance);
        }
        return now;
    }

    uint32_t mStartCount                         = 0;
    System::Clock::Timeout mAdvanceAfterNextRead = System::Clock::kZero;
};

struct TestOnOffLightingCluster : public ::testing::Test
{
    static void SetUpTestSuite() { ASSERT_EQ(Platform::MemoryInit(), CHIP_NO_ERROR); }
    static void TearDownTestSuite() { Platform::MemoryShutdown(); }

    void SetUp() override
    {
        mCluster.AddDelegate(&mMockDelegate);
        EXPECT_EQ(mCluster.Startup(mClusterTester.GetServerClusterContext()), CHIP_NO_ERROR);
    }

    void TearDown() override
    {
        mCluster.Shutdown(ClusterShutdownType::kClusterShutdown);
        mClusterTester.GetTestContext().StorageDelegate().ClearStorage();
    }

    template <typename T>
    void WriteAttributeToStorage(AttributeId id, const T & value)
    {
        EXPECT_EQ(mClusterTester.GetServerClusterContext().attributeStorage.WriteValue(
                      ConcreteAttributePath(kTestEndpointId, OnOff::Id, id), { (const uint8_t *) &value, sizeof(value) }),
                  CHIP_NO_ERROR);
    }

    MockOnOffDelegate mMockDelegate;
    CountingTimerDelegate mMockTimerDelegate;
    MockOnOffEffectDelegate mMockEffectDelegate;
    MockScenesIntegrationDelegate mMockScenesIntegrationDelegate;
    OnOffLightingCluster mCluster{ kTestEndpointId,
                                   {
                                       .timerDelegate             = mMockTimerDelegate,
                                       .effectDelegate            = mMockEffectDelegate,
                                       .scenesIntegrationDelegate = &mMockScenesIntegrationDelegate,
                                   } };
    ClusterTester mClusterTester{ mCluster };
};

TEST_F(TestOnOffLightingCluster, TestFeatureMap)
{
    uint32_t featureMap = 0;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::FeatureMap::Id, featureMap), CHIP_NO_ERROR);
    EXPECT_TRUE(BitMask<Feature>(featureMap).Has(Feature::kLighting));
}

TEST_F(TestOnOffLightingCluster, Startup_OnOffValue)
{
    // Test how StartUpOnOff attribute in storage affects the OnOff state after startup.
    using TestCase = struct
    {
        bool startValue;
        std::optional<uint8_t> startupOnOffValue;
        bool expectedStartState;
    };

    const TestCase kTestCases[] = {
        { true, std::nullopt, true },
        { false, std::nullopt, false },
        { true, to_underlying(StartUpOnOffEnum::kOff), false },
        { false, to_underlying(StartUpOnOffEnum::kOff), false },
        { true, to_underlying(StartUpOnOffEnum::kOn), true },
        { false, to_underlying(StartUpOnOffEnum::kOn), true },
        { true, to_underlying(StartUpOnOffEnum::kToggle), false },
        { false, to_underlying(StartUpOnOffEnum::kToggle), true },

        // Test with invalid StartUpOnOff values, should not change the initial OnOff.
        { true, 123, true },
        { false, 234, false },
    };

    for (const TestCase & testCase : kTestCases)
    {
        mCluster.Shutdown(ClusterShutdownType::kClusterShutdown);

        mClusterTester.GetTestContext().StorageDelegate().ClearStorage();
        WriteAttributeToStorage(Attributes::OnOff::Id, testCase.startValue);
        if (testCase.startupOnOffValue.has_value())
        {
            WriteAttributeToStorage(Attributes::StartUpOnOff::Id, *testCase.startupOnOffValue);
        }

        ChipLogProgress(Test, "Running %s:%s, expecting %s", AsStr(testCase.startValue),
                        StartupOnOffAsStr(testCase.startupOnOffValue), AsStr(testCase.expectedStartState));

        mMockDelegate.Reset();

        ASSERT_EQ(mCluster.Startup(mClusterTester.GetServerClusterContext()), CHIP_NO_ERROR);
        EXPECT_EQ(mCluster.GetOnOff(), testCase.expectedStartState);

        // Only the startup delegate method should be called.
        EXPECT_TRUE(mMockDelegate.mStartupCalled);
        EXPECT_FALSE(mMockDelegate.mCalled);
        EXPECT_EQ(mMockDelegate.mOnOff, testCase.expectedStartState);
    }
}

TEST_F(TestOnOffLightingCluster, Startup_OTA)
{
    // Test that StartupType::kOTA bypasses StartUpOnOff logic.

    mCluster.Shutdown(ClusterShutdownType::kClusterShutdown);

    mClusterTester.GetTestContext().StorageDelegate().ClearStorage();
    WriteAttributeToStorage(Attributes::OnOff::Id, false);                                       // Start OFF
    WriteAttributeToStorage(Attributes::StartUpOnOff::Id, to_underlying(StartUpOnOffEnum::kOn)); // Should turn ON

    MockOnOffDelegate localMockDelegate;

    // Reconstruct the cluster with StartupType::kOTA
    OnOffLightingCluster otaCluster(kTestEndpointId,
                                    {
                                        .timerDelegate             = mMockTimerDelegate,
                                        .effectDelegate            = mMockEffectDelegate,
                                        .scenesIntegrationDelegate = &mMockScenesIntegrationDelegate,
                                        .startupType               = OnOffLightingCluster::StartupType::kOTA,
                                    });
    ClusterTester otaTester(otaCluster);
    otaCluster.AddDelegate(&localMockDelegate);

    ASSERT_EQ(otaCluster.Startup(otaTester.GetServerClusterContext()), CHIP_NO_ERROR);

    // OnOff should remain FALSE, as StartUpOnOff is ignored during OTA startup
    EXPECT_FALSE(otaCluster.GetOnOff());
    EXPECT_TRUE(localMockDelegate.mStartupCalled);
    EXPECT_FALSE(localMockDelegate.mCalled);
    EXPECT_FALSE(localMockDelegate.mOnOff);
}

TEST_F(TestOnOffLightingCluster, Startup_TogglePersists)
{
    // Test that the OnOff value change due to StartUpOnOff::kToggle is persisted.
    const bool initialOnOff = false;

    // Initial state: OFF, StartUpOnOff = kToggle
    mCluster.Shutdown(ClusterShutdownType::kClusterShutdown);
    mClusterTester.GetTestContext().StorageDelegate().ClearStorage();
    WriteAttributeToStorage(Attributes::OnOff::Id, initialOnOff);
    WriteAttributeToStorage(Attributes::StartUpOnOff::Id, to_underlying(StartUpOnOffEnum::kToggle));

    // First startup: Toggles to ON
    mMockDelegate.Reset();
    ASSERT_EQ(mCluster.Startup(mClusterTester.GetServerClusterContext()), CHIP_NO_ERROR);
    EXPECT_TRUE(mCluster.GetOnOff());
    EXPECT_TRUE(mMockDelegate.mStartupCalled);

    // Check that the OnOff attribute in storage is now TRUE
    bool onOffState = false;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OnOff::Id, onOffState), CHIP_NO_ERROR);
    EXPECT_TRUE(onOffState);

    // ensure we do not toggle again (i.e. keep on)
    EXPECT_EQ(mCluster.SetStartupOnOff({}), CHIP_NO_ERROR);

    // Shutdown and Startup again
    mCluster.Shutdown(ClusterShutdownType::kClusterShutdown);
    mMockDelegate.Reset();
    ASSERT_EQ(mCluster.Startup(mClusterTester.GetServerClusterContext()), CHIP_NO_ERROR);

    // OnOff should now be TRUE, as the toggled state (ON) should have been loaded from storage.
    EXPECT_TRUE(mCluster.GetOnOff());

    EXPECT_TRUE(mMockDelegate.mStartupCalled);
}

TEST_F(TestOnOffLightingCluster, TestSetters)
{
    // Test SetOnTime
    mCluster.SetOnTime(100);
    uint16_t onTime = 0;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OnTime::Id, onTime), CHIP_NO_ERROR);
    EXPECT_EQ(onTime, 100);

    // Test SetOffWaitTime
    mCluster.SetOffWaitTime(200);
    uint16_t offWaitTime = 0;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OffWaitTime::Id, offWaitTime), CHIP_NO_ERROR);
    EXPECT_EQ(offWaitTime, 200);

    // Test SetStartupOnOff
    DataModel::Nullable<StartUpOnOffEnum> startUpOnOff;
    EXPECT_EQ(mCluster.SetStartupOnOff(StartUpOnOffEnum::kOn), CHIP_NO_ERROR);
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::StartUpOnOff::Id, startUpOnOff), CHIP_NO_ERROR);
    EXPECT_FALSE(startUpOnOff.IsNull());
    EXPECT_EQ(startUpOnOff.Value(), StartUpOnOffEnum::kOn);

    EXPECT_EQ(mCluster.SetStartupOnOff({}), CHIP_NO_ERROR);
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::StartUpOnOff::Id, startUpOnOff), CHIP_NO_ERROR);
    EXPECT_TRUE(startUpOnOff.IsNull());
}

TEST_F(TestOnOffLightingCluster, TestLightingAttributes)

{
    bool globalSceneControl = false;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::GlobalSceneControl::Id, globalSceneControl), CHIP_NO_ERROR);
    EXPECT_TRUE(globalSceneControl);

    uint16_t onTime = 1;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OnTime::Id, onTime), CHIP_NO_ERROR);
    EXPECT_EQ(onTime, 0);

    uint16_t offWaitTime = 1;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OffWaitTime::Id, offWaitTime), CHIP_NO_ERROR);
    EXPECT_EQ(offWaitTime, 0);

    DataModel::Nullable<StartUpOnOffEnum> startUpOnOff;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::StartUpOnOff::Id, startUpOnOff), CHIP_NO_ERROR);
    EXPECT_TRUE(startUpOnOff.IsNull());
}

TEST_F(TestOnOffLightingCluster, TestDefaultStartUpOnOffFromContext)
{
    // Shutdown the existing cluster
    mCluster.Shutdown(ClusterShutdownType::kClusterShutdown);
    mClusterTester.GetTestContext().StorageDelegate().ClearStorage();

    // Reconstruct the cluster with a default StartUpOnOff value
    OnOffLightingCluster cluster(kTestEndpointId,
                                 {
                                     .timerDelegate             = mMockTimerDelegate,
                                     .effectDelegate            = mMockEffectDelegate,
                                     .scenesIntegrationDelegate = &mMockScenesIntegrationDelegate,
                                     .defaults                  = { .startupOnOff = StartUpOnOffEnum::kOn },
                                 });
    ClusterTester tester(cluster);
    MockOnOffDelegate localMockDelegate;
    cluster.AddDelegate(&localMockDelegate);

    // Startup the cluster
    ASSERT_EQ(cluster.Startup(tester.GetServerClusterContext()), CHIP_NO_ERROR);

    // Check that the StartUpOnOff attribute is set to the default value
    DataModel::Nullable<StartUpOnOffEnum> startUpOnOff;
    EXPECT_EQ(tester.ReadAttribute(Attributes::StartUpOnOff::Id, startUpOnOff), CHIP_NO_ERROR);
    EXPECT_FALSE(startUpOnOff.IsNull());
    EXPECT_EQ(startUpOnOff.Value(), StartUpOnOffEnum::kOn);

    // Check that the OnOff attribute is set to true due to the default StartUpOnOff value
    EXPECT_TRUE(cluster.GetOnOff());
    EXPECT_TRUE(localMockDelegate.mStartupCalled);
    EXPECT_TRUE(localMockDelegate.mOnOff);
}

TEST_F(TestOnOffLightingCluster, TestOnWithTimedOff)
{
    // Step 1: Turn On with Timed Off (OnTime = 10, OffWaitTime = 20)
    Commands::OnWithTimedOff::Type command;
    command.onTime      = 10;
    command.offWaitTime = 20;

    EXPECT_TRUE(mClusterTester.Invoke(command).IsSuccess());
    EXPECT_TRUE(mMockDelegate.mOnOff);
    EXPECT_TRUE(mMockTimerDelegate.IsTimerActive(&mCluster));

    uint16_t onTime = 0;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OnTime::Id, onTime), CHIP_NO_ERROR);
    EXPECT_EQ(onTime, 10);

    uint16_t offWaitTime = 0;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OffWaitTime::Id, offWaitTime), CHIP_NO_ERROR);
    EXPECT_EQ(offWaitTime, 20);

    // Step 2: Advance Clock (simulate 1 tick of 100ms)
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(100));

    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OnTime::Id, onTime), CHIP_NO_ERROR);
    EXPECT_EQ(onTime, 9);

    // Step 3: Advance Clock to exhaust OnTime
    for (int i = 0; i < 9; ++i)
    {
        mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(100));
    }

    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OnTime::Id, onTime), CHIP_NO_ERROR);
    EXPECT_EQ(onTime, 0);

    // Per spec: "If OnTime reaches 0, the server SHALL set the OffWaitTime and OnOff attributes to 0 and FALSE"
    EXPECT_FALSE(mMockDelegate.mOnOff);

    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OffWaitTime::Id, offWaitTime), CHIP_NO_ERROR);
    EXPECT_EQ(offWaitTime, 0);
}

TEST_F(TestOnOffLightingCluster, TestOffWithEffect)
{
    // Step 1: Turn On
    EXPECT_TRUE(mClusterTester.Invoke(Commands::On::Type()).IsSuccess());
    EXPECT_TRUE(mMockDelegate.mOnOff);

    // Step 2: Invoke OffWithEffect
    Commands::OffWithEffect::Type command;
    command.effectIdentifier = EffectIdentifierEnum::kDyingLight;
    command.effectVariant    = 0; // Variant is not used for DyingLight

    EXPECT_TRUE(mClusterTester.Invoke(command).IsSuccess());

    EXPECT_TRUE(mMockEffectDelegate.mCalled);
    EXPECT_EQ(mMockEffectDelegate.mEffectId, EffectIdentifierEnum::kDyingLight);
    EXPECT_FALSE(mMockDelegate.mOnOff);
    EXPECT_EQ(mMockScenesIntegrationDelegate.markInvalidCalls, 1);
}

TEST_F(TestOnOffLightingCluster, TestOffWithEffect_DelegateFails)
{
    // Step 1: Turn On
    EXPECT_TRUE(mClusterTester.Invoke(Commands::On::Type()).IsSuccess());
    EXPECT_TRUE(mMockDelegate.mOnOff);

    // Step 2: Configure the effect delegate to return Failure
    mMockEffectDelegate.mReturnStatus = Status::Failure;

    // Step 3: Invoke OffWithEffect
    Commands::OffWithEffect::Type command;
    command.effectIdentifier = EffectIdentifierEnum::kDelayedAllOff;
    command.effectVariant    = 0;

    auto result = mClusterTester.Invoke(command);
    EXPECT_EQ(result.status, Status::Failure);

    EXPECT_TRUE(mMockEffectDelegate.mCalled);
    EXPECT_EQ(mMockEffectDelegate.mEffectId, EffectIdentifierEnum::kDelayedAllOff);

    // OnOff state should not change if the effect fails.
    EXPECT_TRUE(mMockDelegate.mOnOff);

    bool globalSceneControl = false;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::GlobalSceneControl::Id, globalSceneControl), CHIP_NO_ERROR);
    EXPECT_TRUE(globalSceneControl);
}

TEST_F(TestOnOffLightingCluster, TestTimerCancellation)
{
    // Step 1: Invoke OnWithTimedOff with OnTime=10 and OffWaitTime=0
    Commands::OnWithTimedOff::Type command;
    command.onTime      = 10;
    command.offWaitTime = 0;

    EXPECT_TRUE(mClusterTester.Invoke(command).IsSuccess());
    EXPECT_TRUE(mMockTimerDelegate.IsTimerActive(&mCluster));

    // Step 2: Send an Off command.
    EXPECT_TRUE(mClusterTester.Invoke(Commands::Off::Type()).IsSuccess());
    EXPECT_FALSE(mMockDelegate.mOnOff);

    // Timer should be cancelled because OffWaitTime is 0.
    EXPECT_FALSE(mMockTimerDelegate.IsTimerActive(&mCluster));
}

TEST_F(TestOnOffLightingCluster, TestOffWaitTime)
{
    // Step 1: Invoke OnWithTimedOff with OnTime=10 and OffWaitTime=5
    Commands::OnWithTimedOff::Type command;
    command.onTime      = 10;
    command.offWaitTime = 5;

    EXPECT_TRUE(mClusterTester.Invoke(command).IsSuccess());

    // Step 2: Send an Off command.
    EXPECT_TRUE(mClusterTester.Invoke(Commands::Off::Type()).IsSuccess());
    EXPECT_FALSE(mMockDelegate.mOnOff);

    // Timer should remain active to count down OffWaitTime.
    EXPECT_TRUE(mMockTimerDelegate.IsTimerActive(&mCluster));

    // Step 3: Advance clock to exhaust OffWaitTime.
    for (int i = 0; i < 5; ++i)
    {
        mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(100));
    }

    EXPECT_FALSE(mMockTimerDelegate.IsTimerActive(&mCluster));

    uint16_t offWaitTime = 1;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OffWaitTime::Id, offWaitTime), CHIP_NO_ERROR);
    EXPECT_EQ(offWaitTime, 0);
}

TEST_F(TestOnOffLightingCluster, DelayedCountdownsRetainFractionalTicks)
{
    for (bool on : { true, false })
    {
        Commands::OnWithTimedOff::Type command;
        command.onTime      = 300;
        command.offWaitTime = 300;
        ASSERT_TRUE(mClusterTester.Invoke(command).IsSuccess());
        if (!on)
        {
            ASSERT_TRUE(mClusterTester.Invoke(Commands::Off::Type()).IsSuccess());
        }

        const ConcreteClusterPath path(kTestEndpointId, OnOff::Id);
        const DataVersion version  = mCluster.GetDataVersion(path);
        const uint32_t timerStarts = mMockTimerDelegate.mStartCount;
        mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(2450));
        EXPECT_EQ(mMockTimerDelegate.mStartCount, timerStarts + 1);
        EXPECT_EQ(mCluster.GetDataVersion(path), version + 1);
        EXPECT_EQ(on ? mCluster.GetOnTime() : mCluster.GetOffWaitTime(), 276);
        mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(49));
        EXPECT_EQ(on ? mCluster.GetOnTime() : mCluster.GetOffWaitTime(), 276);
        mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(1));
        EXPECT_EQ(on ? mCluster.GetOnTime() : mCluster.GetOffWaitTime(), 275);

        mCluster.SetOnTime(0);
        mCluster.SetOffWaitTime(0);
    }
}

TEST_F(TestOnOffLightingCluster, IrregularCallbacksDoNotAccumulateDrift)
{
    ASSERT_TRUE(mClusterTester.Invoke(Commands::On::Type()).IsSuccess());
    mCluster.SetOnTime(300);

    uint32_t elapsedMs = 0;
    for (uint32_t intervalMs : { 77u, 168u, 2407u, 1u, 349u, 98u, 2400u })
    {
        elapsedMs += intervalMs;
        mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(intervalMs));
        EXPECT_EQ(mCluster.GetOnTime(), 300u - elapsedMs / 100u);
    }
}

TEST_F(TestOnOffLightingCluster, EarlyCallbackDoesNotDecrementCountdown)
{
    ASSERT_TRUE(mClusterTester.Invoke(Commands::On::Type()).IsSuccess());
    mCluster.SetOnTime(10);

    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(50));
    mMockTimerDelegate.CancelTimer(&mCluster);
    mCluster.TimerFired();
    EXPECT_TRUE(mMockTimerDelegate.IsTimerActive(&mCluster));
    EXPECT_EQ(mCluster.GetOnTime(), 10);
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(49));
    EXPECT_EQ(mCluster.GetOnTime(), 10);
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(1));
    EXPECT_EQ(mCluster.GetOnTime(), 9);
}

TEST_F(TestOnOffLightingCluster, DelayedExpiryClampsAndTransitionsOnce)
{
    Commands::OnWithTimedOff::Type command;
    command.onTime      = 10;
    command.offWaitTime = 20;
    ASSERT_TRUE(mClusterTester.Invoke(command).IsSuccess());

    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(2450));
    EXPECT_EQ(mCluster.GetOnTime(), 0);
    EXPECT_EQ(mCluster.GetOffWaitTime(), 0);
    EXPECT_FALSE(mCluster.GetOnOff());
    EXPECT_FALSE(mMockTimerDelegate.IsTimerActive(&mCluster));

    mMockDelegate.mCalled = false;
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(1000));
    EXPECT_FALSE(mMockDelegate.mCalled);

    mCluster.SetOffWaitTime(20);
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(2450));
    EXPECT_EQ(mCluster.GetOffWaitTime(), 0);
    EXPECT_FALSE(mCluster.GetOnOff());
    EXPECT_FALSE(mMockDelegate.mCalled);
    EXPECT_FALSE(mMockTimerDelegate.IsTimerActive(&mCluster));
}

TEST_F(TestOnOffLightingCluster, UnrelatedWritesPreserveCountdownPhase)
{
    for (bool on : { true, false })
    {
        Commands::OnWithTimedOff::Type command;
        command.onTime      = 30;
        command.offWaitTime = 30;
        ASSERT_TRUE(mClusterTester.Invoke(command).IsSuccess());
        if (!on)
        {
            ASSERT_TRUE(mClusterTester.Invoke(Commands::Off::Type()).IsSuccess());
        }

        mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(50));
        ASSERT_EQ(mClusterTester.WriteAttribute(on ? Attributes::OffWaitTime::Id : Attributes::OnTime::Id, uint16_t(5)),
                  CHIP_NO_ERROR);
        mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(50));
        EXPECT_EQ(on ? mCluster.GetOnTime() : mCluster.GetOffWaitTime(), 29);

        mCluster.SetOnTime(0);
        mCluster.SetOffWaitTime(0);
    }
}

TEST_F(TestOnOffLightingCluster, UnchangedAndDiscardedCommandsPreserveCountdownPhase)
{
    for (bool on : { true, false })
    {
        Commands::OnWithTimedOff::Type command;
        command.onTime      = 30;
        command.offWaitTime = 30;
        ASSERT_TRUE(mClusterTester.Invoke(command).IsSuccess());
        if (!on)
        {
            ASSERT_TRUE(mClusterTester.Invoke(Commands::Off::Type()).IsSuccess());
            command.onOffControl.Set(OnOffControlBitmap::kAcceptOnlyWhenOn);
        }

        mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(50));
        ASSERT_TRUE(mClusterTester.Invoke(command).IsSuccess());
        mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(50));
        EXPECT_EQ(on ? mCluster.GetOnTime() : mCluster.GetOffWaitTime(), 29);

        mCluster.SetOnTime(0);
        mCluster.SetOffWaitTime(0);
    }
}

TEST_F(TestOnOffLightingCluster, ReadsDoNotAdvancePendingCountdown)
{
    for (bool on : { true, false })
    {
        Commands::OnWithTimedOff::Type command;
        command.onTime      = 300;
        command.offWaitTime = 300;
        ASSERT_TRUE(mClusterTester.Invoke(command).IsSuccess());
        if (!on)
        {
            ASSERT_TRUE(mClusterTester.Invoke(Commands::Off::Type()).IsSuccess());
        }

        // Suppress delivery while advancing the clock, as when another stack operation blocks the callback.
        mMockTimerDelegate.CancelTimer(&mCluster);
        mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(2450));
        const ConcreteClusterPath path(kTestEndpointId, OnOff::Id);
        const DataVersion version  = mCluster.GetDataVersion(path);
        const uint32_t timerStarts = mMockTimerDelegate.mStartCount;
        ReadOperation read(kTestEndpointId, OnOff::Id, on ? Attributes::OnTime::Id : Attributes::OffWaitTime::Id);
        auto encoder = read.StartEncoding(ReadOperation::EncodingParams().SetDataVersion(version));
        ASSERT_NE(encoder.get(), nullptr);
        ASSERT_TRUE(mCluster.ReadAttribute(read.GetRequest(), *encoder).IsSuccess());
        ASSERT_EQ(read.FinishEncoding(), CHIP_NO_ERROR);
        std::vector<DecodedAttributeData> reports;
        ASSERT_EQ(read.GetEncodedIBs().Decode(reports), CHIP_NO_ERROR);
        ASSERT_EQ(reports.size(), 1u);
        EXPECT_EQ(reports.front().dataVersion, version);
        EXPECT_EQ(mCluster.GetDataVersion(path), version);
        EXPECT_EQ(mMockTimerDelegate.mStartCount, timerStarts);
        uint16_t remaining = 0;
        ASSERT_EQ(reports.front().dataReader.Get(remaining), CHIP_NO_ERROR);
        EXPECT_EQ(remaining, 300);
        mCluster.TimerFired();
        remaining = on ? mCluster.GetOnTime() : mCluster.GetOffWaitTime();
        EXPECT_EQ(remaining, 276);
        mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(50));
        EXPECT_EQ(on ? mCluster.GetOnTime() : mCluster.GetOffWaitTime(), 275);

        mCluster.SetOnTime(0);
        mCluster.SetOffWaitTime(0);
    }
}

TEST_F(TestOnOffLightingCluster, CommandsUseElapsedCountdownBeforeApplyingMaximum)
{
    Commands::OnWithTimedOff::Type command;
    command.onTime = 30;
    ASSERT_TRUE(mClusterTester.Invoke(command).IsSuccess());
    mMockTimerDelegate.CancelTimer(&mCluster);
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(2450));

    command.onTime = 10;
    ASSERT_TRUE(mClusterTester.Invoke(command).IsSuccess());
    EXPECT_EQ(mCluster.GetOnTime(), 10);
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(50));
    EXPECT_EQ(mCluster.GetOnTime(), 9);
}

TEST_F(TestOnOffLightingCluster, TimedCommandsUseOneCountdownSnapshot)
{
    for (bool on : { true, false })
    {
        Commands::OnWithTimedOff::Type command;
        command.onTime      = 1;
        command.offWaitTime = 2;
        ASSERT_TRUE(mClusterTester.Invoke(command).IsSuccess());
        if (!on)
        {
            ASSERT_TRUE(mClusterTester.Invoke(Commands::Off::Type()).IsSuccess());
        }

        mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(50));
        // Cross a tick during command execution without delivering the pending timer.
        mMockTimerDelegate.CancelTimer(&mCluster);
        mMockTimerDelegate.mAdvanceAfterNextRead = System::Clock::Milliseconds32(100);
        command.onTime                           = 10;
        command.offWaitTime                      = 10;
        ASSERT_TRUE(mClusterTester.Invoke(command).IsSuccess());
        EXPECT_EQ(mCluster.GetOnOff(), on);
        EXPECT_EQ(on ? mCluster.GetOnTime() : mCluster.GetOffWaitTime(), on ? 10 : 2);

        mMockTimerDelegate.AdvanceClock(System::Clock::kZero);
        EXPECT_EQ(mCluster.GetOnOff(), on);
        EXPECT_EQ(on ? mCluster.GetOnTime() : mCluster.GetOffWaitTime(), on ? 9 : 1);

        mCluster.SetOnTime(0);
        mCluster.SetOffWaitTime(0);
    }
}

TEST_F(TestOnOffLightingCluster, WritesDoNotChargeOldElapsedTimeToNewValue)
{
    ASSERT_TRUE(mClusterTester.Invoke(Commands::On::Type()).IsSuccess());
    mCluster.SetOnTime(30);
    mMockTimerDelegate.CancelTimer(&mCluster);
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(2450));

    ASSERT_EQ(mClusterTester.WriteAttribute(Attributes::OnTime::Id, uint16_t(100)), CHIP_NO_ERROR);
    EXPECT_EQ(mCluster.GetOnTime(), 100);
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(50));
    EXPECT_EQ(mCluster.GetOnTime(), 99);
}

TEST_F(TestOnOffLightingCluster, CommandsUseElapsedOffWaitTimeBeforeApplyingMinimum)
{
    Commands::OnWithTimedOff::Type command;
    command.onTime      = 30;
    command.offWaitTime = 30;
    ASSERT_TRUE(mClusterTester.Invoke(command).IsSuccess());
    ASSERT_TRUE(mClusterTester.Invoke(Commands::Off::Type()).IsSuccess());
    mMockTimerDelegate.CancelTimer(&mCluster);
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(2450));

    command.offWaitTime = 20;
    ASSERT_TRUE(mClusterTester.Invoke(command).IsSuccess());
    EXPECT_EQ(mCluster.GetOffWaitTime(), 6);
    EXPECT_FALSE(mCluster.GetOnOff());
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(50));
    EXPECT_EQ(mCluster.GetOffWaitTime(), 5);
}

TEST_F(TestOnOffLightingCluster, ExpiredOnTimeIsAppliedBeforeAcceptOnlyWhenOn)
{
    Commands::OnWithTimedOff::Type command;
    command.onTime = 10;
    ASSERT_TRUE(mClusterTester.Invoke(command).IsSuccess());
    mMockTimerDelegate.CancelTimer(&mCluster);
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(2450));

    command.onOffControl.Set(OnOffControlBitmap::kAcceptOnlyWhenOn);
    ASSERT_TRUE(mClusterTester.Invoke(command).IsSuccess());
    EXPECT_EQ(mCluster.GetOnTime(), 0);
    EXPECT_FALSE(mCluster.GetOnOff());
    EXPECT_FALSE(mMockTimerDelegate.IsTimerActive(&mCluster));
}

TEST_F(TestOnOffLightingCluster, LevelControlTransitionStartsFreshCountdownPhase)
{
    Commands::OnWithTimedOff::Type command;
    command.onTime      = 30;
    command.offWaitTime = 20;
    ASSERT_TRUE(mClusterTester.Invoke(command).IsSuccess());
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(50));

    ASSERT_EQ(mCluster.SetOnOffWithTimeReset(false), CHIP_NO_ERROR);
    EXPECT_EQ(mCluster.GetOnTime(), 0);
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(50));
    EXPECT_EQ(mCluster.GetOffWaitTime(), 20);
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(50));
    EXPECT_EQ(mCluster.GetOffWaitTime(), 19);
}

TEST_F(TestOnOffLightingCluster, StoppedAndInfiniteCountdownsRestartWithFreshPhase)
{
    for (bool on : { true, false })
    {
        ASSERT_EQ(mCluster.SetOnOffWithTimeReset(on), CHIP_NO_ERROR);
        for (uint16_t stoppedValue : { uint16_t(0), uint16_t(0xFFFF) })
        {
            if (on)
            {
                mCluster.SetOnTime(stoppedValue);
            }
            else
            {
                mCluster.SetOffWaitTime(stoppedValue);
            }
            EXPECT_FALSE(mMockTimerDelegate.IsTimerActive(&mCluster));
            mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(2450));
            mCluster.TimerFired();
            EXPECT_EQ(on ? mCluster.GetOnTime() : mCluster.GetOffWaitTime(), stoppedValue);

            if (on)
            {
                mCluster.SetOnTime(10);
            }
            else
            {
                mCluster.SetOffWaitTime(10);
            }
            mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(99));
            EXPECT_EQ(on ? mCluster.GetOnTime() : mCluster.GetOffWaitTime(), 10);
            mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(1));
            EXPECT_EQ(on ? mCluster.GetOnTime() : mCluster.GetOffWaitTime(), 9);
        }
        mCluster.SetOnTime(0);
        mCluster.SetOffWaitTime(0);
    }
}

TEST_F(TestOnOffLightingCluster, CountdownShutdownCancelsAndRestartResetsPhase)
{
    ASSERT_TRUE(mClusterTester.Invoke(Commands::On::Type()).IsSuccess());
    mCluster.SetOnTime(30);
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(50));
    mCluster.Shutdown(ClusterShutdownType::kClusterShutdown);
    EXPECT_FALSE(mMockTimerDelegate.IsTimerActive(&mCluster));
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(2450));

    ASSERT_EQ(mCluster.Startup(mClusterTester.GetServerClusterContext()), CHIP_NO_ERROR);
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(50));
    EXPECT_EQ(mCluster.GetOnTime(), 30);
    mMockTimerDelegate.AdvanceClock(System::Clock::Milliseconds32(50));
    EXPECT_EQ(mCluster.GetOnTime(), 29);
}

TEST_F(TestOnOffLightingCluster, TestGlobalSceneControl)
{
    // Step 1: Check initial State: GlobalSceneControl is true
    bool globalSceneControl = false;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::GlobalSceneControl::Id, globalSceneControl), CHIP_NO_ERROR);
    EXPECT_TRUE(globalSceneControl);

    // Step 2: OffWithEffect should set GlobalSceneControl to false and store the scene.
    Commands::OffWithEffect::Type offCommand;
    offCommand.effectIdentifier = EffectIdentifierEnum::kDyingLight;
    offCommand.effectVariant    = 0;
    EXPECT_TRUE(mClusterTester.Invoke(offCommand).IsSuccess());

    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::GlobalSceneControl::Id, globalSceneControl), CHIP_NO_ERROR);
    EXPECT_FALSE(globalSceneControl);
    EXPECT_EQ(mMockScenesIntegrationDelegate.storeCalls.size(), 1u);

    // Step 3: OnWithRecallGlobalScene should set GlobalSceneControl to true and recall the scene.
    EXPECT_TRUE(mClusterTester.Invoke(Commands::OnWithRecallGlobalScene::Type()).IsSuccess());

    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::GlobalSceneControl::Id, globalSceneControl), CHIP_NO_ERROR);
    EXPECT_TRUE(globalSceneControl);
    EXPECT_EQ(mMockScenesIntegrationDelegate.recallCalls.size(), 1u);
}

TEST_F(TestOnOffLightingCluster, TestSetOnOffWithTimeReset)
{
    // Step 1: Set OnTime and OffWaitTime to non-zero values
    mCluster.SetOnTime(100);
    mCluster.SetOffWaitTime(200);

    // Step 2: Call SetOnOffWithTimeReset(false) to turn off and clear OnTime.
    // Spec requires mOnTime to be set to 0
    EXPECT_EQ(mCluster.SetOnOffWithTimeReset(false), CHIP_NO_ERROR);
    uint16_t onTime = 1;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OnTime::Id, onTime), CHIP_NO_ERROR);
    EXPECT_EQ(onTime, 0);
    uint16_t offWaitTime = 0;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OffWaitTime::Id, offWaitTime), CHIP_NO_ERROR);
    EXPECT_EQ(offWaitTime, 200);

    // Step 3: Set OnTime to 0 and OffWaitTime to non-zero values
    mCluster.SetOnTime(0);
    mCluster.SetOffWaitTime(200);

    // Step 4: Call SetOnOffWithTimeReset(true) to turn on and clear OffWaitTime.
    EXPECT_EQ(mCluster.SetOnOffWithTimeReset(true), CHIP_NO_ERROR);
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OffWaitTime::Id, offWaitTime), CHIP_NO_ERROR);
    EXPECT_EQ(offWaitTime, 0);
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OnTime::Id, onTime), CHIP_NO_ERROR);
    EXPECT_EQ(onTime, 0);

    // Step 5: Turn off, set OnTime and OffWaitTime to non-zero values
    EXPECT_EQ(mCluster.SetOnOffWithTimeReset(false), CHIP_NO_ERROR);
    mCluster.SetOnTime(100);
    mCluster.SetOffWaitTime(200);

    // Step 6: Call SetOnOffWithTimeReset(true) to turn on and clear OffWaitTime.
    // Spec requires mOffTime to NOT be set to 0 because mOnTime is not 0
    EXPECT_EQ(mCluster.SetOnOffWithTimeReset(true), CHIP_NO_ERROR);
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OffWaitTime::Id, offWaitTime), CHIP_NO_ERROR);
    EXPECT_EQ(offWaitTime, 200);
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OnTime::Id, onTime), CHIP_NO_ERROR);
    EXPECT_EQ(onTime, 100);
}

TEST_F(TestOnOffLightingCluster, TestOffWithEffect_GlobalSceneControlFalse)
{
    // Step 1: Turn On.
    EXPECT_TRUE(mClusterTester.Invoke(Commands::On::Type()).IsSuccess());
    EXPECT_TRUE(mMockDelegate.mOnOff);

    // Step 2: Set GlobalSceneControl to false by invoking OffWithEffect.
    Commands::OffWithEffect::Type offCommand1;
    offCommand1.effectIdentifier = EffectIdentifierEnum::kDyingLight;
    offCommand1.effectVariant    = 0;
    EXPECT_TRUE(mClusterTester.Invoke(offCommand1).IsSuccess());
    EXPECT_EQ(mMockScenesIntegrationDelegate.storeCalls.size(), 1u);

    bool globalSceneControl = true;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::GlobalSceneControl::Id, globalSceneControl), CHIP_NO_ERROR);
    EXPECT_FALSE(globalSceneControl);

    // Step 3: Reset mock delegates for next call.
    mMockScenesIntegrationDelegate.storeCalls.clear();
    mMockEffectDelegate.mCalled = false;

    // Step 4: Turn On again. This sets GlobalSceneControl to true.
    EXPECT_TRUE(mClusterTester.Invoke(Commands::On::Type()).IsSuccess());
    EXPECT_TRUE(mMockDelegate.mOnOff);

    // Step 5: Set GlobalSceneControl to false by invoking OffWithEffect.
    EXPECT_TRUE(mClusterTester.Invoke(offCommand1).IsSuccess());
    EXPECT_EQ(mMockScenesIntegrationDelegate.storeCalls.size(), 1u);

    // start clear
    mMockScenesIntegrationDelegate.storeCalls.clear();
    mMockEffectDelegate.mCalled = false;

    // Step 6: Call OffWithEffect again. Since GlobalSceneControl is false,
    // no effect should be triggered and no scene operations should occur.
    Commands::OffWithEffect::Type offCommand2;
    offCommand2.effectIdentifier = EffectIdentifierEnum::kDelayedAllOff;
    offCommand2.effectVariant    = 0;
    EXPECT_TRUE(mClusterTester.Invoke(offCommand2).IsSuccess());

    EXPECT_EQ(mMockScenesIntegrationDelegate.storeCalls.size(), 0u);
    EXPECT_FALSE(mMockEffectDelegate.mCalled);
    EXPECT_FALSE(mMockDelegate.mOnOff);
}

TEST_F(TestOnOffLightingCluster, TestOnWithRecallGlobalScene_GlobalSceneControlTrue)
{
    // If GlobalSceneControl is true, OnWithRecallGlobalScene should do nothing.
    EXPECT_FALSE(mMockDelegate.mOnOff);

    EXPECT_TRUE(mClusterTester.Invoke(Commands::OnWithRecallGlobalScene::Type()).IsSuccess());

    EXPECT_EQ(mMockScenesIntegrationDelegate.recallCalls.size(), 0u);
    EXPECT_FALSE(mMockDelegate.mOnOff);

    bool globalSceneControl = false;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::GlobalSceneControl::Id, globalSceneControl), CHIP_NO_ERROR);
    EXPECT_TRUE(globalSceneControl);
}

class FailingRecallScenesIntegrationDelegate : public MockScenesIntegrationDelegate
{
public:
    CHIP_ERROR RecallGlobalScene(FabricIndex fabricIndex) override
    {
        recallCalls.push_back({ fabricIndex });
        return CHIP_ERROR_BAD_REQUEST; // Simulate failure
    }
};

TEST_F(TestOnOffLightingCluster, TestOnWithRecallGlobalScene_RecallFails)
{
    // Step 1: Set GlobalSceneControl to false using the default delegate.
    EXPECT_TRUE(mClusterTester.Invoke(Commands::On::Type()).IsSuccess());
    Commands::OffWithEffect::Type offCommand;
    offCommand.effectIdentifier = EffectIdentifierEnum::kDyingLight;
    offCommand.effectVariant    = 0;
    EXPECT_TRUE(mClusterTester.Invoke(offCommand).IsSuccess());
    bool globalSceneControl = true;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::GlobalSceneControl::Id, globalSceneControl), CHIP_NO_ERROR);
    EXPECT_FALSE(globalSceneControl);

    // Step 2: Re-setup the cluster with the FailingRecallScenesIntegrationDelegate.
    FailingRecallScenesIntegrationDelegate failingDelegate;
    MockOnOffDelegate localMockDelegate;
    OnOffLightingCluster cluster(kTestEndpointId,
                                 { .timerDelegate             = mMockTimerDelegate,
                                   .effectDelegate            = mMockEffectDelegate,
                                   .scenesIntegrationDelegate = &failingDelegate });
    ClusterTester tester(cluster);
    cluster.AddDelegate(&localMockDelegate);
    EXPECT_EQ(cluster.Startup(tester.GetServerClusterContext()), CHIP_NO_ERROR);

    // Step 3: Set GlobalSceneControl to false again with the new cluster instance.
    EXPECT_TRUE(tester.Invoke(Commands::On::Type()).IsSuccess());
    EXPECT_TRUE(tester.Invoke(offCommand).IsSuccess());
    EXPECT_EQ(tester.ReadAttribute(Attributes::GlobalSceneControl::Id, globalSceneControl), CHIP_NO_ERROR);
    EXPECT_FALSE(globalSceneControl);

    // Step 4: Invoke OnWithRecallGlobalScene. Recall will fail.
    EXPECT_TRUE(tester.Invoke(Commands::OnWithRecallGlobalScene::Type()).IsSuccess());

    EXPECT_EQ(failingDelegate.recallCalls.size(), 1u);

    // Fallback behavior: OnOff should be set to TRUE.
    EXPECT_TRUE(localMockDelegate.mOnOff);

    EXPECT_EQ(tester.ReadAttribute(Attributes::GlobalSceneControl::Id, globalSceneControl), CHIP_NO_ERROR);
    EXPECT_TRUE(globalSceneControl);
}

TEST_F(TestOnOffLightingCluster, TestOnWithTimedOff_AcceptOnlyWhenOn)
{
    // Step 1: Ensure device is OFF (default state).
    EXPECT_FALSE(mMockDelegate.mOnOff);

    // Step 2: Call OnWithTimedOff with AcceptOnlyWhenOn = true.
    // The command should be discarded as the device is OFF.
    Commands::OnWithTimedOff::Type command;
    command.onOffControl.Set(OnOffControlBitmap::kAcceptOnlyWhenOn);
    command.onTime      = 10;
    command.offWaitTime = 20;

    EXPECT_TRUE(mClusterTester.Invoke(command).IsSuccess());

    EXPECT_FALSE(mMockDelegate.mOnOff);
    EXPECT_FALSE(mMockTimerDelegate.IsTimerActive(&mCluster));
}

TEST_F(TestOnOffLightingCluster, TestOnWithTimedOff_DelayedOffGuard)
{
    // Step 1: Ensure device is OFF.
    EXPECT_TRUE(mClusterTester.Invoke(Commands::Off::Type()).IsSuccess());
    EXPECT_FALSE(mMockDelegate.mOnOff);

    // Step 2: Set OffWaitTime to a non-zero value.
    EXPECT_EQ(mClusterTester.WriteAttribute(Attributes::OffWaitTime::Id, static_cast<uint16_t>(5)), CHIP_NO_ERROR);
    uint16_t offWaitTime = 0;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OffWaitTime::Id, offWaitTime), CHIP_NO_ERROR);
    EXPECT_EQ(offWaitTime, 5);

    // Step 3: Call OnWithTimedOff. Since the device is OFF and OffWaitTime > 0,
    // the command should only potentially reduce OffWaitTime.
    Commands::OnWithTimedOff::Type command;
    command.onTime      = 10;
    command.offWaitTime = 20;

    EXPECT_TRUE(mClusterTester.Invoke(command).IsSuccess());

    EXPECT_FALSE(mMockDelegate.mOnOff);

    // OffWaitTime should be min(current, new).
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OffWaitTime::Id, offWaitTime), CHIP_NO_ERROR);
    EXPECT_EQ(offWaitTime, std::min(static_cast<uint16_t>(5), static_cast<uint16_t>(20)));

    // Timer should be active to count down OffWaitTime.
    EXPECT_TRUE(mMockTimerDelegate.IsTimerActive(&mCluster));
}

TEST_F(TestOnOffLightingCluster, TestWriteOnTimeUpdatesTimer)
{
    EXPECT_FALSE(mMockTimerDelegate.IsTimerActive(&mCluster));

    // Step 1: Turn device ON.
    EXPECT_TRUE(mClusterTester.Invoke(Commands::On::Type()).IsSuccess());
    EXPECT_TRUE(mMockDelegate.mOnOff);

    // Step 2: Write non-zero OnTime, timer should start.
    EXPECT_EQ(mClusterTester.WriteAttribute(Attributes::OnTime::Id, static_cast<uint16_t>(100)), CHIP_NO_ERROR);
    EXPECT_TRUE(mMockTimerDelegate.IsTimerActive(&mCluster));

    // Step 3: Write 0 to OnTime, timer should stop.
    EXPECT_EQ(mClusterTester.WriteAttribute(Attributes::OnTime::Id, static_cast<uint16_t>(0)), CHIP_NO_ERROR);
    EXPECT_FALSE(mMockTimerDelegate.IsTimerActive(&mCluster));
}

TEST_F(TestOnOffLightingCluster, TestWriteOffWaitTimeUpdatesTimer)
{
    // Step 1: Ensure device is OFF.
    EXPECT_TRUE(mClusterTester.Invoke(Commands::Off::Type()).IsSuccess());
    EXPECT_FALSE(mMockDelegate.mOnOff);
    EXPECT_FALSE(mMockTimerDelegate.IsTimerActive(&mCluster));

    // Step 2: Write non-zero OffWaitTime, timer should start.
    EXPECT_EQ(mClusterTester.WriteAttribute(Attributes::OffWaitTime::Id, static_cast<uint16_t>(100)), CHIP_NO_ERROR);
    EXPECT_TRUE(mMockTimerDelegate.IsTimerActive(&mCluster));

    // Step 3: Write 0 to OffWaitTime, timer should stop.
    EXPECT_EQ(mClusterTester.WriteAttribute(Attributes::OffWaitTime::Id, static_cast<uint16_t>(0)), CHIP_NO_ERROR);
    EXPECT_FALSE(mMockTimerDelegate.IsTimerActive(&mCluster));
}

TEST_F(TestOnOffLightingCluster, TestWriteStartUpOnOff)
{
    // Step 1: Check Initial State: OnOff is false, StartUpOnOff is null.
    EXPECT_FALSE(mMockDelegate.mOnOff);
    DataModel::Nullable<StartUpOnOffEnum> startUpOnOff;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::StartUpOnOff::Id, startUpOnOff), CHIP_NO_ERROR);
    EXPECT_TRUE(startUpOnOff.IsNull());

    // Step 2: Write StartUpOnOff to kOn. OnOff state should not change.
    EXPECT_EQ(mClusterTester.WriteAttribute(Attributes::StartUpOnOff::Id, StartUpOnOffEnum::kOn), CHIP_NO_ERROR);
    EXPECT_FALSE(mMockDelegate.mOnOff);
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::StartUpOnOff::Id, startUpOnOff), CHIP_NO_ERROR);
    EXPECT_FALSE(startUpOnOff.IsNull());
    EXPECT_EQ(startUpOnOff.Value(), StartUpOnOffEnum::kOn);

    // Step 3: Write StartUpOnOff to kOff. OnOff state should not change.
    EXPECT_EQ(mClusterTester.WriteAttribute(Attributes::StartUpOnOff::Id, StartUpOnOffEnum::kOff), CHIP_NO_ERROR);
    EXPECT_FALSE(mMockDelegate.mOnOff);
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::StartUpOnOff::Id, startUpOnOff), CHIP_NO_ERROR);
    EXPECT_FALSE(startUpOnOff.IsNull());
    EXPECT_EQ(startUpOnOff.Value(), StartUpOnOffEnum::kOff);

    // Step 4: Write StartUpOnOff to kToggle. OnOff state should not change.
    EXPECT_EQ(mClusterTester.WriteAttribute(Attributes::StartUpOnOff::Id, StartUpOnOffEnum::kToggle), CHIP_NO_ERROR);
    EXPECT_FALSE(mMockDelegate.mOnOff);
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::StartUpOnOff::Id, startUpOnOff), CHIP_NO_ERROR);
    EXPECT_FALSE(startUpOnOff.IsNull());
    EXPECT_EQ(startUpOnOff.Value(), StartUpOnOffEnum::kToggle);

    // Step 5: Write StartUpOnOff to Null. OnOff state should not change.
    EXPECT_EQ(mClusterTester.WriteAttribute(Attributes::StartUpOnOff::Id, DataModel::Nullable<StartUpOnOffEnum>()), CHIP_NO_ERROR);
    EXPECT_FALSE(mMockDelegate.mOnOff);
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::StartUpOnOff::Id, startUpOnOff), CHIP_NO_ERROR);
    EXPECT_TRUE(startUpOnOff.IsNull());
}

TEST_F(TestOnOffLightingCluster, TestWriteInvalidStartUpOnOff)
{
    // Step 1: Initial State: StartUpOnOff is null.
    DataModel::Nullable<StartUpOnOffEnum> startUpOnOff;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::StartUpOnOff::Id, startUpOnOff), CHIP_NO_ERROR);
    EXPECT_TRUE(startUpOnOff.IsNull());

    // Step 2: Attempt to write an invalid enum value.
    DataModel::Nullable<uint8_t> invalidValue = 0x99;
    DataModel::ActionReturnStatus result      = mClusterTester.WriteAttribute(Attributes::StartUpOnOff::Id, invalidValue);
    EXPECT_TRUE(result.IsError());
    EXPECT_EQ(result, Status::ConstraintError);
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::StartUpOnOff::Id, startUpOnOff), CHIP_NO_ERROR);
    EXPECT_TRUE(startUpOnOff.IsNull());

    // Step 3: Write a valid value first.
    EXPECT_EQ(mClusterTester.WriteAttribute(Attributes::StartUpOnOff::Id, StartUpOnOffEnum::kOn), CHIP_NO_ERROR);
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::StartUpOnOff::Id, startUpOnOff), CHIP_NO_ERROR);
    EXPECT_EQ(startUpOnOff.Value(), StartUpOnOffEnum::kOn);

    // Step 4: Attempt to write an invalid enum value again.
    result = mClusterTester.WriteAttribute(Attributes::StartUpOnOff::Id, invalidValue);
    EXPECT_TRUE(result.IsError());
    EXPECT_EQ(result, Status::ConstraintError);
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::StartUpOnOff::Id, startUpOnOff), CHIP_NO_ERROR);
    EXPECT_FALSE(startUpOnOff.IsNull());
    EXPECT_EQ(startUpOnOff.Value(), StartUpOnOffEnum::kOn);
}

TEST_F(TestOnOffLightingCluster, TestOnWithTimedOff_OnTimeActive)
{
    // Step 1: Turn On with Timed Off (OnTime = 10, OffWaitTime = 0).
    Commands::OnWithTimedOff::Type command1;
    command1.onTime      = 10;
    command1.offWaitTime = 0;
    EXPECT_TRUE(mClusterTester.Invoke(command1).IsSuccess());
    EXPECT_TRUE(mMockTimerDelegate.IsTimerActive(&mCluster));

    // Step 2: Call OnWithTimedOff again with a smaller OnTime (5).
    // OnTime should remain 10 because the new value is not greater.
    Commands::OnWithTimedOff::Type command2;
    command2.onTime      = 5;
    command2.offWaitTime = 0;
    EXPECT_TRUE(mClusterTester.Invoke(command2).IsSuccess());

    uint16_t onTime = 0;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OnTime::Id, onTime), CHIP_NO_ERROR);
    EXPECT_EQ(onTime, 10);
    EXPECT_TRUE(mMockTimerDelegate.IsTimerActive(&mCluster));

    // Step 3: Call OnWithTimedOff again with a larger OnTime (15).
    // OnTime should update to 15.
    Commands::OnWithTimedOff::Type command3;
    command3.onTime      = 15;
    command3.offWaitTime = 0;
    EXPECT_TRUE(mClusterTester.Invoke(command3).IsSuccess());

    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OnTime::Id, onTime), CHIP_NO_ERROR);
    EXPECT_EQ(onTime, 15);
    EXPECT_TRUE(mMockTimerDelegate.IsTimerActive(&mCluster));
}

TEST_F(TestOnOffLightingCluster, TestWriteOnTimeWhenOffWaitActive)
{
    // Step 1: Turn ON, set OffWaitTime, then turn OFF to start the OffWaitTime timer.
    EXPECT_TRUE(mClusterTester.Invoke(Commands::On::Type()).IsSuccess());
    mCluster.SetOffWaitTime(10);
    EXPECT_TRUE(mClusterTester.Invoke(Commands::Off::Type()).IsSuccess());
    EXPECT_TRUE(mMockTimerDelegate.IsTimerActive(&mCluster));
    EXPECT_FALSE(mMockDelegate.mOnOff);

    // Step 2: Write to OnTime. This should not affect the active OffWaitTime timer.
    EXPECT_EQ(mClusterTester.WriteAttribute(Attributes::OnTime::Id, static_cast<uint16_t>(5)), CHIP_NO_ERROR);

    EXPECT_TRUE(mMockTimerDelegate.IsTimerActive(&mCluster));
    uint16_t onTime = 0;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OnTime::Id, onTime), CHIP_NO_ERROR);
    EXPECT_EQ(onTime, 5);
}

TEST_F(TestOnOffLightingCluster, TestWriteOffWaitTimeWhenOnTimeActive)
{
    // Step 1: Turn ON with OnTime to start the OnTime timer.
    Commands::OnWithTimedOff::Type command1;
    command1.onTime      = 10;
    command1.offWaitTime = 0;
    EXPECT_TRUE(mClusterTester.Invoke(command1).IsSuccess());
    EXPECT_TRUE(mMockTimerDelegate.IsTimerActive(&mCluster));
    EXPECT_TRUE(mMockDelegate.mOnOff);

    // Step 2: Write to OffWaitTime. This should not affect the active OnTime timer.
    EXPECT_EQ(mClusterTester.WriteAttribute(Attributes::OffWaitTime::Id, static_cast<uint16_t>(5)), CHIP_NO_ERROR);

    EXPECT_TRUE(mMockTimerDelegate.IsTimerActive(&mCluster));
    uint16_t offWaitTime = 0;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OffWaitTime::Id, offWaitTime), CHIP_NO_ERROR);
    EXPECT_EQ(offWaitTime, 5);
}

TEST_F(TestOnOffLightingCluster, TestOnWithRecallGlobalScene_NullDelegate)
{
    // Step 1: Set GlobalSceneControl to false using the default delegate.
    EXPECT_TRUE(mClusterTester.Invoke(Commands::On::Type()).IsSuccess());
    Commands::OffWithEffect::Type offCommand;
    offCommand.effectIdentifier = EffectIdentifierEnum::kDyingLight;
    offCommand.effectVariant    = 0;
    EXPECT_TRUE(mClusterTester.Invoke(offCommand).IsSuccess());
    bool globalSceneControl = true;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::GlobalSceneControl::Id, globalSceneControl), CHIP_NO_ERROR);
    EXPECT_FALSE(globalSceneControl);

    // Step 2: Re-setup the cluster with a null scenes delegate.
    MockOnOffDelegate localMockDelegate;
    OnOffLightingCluster cluster(kTestEndpointId,
                                 {
                                     .timerDelegate  = mMockTimerDelegate,
                                     .effectDelegate = mMockEffectDelegate,
                                 });
    ClusterTester tester(cluster);
    cluster.AddDelegate(&localMockDelegate);
    EXPECT_EQ(cluster.Startup(tester.GetServerClusterContext()), CHIP_NO_ERROR);

    // Step 3: Set GlobalSceneControl to false again with the new cluster instance.
    EXPECT_TRUE(tester.Invoke(Commands::On::Type()).IsSuccess());
    EXPECT_TRUE(tester.Invoke(offCommand).IsSuccess());
    EXPECT_EQ(tester.ReadAttribute(Attributes::GlobalSceneControl::Id, globalSceneControl), CHIP_NO_ERROR);
    EXPECT_FALSE(globalSceneControl);

    // Step 4: Invoke OnWithRecallGlobalScene.
    // Should turn on the device as a fallback since the scenes delegate is null.
    EXPECT_TRUE(tester.Invoke(Commands::OnWithRecallGlobalScene::Type()).IsSuccess());

    EXPECT_TRUE(localMockDelegate.mOnOff);
    EXPECT_EQ(tester.ReadAttribute(Attributes::GlobalSceneControl::Id, globalSceneControl), CHIP_NO_ERROR);
    EXPECT_TRUE(globalSceneControl);
}

TEST_F(TestOnOffLightingCluster, TestToggleCommand)
{
    // Step 1: Ensure device is OFF.
    EXPECT_FALSE(mMockDelegate.mOnOff);

    // Step 2: Invoke Toggle command. Device should turn ON.
    EXPECT_TRUE(mClusterTester.Invoke(Commands::Toggle::Type()).IsSuccess());
    EXPECT_TRUE(mMockDelegate.mOnOff);
    bool onOffState = false;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OnOff::Id, onOffState), CHIP_NO_ERROR);
    EXPECT_TRUE(onOffState);

    // Step 3: Invoke Toggle command again. Device should turn OFF.
    EXPECT_TRUE(mClusterTester.Invoke(Commands::Toggle::Type()).IsSuccess());
    EXPECT_FALSE(mMockDelegate.mOnOff);
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OnOff::Id, onOffState), CHIP_NO_ERROR);
    EXPECT_FALSE(onOffState);
}

TEST_F(TestOnOffLightingCluster, TestOffToTimedOn)
{
    // Step 1: Ensure device is OFF.
    EXPECT_FALSE(mMockDelegate.mOnOff);
    EXPECT_FALSE(mMockTimerDelegate.IsTimerActive(&mCluster));

    // Step 2: Invoke OnWithTimedOff command.
    Commands::OnWithTimedOff::Type command;
    command.onTime      = 50;
    command.offWaitTime = 30;

    EXPECT_TRUE(mClusterTester.Invoke(command).IsSuccess());

    // Step 3: Verify the state transition to ON.
    EXPECT_TRUE(mMockDelegate.mOnOff);
    bool onOffState = false;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OnOff::Id, onOffState), CHIP_NO_ERROR);
    EXPECT_TRUE(onOffState);

    // Step 4: Verify OnTime and OffWaitTime attributes.
    uint16_t onTime = 0;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OnTime::Id, onTime), CHIP_NO_ERROR);
    EXPECT_EQ(onTime, 50);

    uint16_t offWaitTime = 0;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OffWaitTime::Id, offWaitTime), CHIP_NO_ERROR);
    EXPECT_EQ(offWaitTime, 30);

    // Step 5: Verify that the timer is active.
    EXPECT_TRUE(mMockTimerDelegate.IsTimerActive(&mCluster));
}

TEST_F(TestOnOffLightingCluster, TestOnToTimedOn)
{
    // Step 1: Ensure device is ON.
    EXPECT_TRUE(mClusterTester.Invoke(Commands::On::Type()).IsSuccess());
    EXPECT_TRUE(mMockDelegate.mOnOff);
    EXPECT_FALSE(mMockTimerDelegate.IsTimerActive(&mCluster));

    // Step 2: Invoke OnWithTimedOff command.
    Commands::OnWithTimedOff::Type command;
    command.onTime      = 50;
    command.offWaitTime = 30;

    EXPECT_TRUE(mClusterTester.Invoke(command).IsSuccess());

    // Step 3: Verify the state remains ON.
    EXPECT_TRUE(mMockDelegate.mOnOff);
    bool onOffState = false;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OnOff::Id, onOffState), CHIP_NO_ERROR);
    EXPECT_TRUE(onOffState);

    // Step 4: Verify OnTime and OffWaitTime attributes. (attributes and getters as well)
    uint16_t onTime = 0;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OnTime::Id, onTime), CHIP_NO_ERROR);
    EXPECT_EQ(onTime, 50);

    uint16_t offWaitTime = 0;
    EXPECT_EQ(mClusterTester.ReadAttribute(Attributes::OffWaitTime::Id, offWaitTime), CHIP_NO_ERROR);
    EXPECT_EQ(offWaitTime, 30);

    // via getters
    EXPECT_EQ(mCluster.GetOnTime(), 50);
    EXPECT_EQ(mCluster.GetOffWaitTime(), 30);

    // Step 5: Verify that the timer is active.
    EXPECT_TRUE(mMockTimerDelegate.IsTimerActive(&mCluster));
}

} // namespace
