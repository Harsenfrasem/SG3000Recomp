cmake_minimum_required(VERSION 3.20)
project(SG3000Recomp
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Static recompilation toolkit for Sega Master System and SG-3000 ROMs"
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(SGRECOMP_BUILD_TOOLS "Build SG3000Recomp command line tools" ON)
option(SGRECOMP_BUILD_TESTS "Build SG3000Recomp tests" ON)
option(SGRECOMP_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(sgrecomp_runtime STATIC
    src/runtime/bus.cpp
    src/runtime/console.cpp
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
