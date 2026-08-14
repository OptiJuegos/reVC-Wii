if(NINTENDO_WII)
    option(WII_GAME_BOOT "Build the staged reVC game boot instead of the GX asset viewer" OFF)

    if(WII_GAME_BOOT)
        file(GLOB_RECURSE WII_GAME_SOURCES "*.cpp" "*.h")
        list(FILTER WII_GAME_SOURCES EXCLUDE REGEX "/wii/")
        list(FILTER WII_GAME_SOURCES EXCLUDE REGEX "/wii-port/")
        list(FILTER WII_GAME_SOURCES EXCLUDE REGEX "/skel/(android|glfw|sdl2|win)/")
        list(FILTER WII_GAME_SOURCES EXCLUDE REGEX "/audio/(eax|oal)/")
        list(FILTER WII_GAME_SOURCES EXCLUDE REGEX "/audio/sampman_(miles|oal)\\.cpp$")
        list(APPEND WII_GAME_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/audio/WiiAudioDecoder.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/audio/WiiAudioStream.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/audio/WiiAudioStreaming.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/audio/WiiSfxCache.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/wii-port/wii_game.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/wii-port/WiiPad.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/wii-port/WiiTrace.cpp"
        )
        list(REMOVE_DUPLICATES WII_GAME_SOURCES)
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/extras/GitSHA1.cpp.in"
            "${CMAKE_CURRENT_BINARY_DIR}/extras/GitSHA1.cpp" @ONLY)
        list(APPEND WII_GAME_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/extras/GitSHA1.cpp")

        find_path(WII_MPG123_INCLUDE_DIR NAMES mpg123.h)
        find_library(WII_MPG123_LIBRARY NAMES mpg123)
        if(NOT WII_MPG123_INCLUDE_DIR OR NOT WII_MPG123_LIBRARY)
            message(FATAL_ERROR
                "Wii game audio requires ppc-mpg123; install it with "
                "'(dkp-)pacman -S ppc-mpg123'.")
        endif()

        file(GLOB_RECURSE WII_GAME_HEADERS "*.h")
        list(FILTER WII_GAME_HEADERS EXCLUDE REGEX "/wii-port/")
        # The port's own headers are kept out of the glob above, which sweeps in
        # the retired reference tree as well, but src/core has to reach WiiPad.h:
        # CPad::UpdateMouse drives the Wiimote pointer.  So the one directory is
        # added back by hand rather than by relaxing the filter.
        set(WII_GAME_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/wii-port")
        foreach(HEADER ${WII_GAME_HEADERS})
            get_filename_component(HEADER_DIR "${HEADER}" DIRECTORY)
            list(APPEND WII_GAME_INCLUDE_DIRS "${HEADER_DIR}")
        endforeach()
        list(REMOVE_DUPLICATES WII_GAME_INCLUDE_DIRS)

        add_executable(${EXECUTABLE} ${WII_GAME_SOURCES})
        # wiiuse and bte carry WPAD, and both resolve against libogc, so they
        # have to precede it in the link order the toolchain appends.
        target_link_libraries(${EXECUTABLE} PRIVATE
            librw::librw asnd wiiuse bte fat ${WII_MPG123_LIBRARY} m
        )
        target_include_directories(${EXECUTABLE} PRIVATE
            ${WII_GAME_INCLUDE_DIRS}
            ${WII_MPG123_INCLUDE_DIR}
        )
        target_compile_definitions(${EXECUTABLE} PRIVATE
            NINTENDO_WII RW_GX AUDIO_WII LIBRW CMAKE_BUILD USE_OUR_VERSIONING NDEBUG
            BIGENDIAN rwBIGENDIAN
        )
        target_compile_options(${EXECUTABLE} PRIVATE -w -fshort-wchar)
        set_target_properties(${EXECUTABLE} PROPERTIES
            C_STANDARD 11 C_EXTENSIONS ON C_STANDARD_REQUIRED ON
            CXX_STANDARD 17 CXX_EXTENSIONS ON CXX_STANDARD_REQUIRED ON
        )
        reVC_platform_target(${EXECUTABLE} INSTALL)
        return()
    endif()

    # Isolated librw GX smoke-test target. This short-circuits before the full
    # game source glob and all desktop-only package discovery.
    set(WII_BRINGUP_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/wii-port/bringup_main.cpp")

    add_executable(${EXECUTABLE} ${WII_BRINGUP_SOURCES})
    target_link_libraries(${EXECUTABLE} PRIVATE librw::librw fat)

    # C_EXTENSIONS/CXX_EXTENSIONS mirror the C_CXX_EXTENSIONS handling further
    # down in this file, which this branch skips via return(): libogc/
    # devkitPPC headers rely on GNU extensions being on (see commit
    # 8f49af15). CXX_STANDARD is 17, not reVC's usual 11: this target doesn't
    # compile any reVC game code. C++17 matches the full Wii target.
    set_target_properties(${EXECUTABLE} PROPERTIES
        C_STANDARD 11 C_EXTENSIONS ON C_STANDARD_REQUIRED ON
        CXX_STANDARD 17 CXX_EXTENSIONS ON CXX_STANDARD_REQUIRED ON
    )

    reVC_platform_target(${EXECUTABLE} INSTALL)
    return()
endif()

find_package(Threads REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)

file(GLOB_RECURSE ${PROJECT}_SOURCES "*.cpp" "*.h" "*.rc")
# src/wii is a retired foreign reference tree and src/wii-port is the Wii
# platform skeleton gated behind NINTENDO_WII. Neither builds on the
# desktop/Android/Switch targets, and src/wii in particular pulls in
# devkitPPC-only headers unconditionally, so both must stay out of the blind
# recursive glob.
list(FILTER ${PROJECT}_SOURCES EXCLUDE REGEX "/wii(-port)?/")

function(header_directories RETURN_LIST)
    file(GLOB_RECURSE ALL_SRCS *.h *.cpp *.c)
    list(FILTER ALL_SRCS EXCLUDE REGEX "/wii(-port)?/")
    set(RELDIRS)
    foreach(SRC ${ALL_SRCS})
        file(RELATIVE_PATH RELSRC "${CMAKE_CURRENT_SOURCE_DIR}" "${SRC}")
        get_filename_component(RELDIR "${RELSRC}" DIRECTORY)
        list(APPEND RELDIRS ${RELDIR})
    endforeach()
    list(REMOVE_DUPLICATES RELDIRS)
    set(${RETURN_LIST} ${RELDIRS} PARENT_SCOPE)
endfunction()

header_directories(${PROJECT}_INCLUDES)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/extras/GitSHA1.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/extras/GitSHA1.cpp" @ONLY)
list(APPEND ${PROJECT}_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/extras/GitSHA1.cpp")

if(NOT ANDROID)
    add_executable(${EXECUTABLE} WIN32 ${${PROJECT}_SOURCES})
elseif(ANDROID)
    add_library(${EXECUTABLE} SHARED ${${PROJECT}_SOURCES})
endif()

target_link_libraries(${EXECUTABLE} PRIVATE
    librw::librw
    Threads::Threads
)

if(ANDROID)
    target_link_libraries(${EXECUTABLE} PRIVATE
            log
            GLESv3
            OpenSLES
    )
endif()

target_include_directories(${EXECUTABLE}
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${${PROJECT}_INCLUDES}>
)

target_compile_definitions(${EXECUTABLE}
    PRIVATE
        $<IF:$<CONFIG:DEBUG>,DEBUG,NDEBUG>
        LIBRW
        CMAKE_NO_AUTOLINK
)

if(LIBRW_PLATFORM_D3D9)
    target_compile_definitions(${EXECUTABLE}
        PUBLIC
            USE_D3D9
    )
endif()

if(ANDROID)
    target_compile_definitions(${EXECUTABLE} PRIVATE
            ANDROID
            _ANDROID
            LINUX
            _LINUX
            POSIX
            _POSIX
            NO_HOOK_MALLOC
            AL_LIBTYPE_STATIC
            GNUC
            RW_GL3
            LIBRW_SDL2
            LIBRW
            AUDIO_OAL
            USE_OUR_VERSIONING
            HAVE_EFFICIENT_UNALIGNED_ACCESS
            VECTORIZE_SINCOS
        )
else()
    target_compile_definitions(${EXECUTABLE} PRIVATE CMAKE_BUILD)
    target_compile_definitions(${EXECUTABLE} PRIVATE USE_OUR_VERSIONING)
endif()

if(${PROJECT}_AUDIO STREQUAL "OAL")
    find_package(OpenAL REQUIRED)
    if(TARGET OpenAL::OpenAL)
        target_link_libraries(${EXECUTABLE} PRIVATE OpenAL::OpenAL)
    else()
        target_include_directories(${EXECUTABLE} PRIVATE ${OPENAL_INCLUDE_DIR})
        target_link_libraries(${EXECUTABLE} PRIVATE ${OPENAL_LIBRARY})
        target_compile_definitions(${EXECUTABLE} PRIVATE ${OPENAL_DEFINITIONS})
    endif()
    target_compile_definitions(${EXECUTABLE} PRIVATE AUDIO_OAL)
elseif(${PROJECT}_AUDIO STREQUAL "MSS")
    find_package(MilesSDK REQUIRED)
    target_compile_definitions(${EXECUTABLE} PRIVATE AUDIO_MSS)
    target_link_libraries(${EXECUTABLE} PRIVATE MilesSDK::MilesSDK)
endif()

find_package(mpg123 REQUIRED)
target_link_libraries(${EXECUTABLE} PRIVATE
    MPG123::libmpg123
)
if(${PROJECT}_WITH_OPUS)
    find_package(opusfile REQUIRED)
    target_link_libraries(${EXECUTABLE} PRIVATE
        opusfile::opusfile
    )
    target_compile_definitions(${EXECUTABLE} PRIVATE AUDIO_OPUS)
endif()
if(${PROJECT}_WITH_LIBSNDFILE)
    find_package(SndFile REQUIRED)
    target_link_libraries(${EXECUTABLE} PRIVATE
        SndFile::SndFile
    )
    target_compile_definitions(${EXECUTABLE} PRIVATE AUDIO_OAL_USE_SNDFILE)
endif()

target_compile_definitions(${EXECUTABLE} PRIVATE )

option(${PROJECT}_WITH_SANITIZERS "Use UB sanitizers (better crash log)" OFF)
option(${PROJECT}_WITH_ASAN "Use Address sanitizer (better crash log)" OFF)
option(${PROJECT}_DISABLE_WARNINGS "Disable build warnings" ON)

if(${PROJECT}_DISABLE_WARNINGS)
    target_compile_options(${EXECUTABLE} PRIVATE
            -w)
endif()

if(${PROJECT}_WITH_SANITIZERS)
    target_compile_options(${EXECUTABLE} PUBLIC
            -fsanitize=undefined,float-divide-by-zero,integer,implicit-conversion,implicit-integer-truncation,implicit-integer-arithmetic-value-change,local-bounds,nullability
            -g3 -fno-omit-frame-pointer)
    target_link_options(${EXECUTABLE} PUBLIC -fsanitize=undefined,float-divide-by-zero,integer,implicit-conversion,implicit-integer-truncation,implicit-integer-arithmetic-value-change,local-bounds,nullability)
endif()

if(${PROJECT}_WITH_ASAN)
    target_compile_options(${EXECUTABLE} PUBLIC -fsanitize=address -g3 -fno-omit-frame-pointer)
    target_link_options(${EXECUTABLE} PUBLIC -fsanitize=address)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    if(ANDROID)
        target_compile_options(${EXECUTABLE}
            PRIVATE
                -pthread
                -std=c++11
                -fno-strict-aliasing
                -fexceptions
                -Wcast-align
                -Wuninitialized
                -Winit-self
                -Wstrict-aliasing
                -Wno-reorder
                -Wno-unknown-pragmas
                -Wno-unused-function
                -Wno-unused-but-set-variable
                -Wno-unused-value
                -Wno-unused-command-line-argument
                -faligned-new
        )
    endif()
    target_compile_options(${EXECUTABLE}
        PRIVATE
            "-Wall"
    )
    if (NOT LIBRW_PLATFORM_PS2)
        target_compile_options(${EXECUTABLE}
            PRIVATE
                -Wextra
                -Wdouble-promotion
                -Wpedantic
        )
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(${EXECUTABLE}
        PUBLIC
            /Zc:sizedDealloc-
    )
endif()

if(NINTENDO_SWITCH OR NINTENDO_WII)
    set(${PROJECT}_C_CXX_EXTENSIONS ON)
else()
    set(${PROJECT}_C_CXX_EXTENSIONS OFF)
endif()

if(LIBRW_PLATFORM_GL3 AND LIBRW_GL3_GFXLIB STREQUAL "GLFW")
	include(CheckSymbolExists)

	set(CMAKE_REQUIRED_LIBRARIES glfw)
	set(CMAKE_REQUIRED_DEFINITIONS -DGLFW_EXPOSE_NATIVE_X11)
	check_symbol_exists(glfwGetX11Display "GLFW/glfw3.h;GLFW/glfw3native.h" GLFW_HAS_X11)
	unset(CMAKE_REQUIRED_DEFINITIONS)
	unset(CMAKE_REQUIRED_LIBRARIES)

	if (GLFW_HAS_X11)
		find_package(X11 REQUIRED)
		target_link_libraries(${EXECUTABLE} PRIVATE X11::X11)
		target_compile_definitions(${EXECUTABLE} PRIVATE GET_KEYBOARD_INPUT_FROM_X11)
	endif (GLFW_HAS_X11)
endif()

set_target_properties(${EXECUTABLE}
    PROPERTIES
        C_STANDARD 11
        C_EXTENSIONS ${${PROJECT}_C_CXX_EXTENSIONS}
        C_STANDARD_REQUIRED ON
        CXX_STANDARD 11
        CXX_EXTENSIONS ${${PROJECT}_C_CXX_EXTENSIONS}
        CXX_STANDARD_REQUIRED ON
)

if(${PROJECT}_INSTALL)
    install(
        TARGETS ${EXECUTABLE}
        EXPORT ${EXECUTABLE}-targets
        RUNTIME DESTINATION "."
    )
    if(MSVC)
        install(FILES $<TARGET_PDB_FILE:${EXECUTABLE}> DESTINATION "." OPTIONAL)
    endif()
endif()

reVC_platform_target(${EXECUTABLE} INSTALL)
