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

#include "CameraDevice.h"

#include "Log.h"

#include <gst/app/gstappsink.h>
#include <gst/video/video.h>

#include <algorithm>
#include <sstream>
#include <sys/stat.h>

using camera::Status;

namespace {

// GStreamer video-direction values (GstVideoOrientationMethod).
constexpr int kDirectionIdentity   = 0;
constexpr int kDirectionRotate180  = 2;
constexpr int kDirectionHorizontal = 4;
constexpr int kDirectionVertical   = 5;

constexpr uint8_t kMaxVolumeLevel = 254;

struct SinkContext
{
    CameraDevice::FrameCallback * callback;
    uint16_t streamId;
};

GstBusSyncReply OnBusMessage(GstBus *, GstMessage * message, gpointer name)
{
    GError * error = nullptr;
    gchar * debug  = nullptr;
    switch (GST_MESSAGE_TYPE(message))
    {
    case GST_MESSAGE_ERROR:
        gst_message_parse_error(message, &error, &debug);
        LogError("%s pipeline: %s (%s)", static_cast<const char *>(name), error ? error->message : "unknown error",
                 debug ? debug : "");
        break;
    case GST_MESSAGE_WARNING:
        gst_message_parse_warning(message, &error, &debug);
        LogInfo("%s pipeline warning: %s", static_cast<const char *>(name), error ? error->message : "unknown warning");
        break;
    default:
        break;
    }
    g_clear_error(&error);
    g_free(debug);
    // Nobody pops the bus: drop every message.
    return GST_BUS_DROP;
}

GstElement * LaunchPipeline(const std::string & description, const char * name)
{
    GError * error        = nullptr;
    GstElement * pipeline = gst_parse_launch(description.c_str(), &error);
    if (error != nullptr)
    {
        LogError("Cannot create %s pipeline: %s", name, error->message);
        g_clear_error(&error);
        if (pipeline != nullptr)
        {
            gst_object_unref(pipeline);
        }
        return nullptr;
    }

    GstBus * bus = gst_element_get_bus(pipeline);
    gst_bus_set_sync_handler(bus, OnBusMessage, const_cast<char *>(name), nullptr);
    gst_object_unref(bus);
    return pipeline;
}

void StopPipeline(GstElement * pipeline)
{
    if (pipeline != nullptr)
    {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    }
}

void UnrefElement(GstElement *& element)
{
    if (element != nullptr)
    {
        gst_object_unref(element);
        element = nullptr;
    }
}

GstFlowReturn OnNewSample(GstAppSink * sink, gpointer userData)
{
    auto * context     = static_cast<SinkContext *>(userData);
    GstSample * sample = gst_app_sink_pull_sample(sink);
    if (sample == nullptr)
    {
        return GST_FLOW_OK;
    }

    GstBuffer * buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    // Codec headers (e.g. Opus) are not media frames.
    if (buffer != nullptr && !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_HEADER) && gst_buffer_map(buffer, &map, GST_MAP_READ))
    {
        GstClockTime pts = GST_BUFFER_PTS(buffer);
        if (!GST_CLOCK_TIME_IS_VALID(pts))
        {
            pts = 0;
        }
        // Clock time: shared by the audio and video pipelines.
        const GstClockTime clockTime = gst_element_get_base_time(GST_ELEMENT(sink)) + pts;
        if (*context->callback)
        {
            (*context->callback)(context->streamId, map.data, map.size, std::chrono::microseconds(clockTime / 1000));
        }
        gst_buffer_unmap(buffer, &map);
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

void DestroySinkContext(gpointer userData)
{
    delete static_cast<SinkContext *>(userData);
}

void AttachSink(GstElement * sink, CameraDevice::FrameCallback * callback, uint16_t streamId)
{
    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample          = OnNewSample;
    gst_app_sink_set_callbacks(GST_APP_SINK(sink), &callbacks, new SinkContext{ callback, streamId }, DestroySinkContext);
}

bool HasElement(const char * factory)
{
    GstElementFactory * found = gst_element_factory_find(factory);
    if (found == nullptr)
    {
        LogError("GStreamer element '%s' not found", factory);
        return false;
    }
    gst_object_unref(found);
    return true;
}

// Looks for an audio capture device (monitors of audio outputs excluded).
bool DetectMicrophone()
{
    GstDeviceMonitor * monitor = gst_device_monitor_new();
    gst_device_monitor_add_filter(monitor, "Audio/Source", nullptr);
    if (!gst_device_monitor_start(monitor))
    {
        gst_object_unref(monitor);
        return false;
    }

    bool found      = false;
    GList * devices = gst_device_monitor_get_devices(monitor);
    for (GList * item = devices; item != nullptr; item = item->next)
    {
        GstDevice * device      = GST_DEVICE(item->data);
        GstStructure * props    = gst_device_get_properties(device);
        const gchar * deviceClass = (props != nullptr) ? gst_structure_get_string(props, "device.class") : nullptr;
        if (g_strcmp0(deviceClass, "monitor") != 0)
        {
            gchar * name = gst_device_get_display_name(device);
            LogInfo("Microphone found: %s", name);
            g_free(name);
            found = true;
        }
        if (props != nullptr)
        {
            gst_structure_free(props);
        }
    }
    g_list_free_full(devices, gst_object_unref);
    gst_device_monitor_stop(monitor);
    gst_object_unref(monitor);
    return found;
}

} // namespace

CameraDevice::~CameraDevice()
{
    Shutdown();
}

bool CameraDevice::Init(const std::string & devicePath)
{
    struct stat info;
    if (stat(devicePath.c_str(), &info) != 0)
    {
        LogError("Camera device %s not found", devicePath.c_str());
        return false;
    }
    mDevicePath = devicePath;

    gst_init(nullptr, nullptr);
    if (!HasElement("v4l2src") || !HasElement("x264enc") || !HasElement("videocrop") || !HasElement("videoflip"))
    {
        LogError("Install the GStreamer good and ugly plugins");
        return false;
    }

    mHasMicrophone = DetectMicrophone() && HasElement("opusenc");
    if (!mHasMicrophone)
    {
        LogInfo("No microphone: audio streams will be rejected");
    }

    mWorkerRunning = true;
    mWorker        = std::thread(&CameraDevice::WorkerLoop, this);
    LogInfo("Camera %s ready", devicePath.c_str());
    return true;
}

void CameraDevice::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(mTaskMutex);
        if (!mWorkerRunning)
        {
            return;
        }
        mWorkerRunning = false;
    }
    mTaskCondition.notify_all();
    if (mWorker.joinable())
    {
        mWorker.join();
    }

    GstElement * video = nullptr;
    GstElement * audio = nullptr;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        ReleaseVideoElementsLocked();
        ReleaseAudioElementsLocked();
        std::swap(video, mVideoPipeline);
        std::swap(audio, mAudioPipeline);
    }
    StopPipeline(video);
    StopPipeline(audio);
}

