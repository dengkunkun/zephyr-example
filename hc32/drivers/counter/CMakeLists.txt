# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_COUNTER_HC32_TMRA counter_hc32_tmra.c)
