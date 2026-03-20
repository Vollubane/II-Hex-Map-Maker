cmake_minimum_required(VERSION 3.19)

project(iiHexMapMaker)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(cmake/GDExtensionBindings.cmake)
add_subdirectory(cpp)
