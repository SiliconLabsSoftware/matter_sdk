/*
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

#pragma once

#include <cstring>

#include <lib/core/CHIPError.h>
#include <lib/support/logging/CHIPLogging.h>

namespace DeviceChipCryptoPALTestDetail {
inline bool & FailureFlag()
{
    static bool failed = false;
    return failed;
}
} // namespace DeviceChipCryptoPALTestDetail

#define TEST_F(suite, name) static CHIP_ERROR DeviceCryptoTest_##name()

#define DEVICE_TEST_FAIL()                                                                                                         \
    do                                                                                                                             \
    {                                                                                                                              \
        ChipLogError(Crypto, "Device crypto test failed at %s:%d", __FILE__ + 119, __LINE__);                                      \
        DeviceChipCryptoPALTestDetail::FailureFlag() = true;                                                                       \
    } while (0)

#define DEVICE_TEST_RESULT() (DeviceChipCryptoPALTestDetail::FailureFlag() ? CHIP_ERROR_INTERNAL : CHIP_NO_ERROR)

#define EXPECT_EQ(a, b)                                                                                                            \
    do                                                                                                                             \
    {                                                                                                                              \
        if (!((a) == (b)))                                                                                                         \
        {                                                                                                                          \
            DEVICE_TEST_FAIL();                                                                                                    \
        }                                                                                                                          \
    } while (0)

#define EXPECT_NE(a, b)                                                                                                            \
    do                                                                                                                             \
    {                                                                                                                              \
        if (!((a) != (b)))                                                                                                         \
        {                                                                                                                          \
            DEVICE_TEST_FAIL();                                                                                                    \
        }                                                                                                                          \
    } while (0)

#define EXPECT_TRUE(x)                                                                                                             \
    do                                                                                                                             \
    {                                                                                                                              \
        if (!(x))                                                                                                                  \
        {                                                                                                                          \
            DEVICE_TEST_FAIL();                                                                                                    \
        }                                                                                                                          \
    } while (0)

#define EXPECT_SUCCESS(err) EXPECT_EQ((err), CHIP_NO_ERROR)

#define EXPECT_LE(a, b)                                                                                                            \
    do                                                                                                                             \
    {                                                                                                                              \
        if (!((a) <= (b)))                                                                                                         \
        {                                                                                                                          \
            DEVICE_TEST_FAIL();                                                                                                    \
        }                                                                                                                          \
    } while (0)

#define EXPECT_GT(a, b)                                                                                                            \
    do                                                                                                                             \
    {                                                                                                                              \
        if (!((a) > (b)))                                                                                                          \
        {                                                                                                                          \
            DEVICE_TEST_FAIL();                                                                                                    \
        }                                                                                                                          \
    } while (0)

#define ASSERT_EQ(a, b) EXPECT_EQ(a, b)
#define ASSERT_TRUE(x) EXPECT_TRUE(x)
#define ASSERT_SUCCESS(err) EXPECT_SUCCESS(err)
#define ASSERT_GT(a, b) EXPECT_GT(a, b)

#define EXPECT_FALSE(x) EXPECT_TRUE(!(x))

#define ADD_FAILURE() DEVICE_TEST_FAIL()

#define GTEST_SKIP() return CHIP_NO_ERROR

#define EXPECT_STREQ(a, b)                                                                                                         \
    do                                                                                                                             \
    {                                                                                                                              \
        if (strcmp((a), (b)) != 0)                                                                                                 \
        {                                                                                                                          \
            DEVICE_TEST_FAIL();                                                                                                    \
        }                                                                                                                          \
    } while (0)
