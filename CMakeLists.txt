cmake_minimum_required(VERSION 3.20)
project(SG3000Recomp
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Static recompilation toolkit for SMS and SG-3000 ROMs"
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(SGRECOMP_BUILD_TOOLS "Build SG3000Recomp command line tools" ON)
option(SGRECOMP_BUILD_HOST "Build SG3000Recomp host executables" ON)
option(SGRECOMP_BUILD_TESTS "Build SG3000Recomp tests" ON)
option(SGRECOMP_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(sgrecomp_runtime STATIC
    src/runtime/bus.cpp
    src/runtime/console.cpp
    src/runtime/host_runtime.cpp
    src/runtime/joypad.cpp
    src/runtime/psg.cpp
    src/runtime/vdp.cpp
    src/runtime/z80.cpp
)

target_include_directories(sgrecomp_runtime PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

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

    target_link_libraries(sgrecomp_host PRIVATE sgrecomp_runtime user32 gdi32 winmm)

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
        )

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
    endif()
endif()

install(TARGETS sgrecomp_runtime
    EXPORT SG3000RecompTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

if(TARGET sgrecomp)
    install(TARGETS sgrecomp RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if(TARGET sgrecomp_host)
    install(TARGETS sgrecomp_host RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
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
