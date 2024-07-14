#To anybody reading this and possibly trying to use it, I'm sorry I suck at CMake. I have no idea how to properly handle the dependencies(below)

#DEPENDENCIES TO BE RESOLVED
#GLM
#GLFW
#VOLK

cmake_minimum_required(VERSION "3.25")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

project(PaperRenderer)

set(source_dir "${PROJECT_SOURCE_DIR}/src")

file(GLOB_RECURSE main_s ${source_dir}/*.cpp)
file(GLOB_RECURSE main_h ${source_dir}/*.h)

add_library(${PROJECT_NAME} STATIC ${main_s} ${main_h})