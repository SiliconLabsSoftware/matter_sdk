/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
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

#include <stdint.h>

#ifdef __cplusplus
constexpr uint8_t kLogHeader     = 0x01; // ASCII Start of Heading
constexpr uint8_t kLogFooter     = 0x04; // ASCII End of Transmission
constexpr uint8_t kHeaderSize    = 1;
constexpr uint8_t kFooterSize    = 1;
constexpr uint8_t kEndOfLineSize = 2; // \r\n
#else
#define kLogHeader     0x01
#define kLogFooter     0x04
#define kHeaderSize    1
#define kFooterSize    1
#define kEndOfLineSize 2
#endif

#ifdef __cplusplus
extern "C" {
#endif

void uartConsoleInit(void);
// SiWx917: brings up ULP UART during sl_platform_init. No-op on other platforms.
void uartEarlyInit(void);
int16_t uartConsoleWrite(const char * Buf, uint16_t BufLength);
int16_t uartLogWrite(const char * log, uint8_t length, uint8_t category, uint64_t timestamp);
int16_t uartConsoleRead(char * Buf, uint16_t NbBytesToRead);
void uartFlushTxQueue(void);
void uartForceTransmit(const char * data, uint16_t length);

void uartMainLoop(void * args);

void sendLogImmediately(bool sendNow);

// Implemented by in openthread code
#ifndef PW_RPC_ENABLED
extern void otPlatUartReceived(const uint8_t * aBuf, uint16_t aBufLength);
extern void otPlatUartSendDone(void);
extern void otSysEventSignalPending(void);
#endif

#ifdef __cplusplus
} // extern "C"
#endif