void CameraDevice::SetFrameCallbacks(FrameCallback videoCallback, FrameCallback audioCallback)
{
    mVideoCallback = std::move(videoCallback);
    mAudioCallback = std::move(audioCallback);
}

void CameraDevice::Post(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(mTaskMutex);
        mTasks.push_back(std::move(task));
    }
    mTaskCondition.notify_one();
}

void CameraDevice::WorkerLoop()
{
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mTaskMutex);
            mTaskCondition.wait(lock, [this] { return !mWorkerRunning || !mTasks.empty(); });
            if (!mWorkerRunning)
            {
                return;
            }
            task = std::move(mTasks.front());
            mTasks.pop_front();
        }
        task();
    }
}

Status CameraDevice::StartVideoStream(const VideoStreamConfig & config)
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mVideoStream.has_value() && mVideoStream->id != config.id)
        {
            LogError("Video stream %u rejected: stream %u already uses the encoder", config.id, mVideoStream->id);
            return Status::kResourceExhausted;
        }
        if (mVideoStream.has_value() && mVideoStream->width == config.width && mVideoStream->height == config.height &&
            mVideoStream->frameRate == config.frameRate && mVideoStream->bitRateKbps == config.bitRateKbps &&
            mVideoStream->keyFrameFrames == config.keyFrameFrames)
        {
            return Status::kSuccess;
        }
        if (!mVideoStream.has_value() || mVideoStream->id != config.id)
        {
            mStreamViewport.reset();
        }
        mVideoStream = config;
    }
    LogInfo("Video stream %u: H.264 %ux%u @ %u fps, %u kbps", config.id, config.width, config.height, config.frameRate,
            config.bitRateKbps);
    Post([this] { RebuildVideoPipeline(); });
    return Status::kSuccess;
}

