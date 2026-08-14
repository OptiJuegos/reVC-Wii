if(NOT COMMAND ogc_create_dol)
    message(FATAL_ERROR "The `ogc_create_dol` cmake command is not available. Please use an appropriate Nintendo Wii toolchain.")
endif()

set(CMAKE_EXECUTABLE_SUFFIX ".elf")

function(reVC_platform_target TARGET)
    cmake_parse_arguments(RPT "INSTALL" "" "" ${ARGN})

    get_target_property(TARGET_TYPE "${TARGET}" TYPE)
    if(TARGET_TYPE STREQUAL "EXECUTABLE")
        ogc_create_dol(${TARGET})

        if(${PROJECT}_INSTALL AND RPT_INSTALL)
            get_target_property(TARGET_OUTPUT_NAME ${TARGET} OUTPUT_NAME)
            if(NOT TARGET_OUTPUT_NAME)
                set(TARGET_OUTPUT_NAME "${TARGET}")
            endif()

            install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_OUTPUT_NAME}.dol"
                DESTINATION "."
            )
        endif()
    endif()
endfunction()
