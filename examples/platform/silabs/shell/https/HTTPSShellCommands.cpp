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

#include "HTTPSShellCommands.h"

#include "AppEvent.h"
#include "BaseApplication.h"
#include "https_offload_example.h"

#include <cinttypes>
#include <cstring>
#include <lib/shell/Command.h>
#include <lib/shell/Engine.h>
#include <lib/shell/streamer.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/silabs/services/https_downloader.h>
#include <platform/silabs/services/vendor_ota_requestor.h>

using namespace chip;
using Shell::Engine;
using Shell::streamer_get;
using Shell::streamer_printf;

namespace {

using chip::HttpsDownloader;
using chip::VendorOTARequestor;

char gPendingOtaUrl[HttpsDownloader::kMaxUrlLen];
size_t gPendingOtaUrlLen = 0;

void HttpsOtaUrlAppEvent(AppEvent * /* aEvent */)
{
    // HttpClient + AppDelegate are registered from StartMatterServicesAppEvent /
    // https_client_demo_start on connectivity. StartUpdate fails with incorrect-state
    // if the client is not running yet (same contract as the periodic check).
    CHIP_ERROR schedErr = chip::DeviceLayer::SystemLayer().ScheduleLambda([] {
        CHIP_ERROR err =
            chip::VendorOTARequestor::GetInstance().StartUpdate(chip::CharSpan(gPendingOtaUrl, gPendingOtaUrlLen));
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(SoftwareUpdate, "[HTTPS-OTA] StartUpdate failed: %" CHIP_ERROR_FORMAT, err.Format());
        }
    });
    if (schedErr != CHIP_NO_ERROR)
    {
        ChipLogError(SoftwareUpdate, "[HTTPS-OTA] ScheduleLambda failed: %" CHIP_ERROR_FORMAT, schedErr.Format());
    }
}

