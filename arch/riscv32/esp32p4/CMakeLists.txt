cmake_minimum_required(VERSION 3.22)
set(IDF_TARGET esp32p4)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(gtos-a3)

if(GTOS_A3_PANEL_PROFILE)
    include(cmake/dsi_phy_timeout.cmake)
endif()
