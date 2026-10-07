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

#pragma once

#include "sl_status.h"

#define HTTP_SERVER_IP "192.168.0.191"
#define HTTP_HOSTNAME "example.com"
#define HTTP_PORT 8443

#ifndef HTTP_SERVER_IP // IP address of the HTTP server
#error "HTTP_SERVER_IP is not defined"
#endif

#ifndef HTTP_HOSTNAME // used for SNI validation
#error "HTTP_HOSTNAME is not defined"
#endif

#ifndef HTTP_PORT // port of the HTTP server
#error "HTTP_PORT is not defined"
#endif

#ifndef HTTP_URL
#define HTTP_URL "/index.html" // URL of the resource on the HTTP server
#endif

#ifndef HTTP_USER
#define HTTP_USER "john" // username for the HTTP server
#endif

#ifndef HTTP_PASS
#define HTTP_PASS "doe" // password for the HTTP server
#endif

#ifndef HTTP_POST_DATA
#define HTTP_POST_DATA                                                                                                             \
    "employee_name=MR.REDDY&employee_id=RSXYZ123&designation=Engineer&company=SILABS&location=Hyderabad" // POST data for the HTTP
                                                                                                         // // server
#endif

/** Load config + start HTTPS offload client (Init only). Call from AppTask. */
sl_status_t https_client_demo_start(void);

/**
 * Run HTTPS PUT / GET / POST against the configured server.
 * Call from AppTask (or another non-CHIP thread), not from the CHIP event loop or shell thread.
 */
sl_status_t https_client_demo_run(void);

struct AppEvent;

/** AppTask-queue handler that runs https_client_demo_run(). */
void HttpsRunAppEvent(AppEvent * aEvent);
