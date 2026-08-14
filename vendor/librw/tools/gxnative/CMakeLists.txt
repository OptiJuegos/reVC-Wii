add_executable(gxnative
    gxnative.cpp
)

target_link_libraries(gxnative
    PUBLIC
        librw::librw
)

if(LIBRW_INSTALL)
    install(TARGETS gxnative
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
endif()

librw_platform_target(gxnative INSTALL)
