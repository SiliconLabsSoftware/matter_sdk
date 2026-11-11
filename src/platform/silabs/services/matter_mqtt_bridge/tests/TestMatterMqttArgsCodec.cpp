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

#include <ClusterAdapter.h>
#include <MatterMqttCodec.h>

#include <cstring>

#include <lib/support/Span.h>
#include <pw_unit_test/framework.h>

TEST(MatterMqttArgsCodecTest, DecodesNumericArgs)
{
    MatterMqttJsonCodec codec;
    const char * payload = R"({"args":{"mode":1,"amount":-3}})";
    MatterMqttCodecTypes::ArgsView args;
    ASSERT_EQ(codec.DecodeArgs(chip::ByteSpan(reinterpret_cast<const uint8_t *>(payload), strlen(payload)), args), CHIP_NO_ERROR);
    EXPECT_EQ(args.count, 2u);
    EXPECT_STREQ(args.items[0].name, "mode");
    EXPECT_STREQ(args.items[0].value, "1");
}

TEST(MatterMqttArgsCodecTest, PacksThermostatCommandArgs)
{
    StaticClusterAdapterRegistry registry(chip::Span<const ClusterAdapter>(gAdapters, gAdapterCount));
    const ClusterAdapter * adapter = registry.FindByClusterName(chip::CharSpan::fromCharString("Thermostat"));
    ASSERT_NE(adapter, nullptr);

    chip::CommandId cmdId = 0;
    const MatterMqttCommandEntry * entry = nullptr;
    ASSERT_TRUE(registry.ResolveCommandId(adapter, chip::CharSpan::fromCharString("SetpointRaiseLower"), cmdId, &entry));

    MatterMqttCodecTypes::ArgsView args;
    args.count           = 2;
    args.items[0].name   = "mode";
    args.items[0].value  = "1";
    args.items[1].name   = "amount";
    args.items[1].value  = "-3";

    uint8_t packed[MATTER_MQTT_MAX_CMD_ARG_PACK] = {};
    uint8_t packedLen                            = 0;
    EXPECT_EQ(registry.PackCommandArgs(*entry, args, packed, packedLen), CHIP_NO_ERROR);
    EXPECT_EQ(packedLen, 2u);
    EXPECT_EQ(packed[0], 1u);
    EXPECT_EQ(static_cast<int8_t>(packed[1]), -3);
}
