# SPDX-License-Identifier: Apache-2.0
#
# The HC32F4A0PGTB target pack for pyOCD is `hc32f4a0xi` (2MB variant).
# If not installed locally, run `pyocd pack --install hc32f4a0xi` once.

board_runner_args(pyocd "--target=hc32f4a0xi")

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
