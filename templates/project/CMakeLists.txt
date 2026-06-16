cmake_minimum_required(VERSION 3.20)
project(Recompiled8BitGame LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SGRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "SG3000Recomp root")

add_subdirectory("${SGRECOMP_DIR}" "${CMAKE_BINARY_DIR}/sgrecomp")

add_executable(${PROJECT_NAME}
    main.cpp
    generated_rom.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE sgrecomp_runtime)
