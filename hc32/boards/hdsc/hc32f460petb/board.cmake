# SPDX-License-Identifier: Apache-2.0

board_runner_args(pyocd "--target=hc32f460xe")

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
