/*
 *    Copyright (c) 2023-2024 Project CHIP Authors
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

// #include "AppTask.h"

// #include <zephyr/logging/log.h>

// LOG_MODULE_REGISTER(app, CONFIG_CHIP_APP_LOG_LEVEL);

// using namespace ::chip;

// int main()
// {   
//     // TODO MATTER-

//     // CHIP_ERROR err = CHIP_NO_ERROR;

//     // if (err == CHIP_NO_ERROR)
//     // {
//     //     err = chip::silabs::App::GetAppTask().Start();
//     // }

//     // LOG_ERR("Exited with code %" CHIP_ERROR_FORMAT, err.Format());
//     // return err == CHIP_NO_ERROR ? EXIT_SUCCESS : EXIT_FAILURE;
// }


// For the time being, simple run the standard blinky app from Zephyr
/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

 #include <stdio.h>
 #include <zephyr/kernel.h>
 #include <zephyr/drivers/gpio.h>
 
 /* 1000 msec = 1 sec */
 #define SLEEP_TIME_MS   1000
 
 /* The devicetree node identifier for the "led0" alias. */
 #define LED0_NODE DT_ALIAS(led0)
 
 /*
  * A build error on this line means your board is unsupported.
  * See the sample documentation for information on how to fix this.
  */
 static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
 
 int main(void)
 {
     int ret;
     bool led_state = true;
 
     if (!gpio_is_ready_dt(&led)) {
         return 0;
     }
 
     ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
     if (ret < 0) {
         return 0;
     }
 
     while (1) {
         ret = gpio_pin_toggle_dt(&led);
         if (ret < 0) {
             return 0;
         }
 
         led_state = !led_state;
         printf("LED state: %s\n", led_state ? "ON" : "OFF");
         k_msleep(SLEEP_TIME_MS);
     }
     return 0;
 }
