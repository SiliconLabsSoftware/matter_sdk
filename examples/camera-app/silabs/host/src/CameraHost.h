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

/**
 * Handles the camera commands and attribute changes forwarded by the SoC, and answers it with the
 * Interaction Model status of the operation (the SoC returns it to the Matter client).
 */

#pragma once

#include "CameraIds.h"
#include "MmicLink.h"
#include "Tlv.h"

#include <map>
#include <vector>

class CameraDevice;
class WebRtcManager;

class CameraHost
{
public:
    CameraHost(MmicLink & link, CameraDevice & camera, WebRtcManager & webrtc);

    void OnCommand(const MmicLink::ClusterCommand & command);
    void OnAttributeChange(const MmicLink::AttributeChange & change);

private:
    camera::Status HandleAvStreamManagement(uint32_t commandId, const std::vector<tlv::Element> & params);
    camera::Status HandleWebRtcProvider(uint16_t endpointId, uint32_t commandId, const std::vector<tlv::Element> & params);
    camera::Status HandleAvSettings(uint32_t commandId, const std::vector<tlv::Element> & params);

    camera::Status VideoStreamAllocate(const tlv::Element & stream);
    camera::Status AudioStreamAllocate(const tlv::Element & stream);
    camera::Status SnapshotStreamAllocate(const tlv::Element & stream);

    MmicLink & mLink;
    CameraDevice & mCamera;
    WebRtcManager & mWebRtc;

    // Snapshot streams are only tracked: capture is not supported yet.
    std::map<uint16_t, uint8_t> mSnapshotStreams;
};
