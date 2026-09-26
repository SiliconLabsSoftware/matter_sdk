/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
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

 #include <lib/support/CHIPMem.h>
 #include <pw_unit_test/framework.h>

 #include <platform/silabs/multi-ota/OTATlvProcessor.h>

 using namespace chip;
 using namespace chip::DeviceLayer::Silabs::MultiOTA;

 namespace {

 CHIP_ERROR StubProcessDescriptor(void * /* descriptor */)
 {
     return CHIP_NO_ERROR;
 }

 class TestableOTATlvProcessor : public OTATlvProcessor
 {
 public:
     CHIP_ERROR nextProcessInternalStatus = CHIP_NO_ERROR;
     CHIP_ERROR nextExitActionStatus     = CHIP_NO_ERROR;
     size_t lastProcessInternalBlockSize  = 0;

     using OTATlvProcessor::IsError;
     using OTATlvProcessor::mLength;
     using OTATlvProcessor::mProcessedLength;

     CHIP_ERROR ProcessInternal(ByteSpan & block) override
     {
         lastProcessInternalBlockSize = block.size();
         return nextProcessInternalStatus;
     }

     CHIP_ERROR ExitAction() override { return nextExitActionStatus; }

     CHIP_ERROR ApplyAction() override { return CHIP_NO_ERROR; }

     CHIP_ERROR FinalizeAction() override { return CHIP_NO_ERROR; }
 };

 class OTATlvProcessorFixture : public ::testing::Test
 {
 public:
     static void SetUpTestSuite() { ASSERT_EQ(Platform::MemoryInit(), CHIP_NO_ERROR); }

     static void TearDownTestSuite() { Platform::MemoryShutdown(); }

     void SetUp() override
     {
         mProcessor.RegisterDescriptorCallback(StubProcessDescriptor);
         ASSERT_EQ(mProcessor.Init(), CHIP_NO_ERROR);
         mProcessor.nextProcessInternalStatus = CHIP_NO_ERROR;
         mProcessor.nextExitActionStatus      = CHIP_NO_ERROR;
         mProcessor.lastProcessInternalBlockSize = 0;
     }

 protected:
     TestableOTATlvProcessor mProcessor;
 };

 } // namespace

 TEST(OTATlvProcessorIsValidTag, AcceptsKnownTagsAndRejectsOutOfRange)
 {
     TestableOTATlvProcessor processor;

     EXPECT_TRUE(processor.IsValidTag(OTAProcessorTag::kApplicationProcessor));
     EXPECT_TRUE(processor.IsValidTag(OTAProcessorTag::kBootloaderProcessor));
     EXPECT_TRUE(processor.IsValidTag(OTAProcessorTag::kFactoryDataProcessor));
     EXPECT_TRUE(processor.IsValidTag(OTAProcessorTag::kMaxValue));

     EXPECT_FALSE(processor.IsValidTag(static_cast<OTAProcessorTag>(0)));
     EXPECT_FALSE(processor.IsValidTag(static_cast<OTAProcessorTag>(12)));
 }

 TEST_F(OTATlvProcessorFixture, IsErrorTreatsProgressCodesAsNonErrors)
 {
     CHIP_ERROR success = CHIP_NO_ERROR;
     CHIP_ERROR bufferTooSmall = CHIP_ERROR_BUFFER_TOO_SMALL;
     CHIP_ERROR fetchScheduled = CHIP_OTA_FETCH_ALREADY_SCHEDULED;
     CHIP_ERROR failure = CHIP_ERROR_INTERNAL;

     EXPECT_FALSE(mProcessor.IsError(success));
     EXPECT_FALSE(mProcessor.IsError(bufferTooSmall));
     EXPECT_FALSE(mProcessor.IsError(fetchScheduled));
     EXPECT_TRUE(mProcessor.IsError(failure));
 }

 TEST(OTADataAccumulator, AccumulatesUntilThreshold)
 {
     OTADataAccumulator accumulator;
     accumulator.Init(4);

     uint8_t chunk1[] = { 0x01, 0x02 };
     ByteSpan block1(chunk1);
     EXPECT_EQ(accumulator.Accumulate(block1), CHIP_ERROR_BUFFER_TOO_SMALL);
     EXPECT_TRUE(block1.empty());

     uint8_t chunk2[] = { 0x03, 0x04 };
     ByteSpan block2(chunk2);
     EXPECT_EQ(accumulator.Accumulate(block2), CHIP_NO_ERROR);
     EXPECT_TRUE(block2.empty());
     EXPECT_EQ(accumulator.data()[0], 0x01);
     EXPECT_EQ(accumulator.data()[3], 0x04);

     accumulator.Clear();
 }

 TEST(OTATlvProcessorInit, RequiresDescriptorCallback)
 {
     TestableOTATlvProcessor processor;
     EXPECT_EQ(processor.Init(), CHIP_OTA_PROCESSOR_CB_NOT_REGISTERED);
 }

 TEST(OTATlvProcessorInit, SucceedsWithDescriptorCallback)
 {
     TestableOTATlvProcessor processor;
     processor.RegisterDescriptorCallback(StubProcessDescriptor);
     EXPECT_EQ(processor.Init(), CHIP_NO_ERROR);
 }

 TEST(OTATlvProcessorClear, ResetsProcessorState)
 {
     TestableOTATlvProcessor processor;
     processor.RegisterDescriptorCallback(StubProcessDescriptor);
     ASSERT_EQ(processor.Init(), CHIP_NO_ERROR);

     processor.SetLength(100);
     processor.mProcessedLength = 50;
     processor.SetWasSelected(true);

     EXPECT_EQ(processor.Clear(), CHIP_NO_ERROR);
     EXPECT_EQ(processor.mLength, 0u);
     EXPECT_EQ(processor.mProcessedLength, 0u);
     EXPECT_FALSE(processor.WasSelected());
 }

 TEST(OTADataAccumulator, LeavesRemainderInBlock)
 {
     OTADataAccumulator accumulator;
     accumulator.Init(4);

     uint8_t input[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
     ByteSpan block(input);
     EXPECT_EQ(accumulator.Accumulate(block), CHIP_NO_ERROR);
     EXPECT_EQ(block.size(), 2u);

     accumulator.Clear();
 }

 TEST_F(OTATlvProcessorFixture, IsErrorTreatsOtaChangeProcessorAsFailure)
 {
     CHIP_ERROR status = CHIP_OTA_CHANGE_PROCESSOR;
     EXPECT_TRUE(mProcessor.IsError(status));
 }

 TEST_F(OTATlvProcessorFixture, ProcessPartialBlockReturnsNoError)
 {
     mProcessor.SetLength(8);

     uint8_t input[] = { 0x01, 0x02, 0x03, 0x04 };
     ByteSpan block(input);
     EXPECT_EQ(mProcessor.Process(block), CHIP_NO_ERROR);
     EXPECT_EQ(mProcessor.mProcessedLength, 4u);
     EXPECT_TRUE(block.empty());
 }

 TEST_F(OTATlvProcessorFixture, ProcessCompleteBlockReturnsChangeProcessor)
 {
     mProcessor.SetLength(4);

     uint8_t input[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
     ByteSpan block(input);
     EXPECT_EQ(mProcessor.Process(block), CHIP_OTA_CHANGE_PROCESSOR);
     EXPECT_EQ(mProcessor.mProcessedLength, 4u);
     EXPECT_EQ(block.size(), 2u);
     EXPECT_EQ(mProcessor.lastProcessInternalBlockSize, 4u);
 }

 TEST_F(OTATlvProcessorFixture, ProcessFatalErrorDoesNotAdvance)
 {
     mProcessor.SetLength(8);
     mProcessor.nextProcessInternalStatus = CHIP_ERROR_INTERNAL;

     uint8_t input[] = { 0x01, 0x02, 0x03, 0x04 };
     ByteSpan block(input);
     EXPECT_EQ(mProcessor.Process(block), CHIP_ERROR_INTERNAL);
     EXPECT_EQ(mProcessor.mProcessedLength, 0u);
     EXPECT_EQ(block.size(), 4u);
 }

 TEST_F(OTATlvProcessorFixture, ProcessBufferTooSmallStillAdvances)
 {
     mProcessor.SetLength(8);
     mProcessor.nextProcessInternalStatus = CHIP_ERROR_BUFFER_TOO_SMALL;

     uint8_t input[] = { 0x01, 0x02, 0x03, 0x04 };
     ByteSpan block(input);
     EXPECT_EQ(mProcessor.Process(block), CHIP_ERROR_BUFFER_TOO_SMALL);
     EXPECT_EQ(mProcessor.mProcessedLength, 4u);
     EXPECT_TRUE(block.empty());
 }

 TEST_F(OTATlvProcessorFixture, ProcessFetchScheduledStillAdvances)
 {
     mProcessor.SetLength(8);
     mProcessor.nextProcessInternalStatus = CHIP_OTA_FETCH_ALREADY_SCHEDULED;

     uint8_t input[] = { 0x01, 0x02, 0x03, 0x04 };
     ByteSpan block(input);
     EXPECT_EQ(mProcessor.Process(block), CHIP_OTA_FETCH_ALREADY_SCHEDULED);
     EXPECT_EQ(mProcessor.mProcessedLength, 4u);
     EXPECT_TRUE(block.empty());
 }

 TEST_F(OTATlvProcessorFixture, ProcessMultiChunkUntilComplete)
 {
     mProcessor.SetLength(10);

     uint8_t chunk1[] = { 0x01, 0x02, 0x03 };
     ByteSpan block1(chunk1);
     EXPECT_EQ(mProcessor.Process(block1), CHIP_NO_ERROR);
     EXPECT_EQ(mProcessor.mProcessedLength, 3u);

     uint8_t chunk2[] = { 0x04, 0x05, 0x06 };
     ByteSpan block2(chunk2);
     EXPECT_EQ(mProcessor.Process(block2), CHIP_NO_ERROR);
     EXPECT_EQ(mProcessor.mProcessedLength, 6u);

     uint8_t chunk3[] = { 0x07, 0x08, 0x09, 0x0A };
     ByteSpan block3(chunk3);
     EXPECT_EQ(mProcessor.Process(block3), CHIP_OTA_CHANGE_PROCESSOR);
     EXPECT_EQ(mProcessor.mProcessedLength, 10u);
 }

 TEST_F(OTATlvProcessorFixture, ProcessExactLengthWithNoRemainder)
 {
     mProcessor.SetLength(4);

     uint8_t input[] = { 0x01, 0x02, 0x03, 0x04 };
     ByteSpan block(input);
     EXPECT_EQ(mProcessor.Process(block), CHIP_OTA_CHANGE_PROCESSOR);
     EXPECT_EQ(mProcessor.mProcessedLength, 4u);
     EXPECT_TRUE(block.empty());
 }

 TEST_F(OTATlvProcessorFixture, ProcessFatalErrorMidStream)
 {
     mProcessor.SetLength(12);

     uint8_t chunk1[] = { 0x01, 0x02, 0x03, 0x04 };
     ByteSpan block1(chunk1);
     EXPECT_EQ(mProcessor.Process(block1), CHIP_NO_ERROR);
     EXPECT_EQ(mProcessor.mProcessedLength, 4u);

     mProcessor.nextProcessInternalStatus = CHIP_ERROR_INTERNAL;

     uint8_t chunk2[] = { 0x05, 0x06, 0x07, 0x08 };
     ByteSpan block2(chunk2);
     EXPECT_EQ(mProcessor.Process(block2), CHIP_ERROR_INTERNAL);
     EXPECT_EQ(mProcessor.mProcessedLength, 4u);
     EXPECT_EQ(block2.size(), 4u);
 }

 TEST_F(OTATlvProcessorFixture, ProcessExitActionFailure)
 {
     mProcessor.SetLength(4);
     mProcessor.nextExitActionStatus = CHIP_ERROR_INTERNAL;

     uint8_t input[] = { 0x01, 0x02, 0x03, 0x04 };
     ByteSpan block(input);
     EXPECT_EQ(mProcessor.Process(block), CHIP_ERROR_INTERNAL);
     EXPECT_EQ(mProcessor.mProcessedLength, 4u);
 }
