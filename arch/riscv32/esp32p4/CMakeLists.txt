cmake_minimum_required(VERSION 3.22)
set(IDF_TARGET esp32p4)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(gtos-a3)

include(cmake/hosted_sdio_cleanup.cmake)

if(GTOS_A3_PANEL_PROFILE)
    include(cmake/dsi_timeouts.cmake)
    include(cmake/dma2d_readiness.cmake)
endif()
