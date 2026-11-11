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
#include <DeviceIdSource.h>
#include <MatterMqttTopic.h>

#include <cstring>

#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>
#include <pw_unit_test/framework.h>

TEST(MatterMqttTopicTest, BuildsAttributeTopic)
{
    char topic[MATTER_MQTT_MAX_TOPIC_LEN];
    EXPECT_EQ(MatterMqttTopic::BuildAttributeTopic(chip::CharSpan::fromCharString("0x1122334455667788"), 1,
                                                   chip::CharSpan::fromCharString("OnOff"),
                                                   chip::CharSpan::fromCharString("OnOff"), topic, sizeof(topic)),
              CHIP_NO_ERROR);
    EXPECT_STREQ(topic, "matter/v1/0x1122334455667788/ep/1/cluster/OnOff/attr/OnOff");
}

TEST(MatterMqttTopicTest, BuildsWildcardCommandFilter)
{
    char filter[MATTER_MQTT_MAX_TOPIC_LEN];
    EXPECT_EQ(MatterMqttTopic::BuildCommandFilter(chip::CharSpan::fromCharString("0xAABBCCDDEEFF0011"), true, filter,
                                                  sizeof(filter)),
              CHIP_NO_ERROR);
    EXPECT_STREQ(filter, "matter/v1/0xAABBCCDDEEFF0011/ep/+/cluster/+/cmd/+");
}

TEST(MatterMqttTopicTest, ParsesCommandTopic)
{
    const char * topic = "matter/v1/0x1122334455667788/ep/1/cluster/OnOff/cmd/Toggle";
    MatterMqttTopic::ParsedCommandTopic parsed;
    EXPECT_EQ(MatterMqttTopic::ParseCommandTopic(chip::CharSpan::fromCharString(topic),
                                                 chip::CharSpan::fromCharString("0x1122334455667788"), parsed),
              CHIP_NO_ERROR);
    EXPECT_EQ(parsed.endpoint, 1u);
    EXPECT_STREQ(parsed.clusterName, "OnOff");
    EXPECT_STREQ(parsed.commandName, "Toggle");
}

TEST(MatterMqttTopicTest, RejectsNodeIdMismatch)
{
    const char * topic = "matter/v1/0xFFFFFFFFFFFFFFFF/ep/1/cluster/OnOff/cmd/On";
    MatterMqttTopic::ParsedCommandTopic parsed;
    EXPECT_NE(MatterMqttTopic::ParseCommandTopic(chip::CharSpan::fromCharString(topic),
                                                 chip::CharSpan::fromCharString("0x1122334455667788"), parsed),
              CHIP_NO_ERROR);
}

TEST(MatterMqttAdapterRegistryTest, ResolvesAlias)
{
    StaticClusterAdapterRegistry registry(chip::Span<const ClusterAdapter>(gAdapters, gAdapterCount));
    const ClusterAdapter * adapter = registry.FindByClusterName(chip::CharSpan::fromCharString("OnOff"));
    ASSERT_NE(adapter, nullptr);

    chip::CommandId cmdId                = 0;
    const MatterMqttCommandEntry * entry = nullptr;
    EXPECT_TRUE(registry.ResolveCommandId(adapter, chip::CharSpan::fromCharString("toggle"), cmdId, &entry));
    EXPECT_NE(entry, nullptr);
}