Status CameraDevice::StopVideoStream(uint16_t id)
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mVideoStream.has_value() || mVideoStream->id != id)
        {
            return Status::kNotFound;
        }
        mVideoStream.reset();
        mStreamViewport.reset();
    }
    LogInfo("Video stream %u stopped", id);
    Post([this] { RebuildVideoPipeline(); });
    return Status::kSuccess;
}

bool CameraDevice::HasVideoStream(uint16_t id) const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mVideoStream.has_value() && mVideoStream->id == id;
}

Status CameraDevice::StartAudioStream(const AudioStreamConfig & config)
{
    if (!mHasMicrophone)
    {
        LogError("Audio stream %u rejected: no microphone", config.id);
        return Status::kResourceExhausted;
    }
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mAudioStream.has_value() && mAudioStream->id != config.id)
        {
            LogError("Audio stream %u rejected: stream %u already uses the microphone", config.id, mAudioStream->id);
            return Status::kResourceExhausted;
        }
        mAudioStream = config;
    }
    LogInfo("Audio stream %u: Opus %u channel(s) @ %u Hz, %u bps", config.id, config.channels, config.sampleRate, config.bitRate);
    Post([this] { RebuildAudioPipeline(); });
    return Status::kSuccess;
}

Status CameraDevice::StopAudioStream(uint16_t id)
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mAudioStream.has_value() || mAudioStream->id != id)
        {
            return Status::kNotFound;
        }
        mAudioStream.reset();
    }
    LogInfo("Audio stream %u stopped", id);
    Post([this] { RebuildAudioPipeline(); });
    return Status::kSuccess;
}

bool CameraDevice::HasAudioStream(uint16_t id) const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mAudioStream.has_value() && mAudioStream->id == id;
}

void CameraDevice::RequestKeyFrame()
{
    GstElement * sink = nullptr;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mVideoSink != nullptr)
        {
            sink = GST_ELEMENT(gst_object_ref(mVideoSink));
        }
    }
    if (sink != nullptr)
    {
        // Upstream event, handled by the encoder.
        gst_element_send_event(sink, gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE, TRUE, 0));
        gst_object_unref(sink);
    }
}

void CameraDevice::SetDefaultViewport(const Viewport & viewport)
{
    std::lock_guard<std::mutex> lock(mMutex);
    // As on the SoC, a new default viewport replaces the per-stream viewports.
    mDefaultViewport = viewport;
    mStreamViewport.reset();
    ApplyViewportLocked();
}

Status CameraDevice::SetStreamViewport(uint16_t videoStreamId, const Viewport & viewport)
{
    if (viewport.x2 <= viewport.x1 || viewport.y2 <= viewport.y1 || viewport.x2 > kSensorWidth || viewport.y2 > kSensorHeight)
    {
        return Status::kConstraintError;
    }
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mVideoStream.has_value() || mVideoStream->id != videoStreamId)
    {
        return Status::kNotFound;
    }
    mStreamViewport = viewport;
    ApplyViewportLocked();
    return Status::kSuccess;
}

