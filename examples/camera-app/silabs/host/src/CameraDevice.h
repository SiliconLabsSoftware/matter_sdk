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
 * Camera hardware: V4L2 capture (through libcamerify on a Raspberry Pi camera), H.264 and Opus
 * encoding with GStreamer, image controls.
 *
 * Requests are validated synchronously against the camera capabilities, the GStreamer pipelines
 * are then (re)built on a worker thread so that callers are never blocked by a pipeline start.
 */

#pragma once

#include "CameraIds.h"

#include <gst/gst.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

struct VideoStreamConfig
{
    uint16_t id             = 0;
    uint16_t width          = 0;
    uint16_t height         = 0;
    uint16_t frameRate      = 0;
    uint32_t bitRateKbps    = 0;
    uint32_t keyFrameFrames = 0; // Maximum distance between two key frames
};

struct AudioStreamConfig
{
    uint16_t id         = 0;
    uint8_t channels    = 1;
    uint32_t sampleRate = 48000;
    uint32_t bitRate    = 32000;
};

// Coordinates in the sensor space (CameraDevice::kSensorWidth x kSensorHeight).
struct Viewport
{
    uint16_t x1 = 0;
    uint16_t y1 = 0;
    uint16_t x2 = 0;
    uint16_t y2 = 0;
};

class CameraDevice
{
public:
    // Sensor size advertised by the SoC: coordinate space of the viewports.
    static constexpr uint16_t kSensorWidth  = 1920;
    static constexpr uint16_t kSensorHeight = 1080;
    // Largest H.264 stream encoded in software on a Raspberry Pi 4.
    static constexpr uint16_t kMaxEncodeWidth  = 1280;
    static constexpr uint16_t kMaxEncodeHeight = 720;
    static constexpr uint16_t kMaxFrameRate    = 30;

    // Encoded frame (H.264 access unit or Opus packet) and its capture time.
    using FrameCallback =
        std::function<void(uint16_t streamId, const uint8_t * data, size_t size, std::chrono::microseconds timestamp)>;

    ~CameraDevice();

    bool Init(const std::string & devicePath);
    void Shutdown();
    void SetFrameCallbacks(FrameCallback videoCallback, FrameCallback audioCallback);

    bool HasMicrophone() const { return mHasMicrophone; }

    // A single video stream (one encoder) and a single audio stream at a time.
    camera::Status StartVideoStream(const VideoStreamConfig & config);
    camera::Status StopVideoStream(uint16_t id);
    bool HasVideoStream(uint16_t id) const;
    camera::Status StartAudioStream(const AudioStreamConfig & config);
    camera::Status StopAudioStream(uint16_t id);
    bool HasAudioStream(uint16_t id) const;

    // Asks the encoder for a key frame, e.g. when a WebRTC peer connects.
    void RequestKeyFrame();

    // Default viewport (Viewport attribute) and per-stream viewport (digital PTZ).
    void SetDefaultViewport(const Viewport & viewport);
    camera::Status SetStreamViewport(uint16_t videoStreamId, const Viewport & viewport);

    camera::Status SetImageRotation(uint16_t degrees);
    camera::Status SetImageFlipHorizontal(bool flip);
    camera::Status SetImageFlipVertical(bool flip);
    camera::Status SetMicrophoneMuted(bool muted);
    camera::Status SetMicrophoneVolume(uint8_t level);

    // Stops capture while the hard privacy mode is on.
    void SetPrivacyMode(bool enabled);

private:
    void Post(std::function<void()> task);
    void WorkerLoop();
    void RebuildVideoPipeline();
    void RebuildAudioPipeline();
    void ApplyViewportLocked();
    void ApplyOrientationLocked();
    void ApplyVolumeLocked();
    void ReleaseVideoElementsLocked();
    void ReleaseAudioElementsLocked();

    std::string mDevicePath;
    bool mHasMicrophone = false;
    FrameCallback mVideoCallback;
    FrameCallback mAudioCallback;

    // Desired state, protected by mMutex.
    mutable std::mutex mMutex;
    std::optional<VideoStreamConfig> mVideoStream;
    std::optional<AudioStreamConfig> mAudioStream;
    Viewport mDefaultViewport{ 0, 0, kSensorWidth, kSensorHeight };
    std::optional<Viewport> mStreamViewport;
    uint16_t mRotation       = 0;
    bool mFlipHorizontal     = false;
    bool mFlipVertical       = false;
    bool mMicrophoneMuted    = false;
    double mMicrophoneVolume = 1.0;
    bool mPrivacyMode        = false;

    // Running pipelines and the elements changed at runtime, protected by mMutex.
    GstElement * mVideoPipeline = nullptr;
    GstElement * mVideoSink     = nullptr;
    GstElement * mCrop          = nullptr;
    GstElement * mFlip          = nullptr;
    GstElement * mAudioPipeline = nullptr;
    GstElement * mVolume        = nullptr;

    std::mutex mTaskMutex;
    std::condition_variable mTaskCondition;
    std::deque<std::function<void()>> mTasks;
    bool mWorkerRunning = false;
    std::thread mWorker;
};
