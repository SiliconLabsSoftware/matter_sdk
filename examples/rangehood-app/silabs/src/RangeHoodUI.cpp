/*
 *
 *    Copyright (c) 2024 Project CHIP Authors
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

#include <stdio.h>
#include <string.h>

#include "AppConfig.h"
#include "AppTask.h"
#include "RangeHoodManager.h"
#include "RangeHoodUI.h"
#include "demo-ui-bitmaps.h"
#include "dmd.h"
#include "glib.h"
#include "lcd.h"

#include <platform/CHIPDeviceLayer.h>

using namespace chip::app::Clusters;
using namespace chip::app::Clusters::FanControl;
using namespace ::chip::DeviceLayer;

namespace {
// Bitmap
const uint8_t silabsLogo[]       = { SILABS_LOGO_SMALL };
const uint8_t matterLogoBitmap[] = { MATTER_LOGO_BITMAP };

const uint8_t wifiLogo[]   = { WIFI_BITMAP };
const uint8_t threadLogo[] = { THREAD_BITMAP };
const uint8_t bleLogo[]    = { BLUETOOTH_ICON_SMALL };

#ifdef SL_WIFI
constexpr bool UI_WIFI = true;
#else
constexpr bool UI_WIFI = false;
#endif
} // namespace

void RangeHoodUI::DrawUI(GLIB_Context_t * glibContext)
{
    if (glibContext == nullptr)
    {
        ChipLogError(AppServer, "Context is null");
        return;
    }

    GLIB_clear(glibContext);
    DrawHeader(glibContext);
    DrawRangehoodStatus(glibContext);

#if SL_LCDCTRL_MUX
    sl_wfx_host_pre_lcd_spi_transfer();
#endif // SL_LCDCTRL_MUX
    DMD_updateDisplay();
#if SL_LCDCTRL_MUX
    sl_wfx_host_post_lcd_spi_transfer();
#endif // SL_LCDCTRL_MUX
}

void RangeHoodUI::DrawHeader(GLIB_Context_t * glibContext)
{
    // Draw Silabs Corner icon
    GLIB_drawBitmap(glibContext, SILABS_ICON_POSITION_X, STATUS_ICON_LINE, SILABS_LOGO_WIDTH, SILABS_LOGO_HEIGHT, silabsLogo);
    // Draw BLE Icon
    GLIB_drawBitmap(glibContext, BLE_ICON_POSITION_X, STATUS_ICON_LINE, BLUETOOTH_ICON_SIZE, BLUETOOTH_ICON_SIZE, bleLogo);
    // Draw WiFi/OpenThread Icon
    GLIB_drawBitmap(glibContext, NETWORK_ICON_POSITION_X, STATUS_ICON_LINE, (UI_WIFI) ? WIFI_BITMAP_WIDTH : THREAD_BITMAP_WIDTH,
                    WIFI_BITMAP_HEIGHT, (UI_WIFI) ? wifiLogo : threadLogo);
    // Draw Matter Icon
    GLIB_drawBitmap(glibContext, MATTER_ICON_POSITION_X, STATUS_ICON_LINE, MATTER_LOGO_WIDTH, MATTER_LOGO_HEIGHT, matterLogoBitmap);

    // Draw application name on a dedicated line below icons.
    GLIB_drawStringOnLine(glibContext, APP_TASK_NAME, 3, GLIB_ALIGN_CENTER, 0, 0, true);
#if SL_LCDCTRL_MUX
    sl_wfx_host_pre_lcd_spi_transfer();
#endif // SL_LCDCTRL_MUX
    DMD_updateDisplay();
#if SL_LCDCTRL_MUX
    sl_wfx_host_post_lcd_spi_transfer();
#endif // SL_LCDCTRL_MUX
}

/**
 * @brief Draw a 2 digit Air Quality of screen. Because of this Celsius is used by default
 * @param GLIB_Context_t * pointer to the context for the GLIB library
 * @param int8_t current Air Quality
 */
void RangeHoodUI::DrawRangehoodStatus(GLIB_Context_t * glibContext)
{
    SILABS_LOG("Updating Rangehood Status on LCD");

    FanModeEnum mode = FanModeEnum::kUnknownEnumValue;
    bool lightOn     = false;

    PlatformMgr().LockChipStack();
    RangeHoodMgr().GetExtractorHoodEndpoint().GetFanMode(mode);
    RangeHoodMgr().GetLightEndpoint().GetOnOffState(lightOn);
    PlatformMgr().UnlockChipStack();

    // Print fan mode
    if (mode == FanModeEnum::kOff)
    {
        GLIB_drawStringOnLine(glibContext, "FAN   : OFF", 5, GLIB_ALIGN_LEFT, 0, 0, true);
    }
    else if (mode == FanModeEnum::kOn || mode == FanModeEnum::kHigh || mode == FanModeEnum::kLow || mode == FanModeEnum::kMedium ||
             mode == FanModeEnum::kAuto || mode == FanModeEnum::kSmart)
    {
        GLIB_drawStringOnLine(glibContext, "FAN   : ON", 5, GLIB_ALIGN_LEFT, 0, 0, true);
    }
    else
    {
        GLIB_drawStringOnLine(glibContext, "FAN   : UNKNOWN", 5, GLIB_ALIGN_LEFT, 0, 0, true);
    }

    // Draw Light status below fan information
    GLIB_drawStringOnLine(glibContext, lightOn ? "LIGHT : ON" : "LIGHT : OFF", 7, GLIB_ALIGN_LEFT, 0, 0, true);

#if SL_LCDCTRL_MUX
    sl_wfx_host_pre_lcd_spi_transfer();
#endif // SL_LCDCTRL_MUX
    DMD_updateDisplay();
#if SL_LCDCTRL_MUX
    sl_wfx_host_post_lcd_spi_transfer();
#endif // SL_LCDCTRL_MUX
}
