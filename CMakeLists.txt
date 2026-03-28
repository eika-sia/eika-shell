cmake_minimum_required(VERSION 3.10)
project(shell_lab)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB MAIN_FILE CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/main.cpp"
)

file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/shell/*.cpp"
    "${CMAKE_SOURCE_DIR}/parser/*.cpp"
    "${CMAKE_SOURCE_DIR}/builtins/*.cpp"
    "${CMAKE_SOURCE_DIR}/features/*.cpp"
)

add_executable(shell
    ${MAIN_FILE}
    ${MODULE_SOURCES}
)
