get_filename_component(gtos_root "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)

idf_component_register(
    SRCS "boot.c" "kernel.cpp"
         "${gtos_root}/src/common/boot_log.cpp"
         "${gtos_root}/src/memorymanagement.cpp"
         "${gtos_root}/src/apps/package.cpp"
         "${gtos_root}/src/apps/vm.cpp"
    PRIV_INCLUDE_DIRS "${gtos_root}/include"
    PRIV_REQUIRES esp_psram heap
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE GTOS_EXTERNAL_NEW_DELETE)

idf_build_get_property(python PYTHON)
set(catch_package "${CMAKE_CURRENT_BINARY_DIR}/catch.gtapp")
add_custom_command(OUTPUT "${catch_package}"
    COMMAND "${python}" "${gtos_root}/tools/package.py" build
            "${gtos_root}/apps/catch.json" "${catch_package}"
    DEPENDS "${gtos_root}/tools/package.py" "${gtos_root}/apps/catch.json"
    VERBATIM
)
target_add_binary_data(${COMPONENT_LIB} "${catch_package}" BINARY)
