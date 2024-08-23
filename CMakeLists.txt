cmake_minimum_required(VERSION "3.25")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

project(PaperRenderer)

set(source_dir "${PROJECT_SOURCE_DIR}/src")

file(GLOB_RECURSE main_s ${source_dir}/*.cpp)
file(GLOB_RECURSE main_h ${source_dir}/*.h)

add_library(${PROJECT_NAME} STATIC ${main_s} ${main_h})

#VULKAN
message("Getting Vulkan package")
find_package(Vulkan REQUIRED)
message("Vulkan Version: ${Vulkan_VERSION}")

#VOLK
message("Getting Volk package")
set(VOLK_SOURCE_DIR "${PROJECT_SOURCE_DIR}/lib/volk")
add_subdirectory(${VOLK_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC volk)
target_include_directories(volk PUBLIC "$<BUILD_INTERFACE:${VOLK_SOURCE_DIR}>$<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/volk>")

#GLM
message("Getting GLM package")
find_package(glm REQUIRED)

#GLFW LIBRARY
message("GLFW")
set(GLFW_INCUDE_DIR "${PROJECT_SOURCE_DIR}/lib/glfw/include")
set(GLFW_SOURCE_DIR "${PROJECT_SOURCE_DIR}/lib/glfw/")
add_subdirectory(${GLFW_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC glfw)
target_include_directories(glfw PUBLIC "$<BUILD_INTERFACE:${GLFW_INCUDE_DIR}>$<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/GLFW>")

#INCLUDES
set(INCLUDE_DIRS "${Vulkan_INCLUDE_DIRS};${VOLK_SOURCE_DIR};${GLFW_INCUDE_DIR};${GLM_INCUDE_DIR}")
include_directories(${INCLUDE_DIRS})

add_dependencies(${PROJECT_NAME} glfw)