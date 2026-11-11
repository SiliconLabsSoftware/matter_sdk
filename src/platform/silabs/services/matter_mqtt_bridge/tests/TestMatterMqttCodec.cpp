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

#include <DeviceIdSource.h>
#include <MatterMqttCodec.h>
#include <MatterMqttContract.h>

#include <cstring>

#include <lib/support/Span.h>
#include <pw_unit_test/framework.h>

TEST(MatterMqttCodecTest, EncodesStringReport)
{
    MatterMqttJsonCodec codec;
    char json[256];
    EXPECT_EQ(codec.EncodeReport(false, chip::CharSpan::fromCharString("on"), json, sizeof(json)), CHIP_NO_ERROR);
    EXPECT_STREQ(json, R"({"value":"on"})");
}

TEST(MatterMqttCodecTest, EncodesNumericReport)
{
    MatterMqttJsonCodec codec;
    char json[256];
    EXPECT_EQ(codec.EncodeReport(true, chip::CharSpan::fromCharString("2100"), json, sizeof(json)), CHIP_NO_ERROR);
    EXPECT_STREQ(json, R"({"value":2100})");
}

TEST(MatterMqttCodecTest, DecodeEmptyArgsObject)
{
    MatterMqttJsonCodec codec;
    const char * payload = "{}";
    MatterMqttCodecTypes::ArgsView args;
    EXPECT_EQ(codec.DecodeArgs(chip::ByteSpan(reinterpret_cast<const uint8_t *>(payload), strlen(payload)), args),
              CHIP_NO_ERROR);
    EXPECT_EQ(args.count, 0u);
}

TEST(MatterMqttContractTest, FormatDeviceId)
{
    char buf[MATTER_MQTT_DEVICE_ID_LEN];
    EXPECT_EQ(FormatDeviceId(0x1122334455667788ULL, buf, sizeof(buf)), CHIP_NO_ERROR);
    EXPECT_STREQ(buf, "0x1122334455667788");
}