CHIP_ERROR HttpsOtaQueueUrl(const char * url)
{
    VerifyOrReturnError(url != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    const size_t len = strlen(url);
    VerifyOrReturnError(len > 0 && len < sizeof(gPendingOtaUrl), CHIP_ERROR_INVALID_ARGUMENT);
    memcpy(gPendingOtaUrl, url, len);
    gPendingOtaUrl[len] = '\0';
    gPendingOtaUrlLen   = len;

    ChipLogProgress(SoftwareUpdate, "[HTTPS-OTA] using passed url=%s", gPendingOtaUrl);

    AppEvent event = {};
    event.Type     = AppEvent::kEventType_Timer;
    event.Handler  = HttpsOtaUrlAppEvent;
    BaseApplication::PostEvent(&event);
    return CHIP_NO_ERROR;
}

CHIP_ERROR HttpsOtaAbort(void)
{
    return chip::DeviceLayer::SystemLayer().ScheduleLambda(
        [] { chip::VendorOTARequestor::GetInstance().CancelImageUpdate(); });
}

CHIP_ERROR HttpsOtaSetPeriodicUrl(const char * url)
{
    VerifyOrReturnError(url != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    const size_t len = strlen(url);
    VerifyOrReturnError(len > 0 && len < HttpsDownloader::kMaxUrlLen, CHIP_ERROR_INVALID_ARGUMENT);
    return HttpsOtaAppSetPeriodicUrl(CharSpan(url, len));
}

CHIP_ERROR HttpsOtaStatusCommand(void)
{
    auto & req = VendorOTARequestor::GetInstance();
    auto * dl  = req.GetHttpsDownloader();
    auto * ip  = req.GetImageProcessor();

    using OTAUpdateStateEnum = chip::OTARequestorInterface::OTAUpdateStateEnum;
    const char * stateStr    = "idle";
    switch (req.GetCurrentUpdateState())
    {
    case OTAUpdateStateEnum::kIdle:
        stateStr = "idle";
        break;
    case OTAUpdateStateEnum::kQuerying:
        stateStr = "querying";
        break;
    case OTAUpdateStateEnum::kDownloading:
        stateStr = "downloading";
        break;
    case OTAUpdateStateEnum::kApplying:
        stateStr = "applying";
        break;
    default:
        stateStr = "other";
        break;
    }

    const uint32_t offset = (dl != nullptr) ? dl->GetNextOffset() : 0;
    const uint64_t bytes  = (ip != nullptr) ? ip->GetBytesDownloaded() : 0;
    auto percent          = (ip != nullptr) ? ip->GetPercentComplete() : chip::app::DataModel::Nullable<uint8_t>{};

    streamer_printf(streamer_get(), "HTTPS OTA state=%s offset=%" PRIu32 " downloaded=%" PRIu64, stateStr, offset, bytes);
    if (percent.IsNull())
    {
        streamer_printf(streamer_get(), " total=unknown percent=n/a");
    }
    else
    {
        streamer_printf(streamer_get(), " percent=%u", percent.Value());
    }
    streamer_printf(streamer_get(), " target=%" PRIu32, req.GetTargetVersion());

    CHIP_ERROR lastErr;
    VendorOTARequestor::LastResultReason reason;
    LogErrorOnFailure(req.GetLastResult(lastErr, reason));
    streamer_printf(streamer_get(), " last=%s %" CHIP_ERROR_FORMAT "\r\n", VendorOTARequestor::LastResultReasonToString(reason),
                    lastErr.Format());
    return CHIP_NO_ERROR;
}

void PrintHttpsOtaHelp()
{
    streamer_printf(streamer_get(), "Usage: https-ota <url|abort|default|status>\r\n");
    streamer_printf(streamer_get(), "  url <url>       Start one-shot HTTPS OTA (no NVM write)\r\n");
    streamer_printf(streamer_get(), "  abort           Cancel in-progress HTTPS OTA\r\n");
    streamer_printf(streamer_get(), "  default <url>   Set periodic check URL (RAM only, lost on reboot)\r\n");
    streamer_printf(streamer_get(), "  status          Show state / progress / last result\r\n");
}

CHIP_ERROR HttpsOtaHelpHandler(int /* argc */, char ** /* argv */)
{
    PrintHttpsOtaHelp();
    return CHIP_NO_ERROR;
}

CHIP_ERROR HttpsOtaCommandHandler(int argc, char ** argv)
{
    if (argc == 0)
    {
        return HttpsOtaHelpHandler(argc, argv);
    }

    if (strcmp(argv[0], "url") == 0)
    {
        VerifyOrReturnError(argc == 2, CHIP_ERROR_INVALID_ARGUMENT);
        return HttpsOtaQueueUrl(argv[1]);
    }
    if (strcmp(argv[0], "abort") == 0)
    {
        VerifyOrReturnError(argc == 1, CHIP_ERROR_INVALID_ARGUMENT);
        return HttpsOtaAbort();
    }
    if (strcmp(argv[0], "default") == 0)
    {
        VerifyOrReturnError(argc == 2, CHIP_ERROR_INVALID_ARGUMENT);
        return HttpsOtaSetPeriodicUrl(argv[1]);
    }
    if (strcmp(argv[0], "status") == 0)
    {
        VerifyOrReturnError(argc == 1, CHIP_ERROR_INVALID_ARGUMENT);
        return HttpsOtaStatusCommand();
    }

    PrintHttpsOtaHelp();
    return CHIP_ERROR_INVALID_ARGUMENT;
}

} // namespace

namespace HTTPSShellCommands {

void RegisterCommands()
{
    static const Shell::Command sHttpsOtaCmd = { &HttpsOtaCommandHandler, "https-ota",
                                                 "Vendor HTTPS OTA: https-ota <url|abort|default|status>" };
    Engine::Root().RegisterCommands(&sHttpsOtaCmd, 1);
}

} // namespace HTTPSShellCommands
