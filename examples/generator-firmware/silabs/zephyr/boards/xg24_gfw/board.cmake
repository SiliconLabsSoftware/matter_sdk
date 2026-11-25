# *******************************************************************************
# License
# <b>Copyright 2026 Silicon Laboratories Inc. www.silabs.com</b>
# *******************************************************************************
# The licensor of this software is Silicon Laboratories Inc.  Your use of this
# software is governed by the terms of Silicon Labs Master Software License
# agreement (MSLA) available at
# www.silabs.com/about-us/legal/master-software-license-agreement.  This
# software is distributed to you in Source Code format and is governed by the
# sections of the MSLA applicable to Source Code.
# *******************************************************************************

board_runner_args(jlink "--device=EFR32MG24BxxxF1536" "--reset-after-load")
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)

board_runner_args(silabs_commander "--device=${CONFIG_SOC}")
include(${ZEPHYR_BASE}/boards/common/silabs_commander.board.cmake)
