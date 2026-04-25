cmake_minimum_required(VERSION 3.10)
project(SimpleDBMS)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

set(SOURCES
    src/main.cpp
    src/schema/Table.cpp
    src/storage/Pager.cpp
)

add_executable(prog ${SOURCES})

# Placeholder for tests
# enable_testing()
# add_subdirectory(tests)
