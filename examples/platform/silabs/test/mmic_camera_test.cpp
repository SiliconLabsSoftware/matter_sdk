/*******************************************************************************
 * @file
 * @brief Host side test of the camera command forwarding over MMIC.
 *******************************************************************************
 * # License
 * <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#include "mmic_camera_test.h"

#include "mmic.h"
#include "mmic_cluster_names.h"

#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr uint32_t kAvStreamManagement = 0x0551;
constexpr uint32_t kAvSettings         = 0x0552;
constexpr uint32_t kWebRtcProvider     = 0x0553;

constexpr uint32_t kCaptureSnapshotCommand = 0x0C;

// Interaction Model status code returned to the device.
constexpr uint8_t kStatusSuccess = 0x00;

enum class Kind : uint8_t
{
    kCommand,
    kAttribute,
};

struct Expected
{
    Kind kind;
    uint32_t clusterId;
    uint32_t id; // Command or attribute id
    const char * label;
    bool received;
};

// Messages forwarded by the camera SoC, in the order of the README test sequence.
Expected sExpected[] = {
    { Kind::kCommand, kAvStreamManagement, 0x0B, "Set stream priorities", false },
    { Kind::kCommand, kAvStreamManagement, 0x03, "Video stream allocate", false },
    { Kind::kCommand, kAvStreamManagement, 0x00, "Audio stream allocate", false },
    { Kind::kCommand, kAvStreamManagement, 0x07, "Snapshot stream allocate", false },
    { Kind::kCommand, kAvStreamManagement, kCaptureSnapshotCommand, "Capture snapshot", false },
    { Kind::kAttribute, kAvStreamManagement, 0x18, "Viewport changed", false },
    { Kind::kAttribute, kAvStreamManagement, 0x1D, "Microphone muted changed", false },
    { Kind::kCommand, kWebRtcProvider, 0x00, "Start WebRTC (solicit offer)", false },
    { Kind::kCommand, kWebRtcProvider, 0x02, "Start WebRTC (provide offer)", false },
    { Kind::kCommand, kWebRtcProvider, 0x04, "WebRTC provide answer", false },
    { Kind::kCommand, kWebRtcProvider, 0x05, "WebRTC provide ICE candidates", false },
    { Kind::kCommand, kWebRtcProvider, 0x06, "Stop WebRTC (end session)", false },
    { Kind::kCommand, kAvSettings, 0x00, "Pan/tilt/zoom set position", false },
    { Kind::kCommand, kAvSettings, 0x01, "Pan/tilt/zoom relative move", false },
    { Kind::kCommand, kAvSettings, 0x03, "Pan/tilt/zoom save preset", false },
    { Kind::kCommand, kAvSettings, 0x02, "Pan/tilt/zoom move to preset", false },
    { Kind::kCommand, kAvSettings, 0x04, "Pan/tilt/zoom remove preset", false },
    { Kind::kCommand, kAvSettings, 0x05, "Digital PTZ set viewport", false },
    { Kind::kCommand, kAvSettings, 0x06, "Digital PTZ relative move", false },
    { Kind::kCommand, kAvStreamManagement, 0x06, "Video stream deallocate", false },
    { Kind::kCommand, kAvStreamManagement, 0x02, "Audio stream deallocate", false },
    { Kind::kCommand, kAvStreamManagement, 0x0A, "Snapshot stream deallocate", false },
};

constexpr size_t kExpectedCount = sizeof(sExpected) / sizeof(sExpected[0]);

std::mutex sMutex;
size_t sReceivedCount  = 0;
char sLastMessage[160] = "none";

const char * NameOrUnknown(const char * name)
{
    return (name != NULL) ? name : "Unknown";
}

// Must be called with sMutex held.
void PrintStatus(void)
{
    // Clear the terminal and redraw from the top.
    printf("\033[H\033[2J");
    printf("mmic_host --- TESTING CAMERA support ---\n\n");
    for (const auto & item : sExpected)
    {
        printf("  %-32s -- %s\n", item.label, item.received ? "check" : "waiting");
    }
    printf("\nLast message: %s\n", sLastMessage);
    printf("\nReceived %zu out of %zu\n", sReceivedCount, kExpectedCount);
    printf("ctrl+c to end the test early\n");
    fflush(stdout);
}

void MarkReceived(Kind kind, uint32_t clusterId, uint32_t id)
{
    for (auto & item : sExpected)
    {
        if (item.kind == kind && item.clusterId == clusterId && item.id == id && !item.received)
        {
            item.received = true;
            sReceivedCount++;
            return;
        }
    }
}

// Writes a fake CaptureSnapshotResponse as a Matter TLV anonymous structure:
// { 0: data (octet string), 1: imageCodec (JPEG), 2: resolution { 0: width, 1: height } }
size_t EncodeSnapshotResponse(uint8_t * out)
{
    static const uint8_t kImage[] = { 0xFF, 0xD8, 0xFF, 0xD9 }; // Empty JPEG (SOI + EOI)
    const uint16_t width          = 640;
    const uint16_t height         = 480;

    size_t i = 0;
    out[i++] = 0x15; // Anonymous structure
    out[i++] = 0x30; // Context tag, octet string with 1-byte length
    out[i++] = 0x00;
    out[i++] = sizeof(kImage);
    memcpy(&out[i], kImage, sizeof(kImage));
    i += sizeof(kImage);
    out[i++] = 0x24; // Context tag, unsigned int 1 byte
    out[i++] = 0x01;
    out[i++] = 0x00; // ImageCodecEnum::kJpeg
    out[i++] = 0x35; // Context tag, structure
    out[i++] = 0x02;
    out[i++] = 0x25; // Context tag, unsigned int 2 bytes
    out[i++] = 0x00;
    out[i++] = static_cast<uint8_t>(width & 0xFF);
    out[i++] = static_cast<uint8_t>(width >> 8);
    out[i++] = 0x25;
    out[i++] = 0x01;
    out[i++] = static_cast<uint8_t>(height & 0xFF);
    out[i++] = static_cast<uint8_t>(height >> 8);
    out[i++] = 0x18; // End of resolution
    out[i++] = 0x18; // End of response
    return i;
}

// Builds the answer to a cluster command that expects one: the test accepts every command.
void BuildAnswer(const clusterCommandHeader_t & request, uint8_t ** answer, size_t * answerLen)
{
    uint8_t payload[sizeof(clusterCommandAnswerHeader_t) + 64];
    clusterCommandAnswerHeader_t header;
    header.transactionId = request.transactionId;
    header.status        = kStatusSuccess;

    size_t payloadLen = sizeof(header);
    if (request.clusterId == kAvStreamManagement && request.commandId == kCaptureSnapshotCommand)
    {
        payloadLen += EncodeSnapshotResponse(payload + sizeof(header));
    }
    memcpy(payload, &header, sizeof(header));

    if (mmic_serialize_packet(MMIC_HEADER_ANS, cluster_command, payload, payloadLen, answer, answerLen) != MMIC_ERROR_OK)
    {
        *answer    = NULL;
        *answerLen = 0;
    }
}

} // namespace

void mmic_camera_test_begin(void)
{
    std::lock_guard<std::mutex> lock(sMutex);
    PrintStatus();
}

void mmic_camera_test_handle_request(const uint8_t * frame, size_t len, uint8_t ** answer, size_t * answerLen)
{
    *answer    = NULL;
    *answerLen = 0;

    mmic_command_id_e opcode = INVALID_COMMAND_ID;
    const uint8_t * payload  = NULL;
    uint16_t payloadLen      = 0;
    if (mmic_deserialize_packet(frame, len, MMIC_HEADER_CMD, &opcode, &payload, &payloadLen) != MMIC_ERROR_OK)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(sMutex);

    if (opcode == cluster_command && payloadLen >= sizeof(clusterCommandHeader_t))
    {
        clusterCommandHeader_t header;
        memcpy(&header, payload, sizeof(header));
        MarkReceived(Kind::kCommand, header.clusterId, header.commandId);
        snprintf(sLastMessage, sizeof(sLastMessage), "command %s.%s (endpoint %u)",
                 NameOrUnknown(mmic_cluster_name(header.clusterId)),
                 NameOrUnknown(mmic_command_name(header.clusterId, header.commandId)), (unsigned) header.endpointId);
        if (header.flags & MMIC_CLUSTER_COMMAND_FLAG_ANSWER_EXPECTED)
        {
            BuildAnswer(header, answer, answerLen);
        }
    }
    else if (opcode == attribute_changed && payloadLen >= sizeof(attributeChangedHeader_t))
    {
        attributeChangedHeader_t header;
        memcpy(&header, payload, sizeof(header));
        MarkReceived(Kind::kAttribute, header.clusterId, header.attributeId);
        snprintf(sLastMessage, sizeof(sLastMessage), "attribute %s.%s (endpoint %u)",
                 NameOrUnknown(mmic_cluster_name(header.clusterId)),
                 NameOrUnknown(mmic_attribute_name(header.clusterId, header.attributeId)), (unsigned) header.endpointId);
    }
    else
    {
        snprintf(sLastMessage, sizeof(sLastMessage), "unexpected request (op code %u)", (unsigned) opcode);
    }

    PrintStatus();
}

bool mmic_camera_test_complete(void)
{
    std::lock_guard<std::mutex> lock(sMutex);
    return sReceivedCount == kExpectedCount;
}

void mmic_camera_test_print_summary(bool interrupted)
{
    std::lock_guard<std::mutex> lock(sMutex);
    if (sReceivedCount == kExpectedCount)
    {
        printf("\nSUCCESS: all %zu camera messages received from the SoC.\n", kExpectedCount);
    }
    else
    {
        printf("\nFAILURE: %s, received %zu out of %zu. Missing:\n", interrupted ? "test interrupted" : "test ended",
               sReceivedCount, kExpectedCount);
        for (const auto & item : sExpected)
        {
            if (!item.received)
            {
                printf("  - %s\n", item.label);
            }
        }
    }
    fflush(stdout);
}
