cmake_minimum_required(VERSION 3.24)

project(branch_prediction VERSION 0.1.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 20)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/tao)
include_directories(${CMAKE_SOURCE_DIR}/tao/pegtl)
include_directories(${CMAKE_SOURCE_DIR}/tao/json)

include_directories(tao)
include_directories(tao/pegtl)
include_directories(tao/json)

add_executable(
    branch_prediction
    main.cpp
    if_counter.cpp
    cppref_example.cpp
    lint.cpp
    benchmark_tao.cpp
)
