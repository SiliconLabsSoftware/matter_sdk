/*******************************************************************************
 * @file
 * @brief Human readable names for clusters forwarded over MMIC (host side).
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

#include "mmic_cluster_names.h"

#include <stddef.h>

namespace {

struct IdName
{
    uint32_t id;
    const char * name;
};

struct ClusterNames
{
    uint32_t id;
    const char * name;
    const IdName * commands;
    size_t commandCount;
    const IdName * attributes;
    size_t attributeCount;
};

// Camera AV Stream Management (0x0551)
const IdName kAvsmCommands[] = {
    { 0x00, "AudioStreamAllocate" },    { 0x02, "AudioStreamDeallocate" }, { 0x03, "VideoStreamAllocate" },
    { 0x05, "VideoStreamModify" },      { 0x06, "VideoStreamDeallocate" }, { 0x07, "SnapshotStreamAllocate" },
    { 0x09, "SnapshotStreamModify" },   { 0x0A, "SnapshotStreamDeallocate" }, { 0x0B, "SetStreamPriorities" },
    { 0x0C, "CaptureSnapshot" },
};
const IdName kAvsmAttributes[] = {
    { 0x0C, "CurrentFrameRate" },
    { 0x0D, "HDRModeEnabled" },
    { 0x0F, "AllocatedVideoStreams" },
    { 0x10, "AllocatedAudioStreams" },
    { 0x11, "AllocatedSnapshotStreams" },
    { 0x12, "StreamUsagePriorities" },
    { 0x13, "SoftRecordingPrivacyModeEnabled" },
    { 0x14, "SoftLivestreamPrivacyModeEnabled" },
    { 0x15, "HardPrivacyModeOn" },
    { 0x16, "NightVision" },
    { 0x17, "NightVisionIllum" },
    { 0x18, "Viewport" },
    { 0x19, "SpeakerMuted" },
    { 0x1A, "SpeakerVolumeLevel" },
    { 0x1D, "MicrophoneMuted" },
    { 0x1E, "MicrophoneVolumeLevel" },
    { 0x21, "MicrophoneAGCEnabled" },
    { 0x22, "ImageRotation" },
    { 0x23, "ImageFlipHorizontal" },
    { 0x24, "ImageFlipVertical" },
    { 0x25, "LocalVideoRecordingEnabled" },
    { 0x26, "LocalSnapshotRecordingEnabled" },
    { 0x27, "StatusLightEnabled" },
    { 0x28, "StatusLightBrightness" },
};

// Camera AV Settings User Level Management (0x0552)
const IdName kAvsumCommands[] = {
    { 0x00, "MPTZSetPosition" }, { 0x01, "MPTZRelativeMove" }, { 0x02, "MPTZMoveToPreset" }, { 0x03, "MPTZSavePreset" },
    { 0x04, "MPTZRemovePreset" }, { 0x05, "DPTZSetViewport" }, { 0x06, "DPTZRelativeMove" },
};
const IdName kAvsumAttributes[] = {
    { 0x00, "MPTZPosition" }, { 0x02, "MPTZPresets" }, { 0x03, "DPTZStreams" }, { 0x09, "MovementState" },
};

// WebRTC Transport Provider (0x0553)
const IdName kWebRtcProviderCommands[] = {
    { 0x00, "SolicitOffer" }, { 0x02, "ProvideOffer" }, { 0x04, "ProvideAnswer" },
    { 0x05, "ProvideICECandidates" }, { 0x06, "EndSession" },
};
const IdName kWebRtcProviderAttributes[] = {
    { 0x00, "CurrentSessions" },
};

#define MMIC_COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

const ClusterNames kClusters[] = {
    { 0x0551, "CameraAvStreamManagement", kAvsmCommands, MMIC_COUNT_OF(kAvsmCommands), kAvsmAttributes,
      MMIC_COUNT_OF(kAvsmAttributes) },
    { 0x0552, "CameraAvSettingsUserLevelManagement", kAvsumCommands, MMIC_COUNT_OF(kAvsumCommands), kAvsumAttributes,
      MMIC_COUNT_OF(kAvsumAttributes) },
    { 0x0553, "WebRTCTransportProvider", kWebRtcProviderCommands, MMIC_COUNT_OF(kWebRtcProviderCommands),
      kWebRtcProviderAttributes, MMIC_COUNT_OF(kWebRtcProviderAttributes) },
};

const ClusterNames * FindCluster(uint32_t clusterId)
{
    for (const auto & cluster : kClusters)
    {
        if (cluster.id == clusterId)
        {
            return &cluster;
        }
    }
    return NULL;
}

const char * FindName(const IdName * table, size_t count, uint32_t id)
{
    for (size_t i = 0; i < count; i++)
    {
        if (table[i].id == id)
        {
            return table[i].name;
        }
    }
    return NULL;
}

} // namespace

const char * mmic_cluster_name(uint32_t clusterId)
{
    const ClusterNames * cluster = FindCluster(clusterId);
    return (cluster != NULL) ? cluster->name : NULL;
}

const char * mmic_command_name(uint32_t clusterId, uint32_t commandId)
{
    const ClusterNames * cluster = FindCluster(clusterId);
    return (cluster != NULL) ? FindName(cluster->commands, cluster->commandCount, commandId) : NULL;
}

const char * mmic_attribute_name(uint32_t clusterId, uint32_t attributeId)
{
    const ClusterNames * cluster = FindCluster(clusterId);
    return (cluster != NULL) ? FindName(cluster->attributes, cluster->attributeCount, attributeId) : NULL;
}
