/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2019 Nest Labs, Inc.
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
/* this file behaves like a config.h, comes first */
#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/ConnectivityManager.h>
#include <platform/internal/BLEManager.h>
#include <platform/silabs/NetworkCommissioningWiFiDriver.h>

#include <lwip/dns.h>
#include <lwip/ip_addr.h>
#include <lwip/nd6.h>
#include <lwip/netif.h>

#include <platform/internal/GenericConnectivityManagerImpl_UDP.ipp>

#if INET_CONFIG_ENABLE_TCP_ENDPOINT
#include <platform/internal/GenericConnectivityManagerImpl_TCP.ipp>
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
#include <platform/internal/GenericConnectivityManagerImpl_BLE.ipp>
#endif

#include "CHIPDevicePlatformConfig.h"
#include <platform/silabs/wifi/WifiInterface.h>

using namespace ::chip;
using namespace ::chip::Inet;
using namespace ::chip::System;
using namespace ::chip::DeviceLayer::Internal;
using namespace ::chip::DeviceLayer::Silabs;

namespace chip {
namespace DeviceLayer {

ConnectivityManagerImpl ConnectivityManagerImpl::sInstance;

CHIP_ERROR ConnectivityManagerImpl::_Init()
{
    CHIP_ERROR err;
    mWiFiStationMode              = kWiFiStationMode_Disabled;
    mWiFiStationState             = kWiFiStationState_NotConnected;
    mLastStationConnectFailTime   = System::Clock::kZero;
    mWiFiStationReconnectInterval = System::Clock::Milliseconds32(CHIP_DEVICE_CONFIG_WIFI_STATION_RECONNECT_INTERVAL);
    mWiFiStationReconnectCount    = 1;
    mFlags.ClearAll();

    // TODO Initialize the Chip Addressing and Routing Module.

    // Ensure that station mode is enabled.
    err = SetWiFiStationMode(kWiFiStationMode_Enabled);
    VerifyOrReturnError(err == CHIP_NO_ERROR, err);

    // Queue work items to bootstrap the AP and station state machines once the Chip event loop is running.
    err = DeviceLayer::SystemLayer().ScheduleWork(DriveStationState, NULL);
    VerifyOrReturnError(err == CHIP_NO_ERROR, err);

    return CHIP_NO_ERROR;
}

void ConnectivityManagerImpl::_OnPlatformEvent(const ChipDeviceEvent * event)
{
    // Forward the event to the generic base classes as needed.
    // Handle Wfx wifi events...
    if (event->Type == DeviceEventType::kPlatformSLEvent)
    {

        switch (event->Platform.event.WFXSystemEvent.data.genericMsgEvent.header.id)
        {
        case to_underlying(WifiInterface::WifiEvent::kStartUp):
            ChipLogProgress(DeviceLayer, "WIFI_EVENT_STA_STARTED");
            DriveStationState();
            break;

        case to_underlying(WifiInterface::WifiEvent::kConnect):
            ChipLogProgress(DeviceLayer, "WIFI_EVENT_STA_CONNECTED");
            ChangeWiFiStationState(kWiFiStationState_Connected);
            break;

        case to_underlying(WifiInterface::WifiEvent::kDisconnect):
            ChipLogProgress(DeviceLayer, "WIFI_EVENT_STA_DISCONNECTED");
            switch (event->Platform.event.WFXSystemEvent.data.disconnectEvent.body.reason)
            {
            // User initiated disconnection
            case to_underlying(WifiInterface::WifiDisconnectionReasons::kApplication):
                ChangeWiFiStationState(kWiFiStationState_NotConnected);
                break;
            default:
                ChangeWiFiStationState(kWiFiStationState_Connecting_Failed);
                break;
            }
            break;

        case to_underlying(WifiInterface::WifiEvent::kGotIPv4):
        case to_underlying(WifiInterface::WifiEvent::kGotIPv6):
        case to_underlying(WifiInterface::WifiEvent::kLostIP):
            ChipLogProgress(DeviceLayer, "WIFI_EVENT_STA_IP_CHANGE");
            UpdateInternetConnectivityState();
            break;
        default:
            break;
        }
    }
}

ConnectivityManager::WiFiStationMode ConnectivityManagerImpl::_GetWiFiStationMode(void)
{
    VerifyOrReturnValue(mWiFiStationMode != kWiFiStationMode_ApplicationControlled, mWiFiStationMode);
    return WifiInterface::GetInstance().IsStationModeEnabled() ? kWiFiStationMode_Enabled : kWiFiStationMode_Disabled;
}

bool ConnectivityManagerImpl::_IsWiFiStationProvisioned(void)
{
    return WifiInterface::GetInstance().IsWifiProvisioned();
}

bool ConnectivityManagerImpl::_IsWiFiStationEnabled(void)
{
    return WifiInterface::GetInstance().IsStationModeEnabled();
}

CHIP_ERROR ConnectivityManagerImpl::_SetWiFiStationMode(ConnectivityManager::WiFiStationMode val)
{
    // If the new WiFi station mode is the same as the current WiFi station mode, return success.
    VerifyOrReturnError(val != mWiFiStationMode, CHIP_NO_ERROR,
                        ChipLogDetail(DeviceLayer, "WiFi station mode is already %s", WiFiStationModeToStr(val)));

    ChipLogProgress(DeviceLayer, "WiFi station mode change: %s -> %s", WiFiStationModeToStr(mWiFiStationMode),
                    WiFiStationModeToStr(val));

    mWiFiStationMode = val;
    VerifyOrReturnError(WifiInterface::GetInstance().EnableStationMode() == CHIP_NO_ERROR, CHIP_ERROR_INTERNAL);
    TEMPORARY_RETURN_IGNORED DeviceLayer::SystemLayer().ScheduleWork(DriveStationState, NULL);
    return CHIP_NO_ERROR;
}

CHIP_ERROR ConnectivityManagerImpl::_SetWiFiStationReconnectInterval(System::Clock::Timeout timeoutMs)
{
    mWiFiStationReconnectInterval = timeoutMs;
    return CHIP_NO_ERROR;
}

void ConnectivityManagerImpl::_ClearWiFiStationProvision(void)
{
    // If the WiFi station mode is application controlled, do not clear the WiFi credentials.
    VerifyOrReturn(mWiFiStationMode != kWiFiStationMode_ApplicationControlled,
                   ChipLogError(DeviceLayer, "kWiFiStationMode_ApplicationControlled enabled"));

    WifiInterface::GetInstance().ClearWifiCredentials();
    TEMPORARY_RETURN_IGNORED DeviceLayer::SystemLayer().ScheduleWork(DriveStationState, NULL);
}

CHIP_ERROR ConnectivityManagerImpl::_GetAndLogWifiStatsCounters(void)
{
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

void ConnectivityManagerImpl::_OnWiFiScanDone()
{
    // CHIP_ERROR_NOT_IMPLEMENTED
}

void ConnectivityManagerImpl::_OnWiFiStationProvisionChange()
{
    // Schedule a call to the DriveStationState method to adjust the station state as needed.
    ChipLogProgress(DeviceLayer, "_ON WIFI PROVISION CHANGE");
    TEMPORARY_RETURN_IGNORED DeviceLayer::SystemLayer().ScheduleWork(DriveStationState, NULL);
}

#if CHIP_CONFIG_ENABLE_ICD_SERVER
CHIP_ERROR ConnectivityManagerImpl::_SetPollingInterval(System::Clock::Milliseconds32 pollingInterval)
{
    // TODO: The polling interval feature is not implemented on this platform. Return success to prevent spurious error logs from
    // ICDManager. Revisit this once we complete the ICD integration
    (void) pollingInterval;
    return CHIP_NO_ERROR;
}
#endif /* CHIP_CONFIG_ENABLE_ICD_SERVER */

// == == == == == == == == == == ConnectivityManager Private Methods == == == == == == == == == ==

void ConnectivityManagerImpl::DriveStationState()
{
    // Refresh the current state and store it in `mWiFiStationMode`
    WiFiStationMode stationMode = GetWiFiStationMode();

    // if the station mode is application controlled or disabled, return
    VerifyOrReturn(stationMode != kWiFiStationMode_ApplicationControlled,
                   ChipLogError(DeviceLayer, "WiFi station mode is application controlled"));
    VerifyOrReturn(stationMode != kWiFiStationMode_Disabled, ChipLogError(DeviceLayer, "WiFi station mode is disabled"));

    CHIP_ERROR err = CHIP_NO_ERROR;

    // If the station interface is NOT under application control...
    // Ensure that the WiFi task is started.
    // returns CHIP_NO_ERROR if the task is started successfully, also when the task is already started
    err = WifiInterface::GetInstance().StartWifiTask();
    VerifyOrReturn(err == CHIP_NO_ERROR, ChipLogError(DeviceLayer, "StartWifiTask failed: %" CHIP_ERROR_FORMAT, err.Format()));

    err = SetWiFiStationMode(kWiFiStationMode_Enabled);
    VerifyOrReturn(err == CHIP_NO_ERROR, ChipLogError(DeviceLayer, "SetStationMode failed: %" CHIP_ERROR_FORMAT, err.Format()));

    // if the station is not provisioned but connected, disconnect it
    if (!IsWiFiStationProvisioned() && IsWiFiStationConnected())
    {
        ChipLogDetail(DeviceLayer, "WiFi station is not provisioned and is connected, disconnecting");
        WifiInterface::GetInstance().TriggerDisconnection();
        ChangeWiFiStationState(kWiFiStationState_Disconnecting);
        // ChangeWiFiStationState() is called in the OnPlatformEvent() callback as per result of TriggerDisconnection()
        // next time DriveStationState() will be called, the station will be in the NotConnected state
        return;
    }

    // if the station is provisioned and not connected,
    // connect it to the access point using the credentials from the staging
    // network
    VerifyOrReturn(IsWiFiStationProvisioned(), ChipLogDetail(DeviceLayer, "WiFi station is not provisioned"));

    System::Clock::Timestamp now               = System::SystemClock().GetMonotonicTimestamp();
    System::Clock::Timestamp timeToNextConnect = System::Clock::kZero;

    ChipLogDetail(DeviceLayer, "WiFi station state: %s", WiFiStationStateToStr(mWiFiStationState));
    switch (mWiFiStationState)
    {
    case kWiFiStationState_NotConnected: {
        // connect the station to the access point using the credentials from the staging network
        err = WifiInterface::GetInstance().ConnectToAccessPoint(); // using the credentials from the staging network
        VerifyOrReturn(err == CHIP_NO_ERROR,
                       ChipLogError(DeviceLayer, "ConnectToAccessPoint failed: %" CHIP_ERROR_FORMAT, err.Format()));
        ChangeWiFiStationState(kWiFiStationState_Connecting);
        // ChangeWiFiStationState() is called in the OnPlatformEvent() callback as per result of ConnectWiFiNetwork()
    }
    break;
    case kWiFiStationState_Connecting: {
        // Connection attempt already in progress; wait for connect/disconnect platform events.
        return;
    }
    break;
    case kWiFiStationState_Connecting_Failed: {
        // if the station is connecting failed,
        // arrange another connection attempt at a suitable point in the future
        mLastStationConnectFailTime = now;

        // TODO: Revisit this logic
        // increase the reconnect interval by the previous interval, for telescoping effect and reduce the frequency of reconnect
        // attempts thus saving power
        // TODO: Guard this with commissioning mode flag
        // mWiFiStationReconnectCount++;
        // if ((mWiFiStationReconnectInterval * mWiFiStationReconnectCount) >
        // CHIP_DEVICE_CONFIG_WIFI_STATION_MAX_RECONNECT_INTERVAL)
        // {
        //     mWiFiStationReconnectCount--;
        // }

        // Reset the station state to NotConnected to start a new connection attempt
        mWiFiStationState = kWiFiStationState_NotConnected;
        timeToNextConnect = (mLastStationConnectFailTime + mWiFiStationReconnectInterval * mWiFiStationReconnectCount) - now;
        // DriveStationState() will be called again to start a new connection attempt
        ChipLogProgress(DeviceLayer, "Next WiFi station reconnect in %" PRIu32 " ms",
                        System::Clock::Milliseconds32(timeToNextConnect).count());
        ReturnOnFailure(DeviceLayer::SystemLayer().StartTimer(timeToNextConnect, DriveStationState, NULL));
    }
    break;
    default: {
        ChipLogDetail(DeviceLayer, "drive station state not handled: %s", WiFiStationStateToStr(mWiFiStationState));
    }
    break;
    }
    // Kick-off any pending network scan that might have been deferred due to the activity
    // of the WiFi station.
}

void ConnectivityManagerImpl::OnStationConnected()
{
    NetworkCommissioning::SlWiFiDriver * nwDriver = NetworkCommissioning::SlWiFiDriver::GetInstance();
    // Cannot use the driver if the instance is not initialized.
    VerifyOrDie(nwDriver != nullptr); // should never be null
    nwDriver->OnConnectWiFiNetwork();

    UpdateInternetConnectivityState();
    // Alert other components of the new state.
    ChipDeviceEvent event;
    event.Type                          = DeviceEventType::kWiFiConnectivityChange;
    event.WiFiConnectivityChange.Result = kConnectivity_Established;
    (void) PlatformMgr().PostEvent(&event);
}

void ConnectivityManagerImpl::OnStationDisconnected()
{
    // TODO: Invoke WARM to perform actions that occur when the WiFi station interface goes down.
    UpdateInternetConnectivityState();
    // Alert other components of the new state.
    ChipDeviceEvent event;
    event.Type                          = DeviceEventType::kWiFiConnectivityChange;
    event.WiFiConnectivityChange.Result = kConnectivity_Lost;
    (void) PlatformMgr().PostEvent(&event);
}

void ConnectivityManagerImpl::DriveStationState(::chip::System::Layer * aLayer, void * aAppState)
{
    sInstance.DriveStationState();
}

void ConnectivityManagerImpl::ChangeWiFiStationState(WiFiStationState newState)
{
    VerifyOrReturn(mWiFiStationState != newState,
                   ChipLogDetail(DeviceLayer, "WiFi station state is already %s", WiFiStationStateToStr(newState)));
    ChipLogProgress(DeviceLayer, "WiFi station state change: %s -> %s", WiFiStationStateToStr(mWiFiStationState),
                    WiFiStationStateToStr(newState));
    // Commit the state before notifying. OnStationConnected() calls
    // UpdateInternetConnectivityState(), which only reports IPv6 when already Connected.
    mWiFiStationState = newState;
    switch (newState)
    {
    case kWiFiStationState_Connecting_Succeeded:
        // if the station is connected or connecting succeeded,
        // reset the last connection failure time and reconnect interval
        mLastStationConnectFailTime = System::Clock::kZero;
        mWiFiStationReconnectCount  = 1;
        // intentionally fall through to the connected state
    case kWiFiStationState_Connected:
        OnStationConnected(); // alert other components of the new state
        break;

    case kWiFiStationState_NotConnected:
        // reset the last connection failure time and reconnect interval
        mLastStationConnectFailTime = System::Clock::kZero;
        mWiFiStationReconnectCount  = 1;
        // intentionally fall through to the failed state
    case kWiFiStationState_Connecting_Failed:
        OnStationDisconnected(); // alert other components of the new state
        break;

    default:
        ChipLogDetail(DeviceLayer, "WiFi station state not notifying: %s", WiFiStationStateToStr(newState));
        break;
    }
    TEMPORARY_RETURN_IGNORED DeviceLayer::SystemLayer().ScheduleWork(DriveStationState, NULL);

    // TODO: Remove this once the WiFi driver is updated to use the new state machine
    NetworkCommissioning::SlWiFiDriver * nwDriver = NetworkCommissioning::SlWiFiDriver::GetInstance();
    // Cannot use the driver if the instance is not initialized.
    VerifyOrDie(nwDriver != nullptr); // should never be null
    nwDriver->UpdateNetworkingStatus();
}

void ConnectivityManagerImpl::UpdateInternetConnectivityState(void)
{
    bool haveIPv4Conn = false;
    bool haveIPv6Conn = false;
    bool hadIPv4Conn  = mFlags.Has(ConnectivityFlags::kHaveIPv4InternetConnectivity);
    bool hadIPv6Conn  = mFlags.Has(ConnectivityFlags::kHaveIPv6InternetConnectivity);
    IPAddress addr;

    // If the WiFi station is currently in the connected state...
    if (mWiFiStationState == kWiFiStationState_Connected)
    {
#if CHIP_DEVICE_CONFIG_ENABLE_IPV4
        haveIPv4Conn = WifiInterface::GetInstance().HasAnIPv4Address();
#endif /* CHIP_DEVICE_CONFIG_ENABLE_IPV4 */
        haveIPv6Conn = WifiInterface::GetInstance().HasAnIPv6Address();
    }

    // If the internet connectivity state has changed...
    if (haveIPv4Conn != hadIPv4Conn || haveIPv6Conn != hadIPv6Conn)
    {
        // Update the current state.
        mFlags.Set(ConnectivityFlags::kHaveIPv4InternetConnectivity, haveIPv4Conn)
            .Set(ConnectivityFlags::kHaveIPv6InternetConnectivity, haveIPv6Conn);

        // Alert other components of the state change.
        ChipDeviceEvent event;
        event.Type                                 = DeviceEventType::kInternetConnectivityChange;
        event.InternetConnectivityChange.IPv4      = GetConnectivityChange(hadIPv4Conn, haveIPv4Conn);
        event.InternetConnectivityChange.IPv6      = GetConnectivityChange(hadIPv6Conn, haveIPv6Conn);
        event.InternetConnectivityChange.ipAddress = addr;

        if (haveIPv4Conn != hadIPv4Conn)
        {
            ChipLogProgress(DeviceLayer, "%s Internet connectivity %s", "IPv4", (haveIPv4Conn) ? "ESTABLISHED" : "LOST");
        }

        if (haveIPv6Conn != hadIPv6Conn)
        {
            ChipLogProgress(DeviceLayer, "%s Internet connectivity %s", "IPv6", (haveIPv6Conn) ? "ESTABLISHED" : "LOST");
        }
        (void) PlatformMgr().PostEvent(&event);
    }
}

} // namespace DeviceLayer
} // namespace chip
