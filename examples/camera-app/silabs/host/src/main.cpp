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
 * camera_host: camera part of the Silabs camera-app. The Matter data model and commissioning run
 * on the SoC, which forwards the camera commands over MMIC (UART). The host drives the camera and
 * streams the media over its own network interface.
 *
 * Usage: camera_host <mmic tty> <streaming network interface> <camera device>
 */

#include "CameraDevice.h"
#include "CameraHost.h"
#include "Log.h"
#include "MmicLink.h"
#include "WebRtcManager.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string>
#include <thread>

namespace {

std::atomic<bool> sStop{ false };

void HandleSignal(int)
{
    sStop = true;
}

void PrintUsage(const char * program)
{
    std::fprintf(stderr,
                 "Usage: %s <mmic tty> <streaming network interface> <camera device>\n"
                 "  mmic tty                     UART connected to the camera SoC, e.g. /dev/ttyACM0\n"
                 "  streaming network interface  interface used for the WebRTC media, e.g. wlan0\n"
                 "  camera device                V4L2 camera, e.g. video0 or /dev/video0\n"
                 "On a Raspberry Pi camera, run it through libcamerify:\n"
                 "  libcamerify %s /dev/ttyACM0 wlan0 video0\n",
                 program, program);
}

// Address of the interface used to bind the WebRTC sockets. IPv4 first, then global IPv6.
std::string InterfaceAddress(const std::string & interface)
{
    ifaddrs * addresses = nullptr;
    if (getifaddrs(&addresses) != 0)
    {
        return "";
    }

    std::string ipv4;
    std::string ipv6;
    for (ifaddrs * entry = addresses; entry != nullptr; entry = entry->ifa_next)
    {
        if (entry->ifa_addr == nullptr || interface != entry->ifa_name || (entry->ifa_flags & IFF_UP) == 0)
        {
            continue;
        }
        char text[INET6_ADDRSTRLEN] = {};
        if (entry->ifa_addr->sa_family == AF_INET && ipv4.empty())
        {
            inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in *>(entry->ifa_addr)->sin_addr, text, sizeof(text));
            ipv4 = text;
        }
        else if (entry->ifa_addr->sa_family == AF_INET6 && ipv6.empty())
        {
            const in6_addr & address = reinterpret_cast<sockaddr_in6 *>(entry->ifa_addr)->sin6_addr;
            if (!IN6_IS_ADDR_LINKLOCAL(&address))
            {
                inet_ntop(AF_INET6, &address, text, sizeof(text));
                ipv6 = text;
            }
        }
    }
    freeifaddrs(addresses);
    return ipv4.empty() ? ipv6 : ipv4;
}

} // namespace

int main(int argc, char ** argv)
{
    if (argc != 4)
    {
        PrintUsage(argv[0]);
        return 1;
    }

    const std::string tty       = argv[1];
    const std::string interface = argv[2];
    std::string device          = argv[3];
    if (device.find('/') == std::string::npos)
    {
        device = "/dev/" + device;
    }

    // Line buffered logs, also when redirected.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    const std::string bindAddress = InterfaceAddress(interface);
    if (bindAddress.empty())
    {
        LogError("No IP address on network interface %s", interface.c_str());
        return 1;
    }
    LogInfo("Streaming on %s (%s)", interface.c_str(), bindAddress.c_str());

    CameraDevice camera;
    if (!camera.Init(device))
    {
        return 1;
    }

    MmicLink link;
    if (!link.Open(tty))
    {
        return 1;
    }

    WebRtcManager webrtc(
        camera,
        [&link](uint16_t endpointId, uint32_t commandId, const std::vector<uint8_t> & params) {
            link.SendClusterCommand(endpointId, camera::ClusterId::kWebRTCTransportRequestor, commandId, params);
        },
        bindAddress);

    camera.SetFrameCallbacks(
        [&webrtc](uint16_t streamId, const uint8_t * data, size_t size, std::chrono::microseconds timestamp) {
            webrtc.OnVideoFrame(streamId, data, size, timestamp);
        },
        [&webrtc](uint16_t streamId, const uint8_t * data, size_t size, std::chrono::microseconds timestamp) {
            webrtc.OnAudioFrame(streamId, data, size, timestamp);
        });

    CameraHost host(link, camera, webrtc);

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    link.Start([&host](const MmicLink::ClusterCommand & command) { host.OnCommand(command); },
               [&host](const MmicLink::AttributeChange & change) { host.OnAttributeChange(change); });
    LogInfo("Waiting for commands from the SoC, ctrl+c to quit");

    while (!sStop)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    LogInfo("Stopping");
    // End the sessions while the link is up, so that the SoC notifies the requestors.
    webrtc.EndAllSessions(camera::WebRTC::EndReason::kUnknownReason);
    link.Stop();
    camera.Shutdown();
    return 0;
}
