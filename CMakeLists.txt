cmake_minimum_required(VERSION 3.20)
project(server-client-tcp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
set(FETCHCONTENT_QUIET NO)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.0
    GIT_SHALLOW    TRUE
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

set(CORE_INC ${CMAKE_SOURCE_DIR}/src)

add_executable(server
    src/Server/Main.cpp
    src/Server/NetworkLoop.cpp
    src/Server/Zone.cpp
    src/Server/Entity.cpp
)
target_include_directories(server PRIVATE ${CORE_INC})
target_compile_options(server PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(server PRIVATE pthread)

add_executable(client
    src/Client/Main.cpp
    src/Client/NetworkWorker.cpp
    src/Client/Physics.cpp
)
target_include_directories(client PRIVATE ${CORE_INC})
target_compile_options(client PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(client PRIVATE raylib pthread)
