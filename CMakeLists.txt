cmake_minimum_required(VERSION 3.8 FATAL_ERROR)
project(tgfs VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_FILE_OFFSET_BITS=64")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall --pedantic -g")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

find_package(FUSE REQUIRED)
add_subdirectory(td)

include_directories(${FUSE_INCLUDE_DIR})
add_executable(tgfs main.cpp)
target_link_libraries(tgfs PRIVATE ${FUSE_LIBRARIES} Td::TdStatic)