Status CameraDevice::SetImageRotation(uint16_t degrees)
{
    // 90 and 270 degrees would change the stream dimensions.
    if (degrees != 0 && degrees != 180)
    {
        return Status::kConstraintError;
    }
    std::lock_guard<std::mutex> lock(mMutex);
    mRotation = degrees;
    ApplyOrientationLocked();
    return Status::kSuccess;
}

Status CameraDevice::SetImageFlipHorizontal(bool flip)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mFlipHorizontal = flip;
    ApplyOrientationLocked();
    return Status::kSuccess;
}

Status CameraDevice::SetImageFlipVertical(bool flip)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mFlipVertical = flip;
    ApplyOrientationLocked();
    return Status::kSuccess;
}

Status CameraDevice::SetMicrophoneMuted(bool muted)
{
    if (!mHasMicrophone)
    {
        return Status::kUnsupportedAttribute;
    }
    std::lock_guard<std::mutex> lock(mMutex);
    mMicrophoneMuted = muted;
    ApplyVolumeLocked();
    return Status::kSuccess;
}

Status CameraDevice::SetMicrophoneVolume(uint8_t level)
{
    if (!mHasMicrophone)
    {
        return Status::kUnsupportedAttribute;
    }
    std::lock_guard<std::mutex> lock(mMutex);
    mMicrophoneVolume = std::min<double>(level, kMaxVolumeLevel) / kMaxVolumeLevel;
    ApplyVolumeLocked();
    return Status::kSuccess;
}

void CameraDevice::SetPrivacyMode(bool enabled)
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mPrivacyMode == enabled)
        {
            return;
        }
        mPrivacyMode = enabled;
    }
    LogInfo("Privacy mode %s", enabled ? "on: capture stopped" : "off");
    Post([this] {
        RebuildVideoPipeline();
        RebuildAudioPipeline();
    });
}

void CameraDevice::ApplyViewportLocked()
{
    if (mCrop == nullptr || !mVideoStream.has_value())
    {
        return;
    }
    // The capture runs at the stream resolution: scale the sensor coordinates.
    const Viewport & viewport = mStreamViewport.value_or(mDefaultViewport);
    const int width           = mVideoStream->width;
    const int height          = mVideoStream->height;
    const int left            = viewport.x1 * width / kSensorWidth;
    const int right           = width - std::min<int>(width, viewport.x2 * width / kSensorWidth);
    const int top             = viewport.y1 * height / kSensorHeight;
    const int bottom          = height - std::min<int>(height, viewport.y2 * height / kSensorHeight);
    g_object_set(mCrop, "left", left, "right", right, "top", top, "bottom", bottom, nullptr);
}

void CameraDevice::ApplyOrientationLocked()
{
    if (mFlip == nullptr)
    {
        return;
    }
    // Combination of a 0/180 degree rotation and the flips.
    const bool horizontal = mFlipHorizontal != (mRotation == 180);
    const bool vertical   = mFlipVertical != (mRotation == 180);
    int direction         = kDirectionIdentity;
    if (horizontal && vertical)
    {
        direction = kDirectionRotate180;
    }
    else if (horizontal)
    {
        direction = kDirectionHorizontal;
    }
    else if (vertical)
    {
        direction = kDirectionVertical;
    }
    g_object_set(mFlip, "video-direction", direction, nullptr);
}

void CameraDevice::ApplyVolumeLocked()
{
    if (mVolume != nullptr)
    {
        g_object_set(mVolume, "mute", mMicrophoneMuted ? TRUE : FALSE, "volume", mMicrophoneVolume, nullptr);
    }
}

void CameraDevice::ReleaseVideoElementsLocked()
{
    UnrefElement(mVideoSink);
    UnrefElement(mCrop);
    UnrefElement(mFlip);
}

