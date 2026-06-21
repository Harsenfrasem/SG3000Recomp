cmake_minimum_required(VERSION 3.20)
project(SG3000Recomp
    VERSION 0.1.0
    LANGUAGES C CXX
    DESCRIPTION "Static recompilation toolkit for SMS and SG-3000 ROMs"
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(nuked_opn2 SHARED
    third_party/Nuked-OPN2/ym3438.c
)
if(WIN32)
    target_sources(nuked_opn2 PRIVATE third_party/Nuked-OPN2/nuked_opn2.def)
endif()
target_include_directories(nuked_opn2 PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/Nuked-OPN2>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/Nuked-OPN2>"
)
set_target_properties(nuked_opn2 PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    PREFIX ""
)

option(SGRECOMP_BUILD_TOOLS "Build SG3000Recomp command line tools" ON)
option(SGRECOMP_BUILD_HOST "Build SG3000Recomp host executables" ON)
option(SGRECOMP_BUILD_TESTS "Build SG3000Recomp tests" ON)
option(SGRECOMP_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(SGRECOMP_REQUIRE_CLANG_FORMAT "Fail configure when clang-format is unavailable" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_program(SGRECOMP_CLANG_FORMAT_EXECUTABLE NAMES clang-format clang-format-19)
if(SGRECOMP_REQUIRE_CLANG_FORMAT AND NOT SGRECOMP_CLANG_FORMAT_EXECUTABLE)
    message(FATAL_ERROR "clang-format 19 is required by this configuration")
endif()

file(GLOB_RECURSE SGRECOMP_FORMAT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
)

if(SGRECOMP_CLANG_FORMAT_EXECUTABLE)
    add_custom_target(format
        COMMAND "${SGRECOMP_CLANG_FORMAT_EXECUTABLE}" -i --style=file ${SGRECOMP_FORMAT_SOURCES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Formatting C++ sources"
        VERBATIM
    )
    add_custom_target(format-check
        COMMAND "${SGRECOMP_CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror --style=file ${SGRECOMP_FORMAT_SOURCES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Checking C++ source formatting"
        VERBATIM
    )
endif()

add_library(sgrecomp_runtime STATIC
    src/runtime/audio_latency.cpp
    src/runtime/bus.cpp
    src/runtime/cartridge.cpp
    src/runtime/console.cpp
    src/runtime/game_profile.cpp
    src/runtime/game_library.cpp
    src/runtime/host_runtime.cpp
    src/runtime/input_script.cpp
    src/runtime/joypad.cpp
    src/runtime/media_io.cpp
    src/runtime/psg.cpp
    src/runtime/recent_games.cpp
    src/runtime/save_state.cpp
    src/runtime/vdp.cpp
    src/runtime/ym2413.cpp
    src/runtime/ym2612.cpp
    src/runtime/z80.cpp
    third_party/emu2413/emu2413.c
)

target_include_directories(sgrecomp_runtime PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
target_include_directories(sgrecomp_runtime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/emu2413"
)
target_link_libraries(sgrecomp_runtime PUBLIC nuked_opn2)

if(MSVC)
    target_compile_options(sgrecomp_runtime PRIVATE /W4 /permissive-)
else()
    target_compile_options(sgrecomp_runtime PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SGRECOMP_WARNINGS_AS_ERRORS)
    if(MSVC)
        target_compile_options(sgrecomp_runtime PRIVATE /WX)
    else()
        target_compile_options(sgrecomp_runtime PRIVATE -Werror)
    endif()
endif()

if(SGRECOMP_BUILD_TOOLS)
    add_executable(sgrecomp
        src/tools/sgrecomp.cpp
    )

    target_link_libraries(sgrecomp PRIVATE sgrecomp_runtime)

    if(MSVC)
        target_compile_options(sgrecomp PRIVATE /W4 /permissive-)
    else()
        target_compile_options(sgrecomp PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(SGRECOMP_WARNINGS_AS_ERRORS)
        if(MSVC)
            target_compile_options(sgrecomp PRIVATE /WX)
        else()
            target_compile_options(sgrecomp PRIVATE -Werror)
        endif()
    endif()
endif()

if(SGRECOMP_BUILD_HOST AND WIN32)
    add_executable(sgrecomp_host
        src/host/win32_host.cpp
    )

    target_link_libraries(sgrecomp_host PRIVATE sgrecomp_runtime user32 gdi32 winmm comdlg32)

    add_custom_command(TARGET sgrecomp_host POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:sgrecomp_host>"
            "$<TARGET_FILE_DIR:sgrecomp_host>/SG3000Recomp.exe"
        COMMENT "Creating user-facing SG3000Recomp.exe"
    )

    if(MSVC)
        target_compile_options(sgrecomp_host PRIVATE /W4 /permissive-)
    else()
        target_compile_options(sgrecomp_host PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(SGRECOMP_WARNINGS_AS_ERRORS)
        if(MSVC)
            target_compile_options(sgrecomp_host PRIVATE /WX)
        else()
            target_compile_options(sgrecomp_host PRIVATE -Werror)
        endif()
    endif()
endif()

if(SGRECOMP_BUILD_TESTS)
    enable_testing()

    add_executable(sgrecomp_tests
        tests/z80_smoke.cpp
    )

    target_link_libraries(sgrecomp_tests PRIVATE sgrecomp_runtime)

    if(MSVC)
        target_compile_options(sgrecomp_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(sgrecomp_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(SGRECOMP_WARNINGS_AS_ERRORS)
        if(MSVC)
            target_compile_options(sgrecomp_tests PRIVATE /WX)
        else()
            target_compile_options(sgrecomp_tests PRIVATE -Werror)
        endif()
    endif()

    add_test(NAME z80_smoke COMMAND sgrecomp_tests)

    add_executable(z80_external_conformance
        tests/z80_external_conformance.cpp
        tests/z80_external_vectors.inc
    )
    target_link_libraries(z80_external_conformance PRIVATE sgrecomp_runtime)
    if(MSVC)
        target_compile_options(z80_external_conformance PRIVATE /W4 /permissive-)
    else()
        target_compile_options(z80_external_conformance PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(SGRECOMP_WARNINGS_AS_ERRORS)
        if(MSVC)
            target_compile_options(z80_external_conformance PRIVATE /WX)
        else()
            target_compile_options(z80_external_conformance PRIVATE -Werror)
        endif()
    endif()
    add_test(NAME z80_external_conformance COMMAND z80_external_conformance)

    if(TARGET sgrecomp)
        add_executable(recompiler_tool_smoke
            tests/recompiler_tool_smoke.cpp
        )

        target_compile_definitions(recompiler_tool_smoke PRIVATE
            SGRECOMP_TOOL_PATH="$<TARGET_FILE:sgrecomp>"
            SGRECOMP_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/recompiler_tool_smoke"
            SGRECOMP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            SGRECOMP_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
            SGRECOMP_CXX_COMPILER_ARG1="${CMAKE_CXX_COMPILER_ARG1}"
            SGRECOMP_CXX_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
            SGRECOMP_RUNTIME_PATH="$<TARGET_FILE:sgrecomp_runtime>"
            SGRECOMP_OPN2_LINK_PATH="$<TARGET_LINKER_FILE:nuked_opn2>"
        )

        if(TARGET sgrecomp_host)
            target_compile_definitions(recompiler_tool_smoke PRIVATE
                SGRECOMP_HOST_PATH="$<TARGET_FILE:sgrecomp_host>"
            )
        endif()

        if(MSVC)
            target_compile_options(recompiler_tool_smoke PRIVATE /W4 /permissive-)
        else()
            target_compile_options(recompiler_tool_smoke PRIVATE -Wall -Wextra -Wpedantic)
        endif()

        if(SGRECOMP_WARNINGS_AS_ERRORS)
            if(MSVC)
                target_compile_options(recompiler_tool_smoke PRIVATE /WX)
            else()
                target_compile_options(recompiler_tool_smoke PRIVATE -Werror)
            endif()
        endif()

        add_test(NAME recompiler_tool_smoke COMMAND recompiler_tool_smoke)
    endif()

    if(TARGET sgrecomp_host)
        add_test(NAME win32_host_help COMMAND sgrecomp_host --help)
        add_test(NAME win32_host_idle COMMAND sgrecomp_host --gui --mute --show-status --quit-after-frames 1)
    endif()
endif()

install(TARGETS sgrecomp_runtime nuked_opn2
    EXPORT SG3000RecompTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(FILES third_party/Nuked-OPN2/ym3438.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/Nuked-OPN2")

if(TARGET sgrecomp)
    install(TARGETS sgrecomp RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if(TARGET sgrecomp_host)
    install(TARGETS sgrecomp_host RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES "$<TARGET_FILE:sgrecomp_host>"
        DESTINATION "."
        RENAME SG3000Recomp.exe
        COMPONENT Portable
    )
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        DESTINATION "."
        COMPONENT Portable
    )
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Nuked-OPN2/LICENSE"
        DESTINATION "."
        RENAME NUKED-OPN2-LICENSE.txt
        COMPONENT Portable
    )
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/emu2413/LICENSE"
        DESTINATION "."
        RENAME EMU2413-LICENSE.txt
        COMPONENT Portable
    )
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/PORTABLE.md"
        DESTINATION "."
        RENAME README.md
        COMPONENT Portable
    )
    install(FILES "$<TARGET_FILE:nuked_opn2>" DESTINATION "." COMPONENT Portable)
endif()

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(EXPORT SG3000RecompTargets
    NAMESPACE SG3000Recomp::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SG3000Recomp"
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SG3000RecompConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SG3000RecompConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SG3000Recomp"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SG3000RecompConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/SG3000RecompConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SG3000RecompConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/SG3000Recomp"
)

if(WIN32 AND TARGET sgrecomp_host)
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_NAME "SG3000Recomp")
    set(CPACK_PACKAGE_VENDOR "SG3000Recomp")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_FILE_NAME "SG3000Recomp-${PROJECT_VERSION}-windows-x64-portable")
    set(CPACK_PACKAGE_CHECKSUM "SHA256")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
    set(CPACK_COMPONENTS_ALL Portable)
    set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
    set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
    include(CPack)
endif()