void CameraDevice::ReleaseAudioElementsLocked()
{
    UnrefElement(mVolume);
}

void CameraDevice::RebuildVideoPipeline()
{
    std::optional<VideoStreamConfig> stream;
    GstElement * previous = nullptr;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        stream = mPrivacyMode ? std::nullopt : mVideoStream;
        ReleaseVideoElementsLocked();
        std::swap(previous, mVideoPipeline);
    }
    StopPipeline(previous);
    if (!stream.has_value())
    {
        return;
    }

    // Capture at the stream resolution. The crop applies the viewport, the scaler restores the
    // stream resolution (with borders when the viewport aspect ratio differs).
    std::ostringstream description;
    description << "v4l2src device=" << mDevicePath << " do-timestamp=true"
                << " ! video/x-raw,width=" << stream->width << ",height=" << stream->height
                << ",framerate=" << stream->frameRate << "/1"
                << " ! videoconvert ! videoflip name=flip ! videocrop name=crop ! videoscale"
                << " ! video/x-raw,width=" << stream->width << ",height=" << stream->height
                << ",format=I420,pixel-aspect-ratio=1/1"
                << " ! x264enc tune=zerolatency speed-preset=ultrafast insert-vui=true byte-stream=true"
                << " bitrate=" << stream->bitRateKbps << " key-int-max=" << stream->keyFrameFrames
                << " ! video/x-h264,profile=constrained-baseline,stream-format=byte-stream,alignment=au"
                << " ! appsink name=video_sink sync=false max-buffers=8 drop=true";

    GstElement * pipeline = LaunchPipeline(description.str(), "video");
    if (pipeline == nullptr)
    {
        return;
    }
    GstElement * sink = gst_bin_get_by_name(GST_BIN(pipeline), "video_sink");
    AttachSink(sink, &mVideoCallback, stream->id);

    {
        std::lock_guard<std::mutex> lock(mMutex);
        mVideoSink = sink;
        mCrop      = gst_bin_get_by_name(GST_BIN(pipeline), "crop");
        mFlip      = gst_bin_get_by_name(GST_BIN(pipeline), "flip");
        ApplyViewportLocked();
        ApplyOrientationLocked();
        mVideoPipeline = pipeline;
    }

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    {
        LogError("Cannot start the video pipeline on %s", mDevicePath.c_str());
    }
}

void CameraDevice::RebuildAudioPipeline()
{
    std::optional<AudioStreamConfig> stream;
    GstElement * previous = nullptr;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        stream = mPrivacyMode ? std::nullopt : mAudioStream;
        ReleaseAudioElementsLocked();
        std::swap(previous, mAudioPipeline);
    }
    StopPipeline(previous);
    if (!stream.has_value())
    {
        return;
    }

    std::ostringstream description;
    description << "autoaudiosrc ! audioconvert ! audioresample"
                << " ! audio/x-raw,rate=" << stream->sampleRate << ",channels=" << static_cast<unsigned>(stream->channels)
                << " ! volume name=volume ! opusenc bitrate=" << stream->bitRate << " frame-size=20"
                << " ! appsink name=audio_sink sync=false max-buffers=16 drop=true";

    GstElement * pipeline = LaunchPipeline(description.str(), "audio");
    if (pipeline == nullptr)
    {
        return;
    }
    GstElement * sink = gst_bin_get_by_name(GST_BIN(pipeline), "audio_sink");
    AttachSink(sink, &mAudioCallback, stream->id);
    gst_object_unref(sink);

    {
        std::lock_guard<std::mutex> lock(mMutex);
        mVolume = gst_bin_get_by_name(GST_BIN(pipeline), "volume");
        ApplyVolumeLocked();
        mAudioPipeline = pipeline;
    }

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    {
        LogError("Cannot start the audio pipeline");
    }
}
