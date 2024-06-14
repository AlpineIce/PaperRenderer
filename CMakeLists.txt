cmake_minimum_required(VERSION "3.25.1")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
project(PaperRenderer)

set(source_dir "${PROJECT_SOURCE_DIR}/src")

file(GLOB_RECURSE main_s ${source_dir}/*.cpp)
file(GLOB_RECURSE main_h ${source_dir}/*.h)

add_library(${PROJECT_NAME} STATIC ${main_s} ${main_h})

message("PaperRenderer")

#VULKAN LIBRARY
find_package(Vulkan REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan)

#GLFW LIBRARY
add_subdirectory("lib/glfw")
target_link_libraries(${PROJECT_NAME} PUBLIC glfw)
target_include_directories(glfw PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/lib/GLFW/include>$<INSTALL_INTERFACE:lib/GLFW/include>")

#GLM
add_subdirectory("lib/glm")
target_link_libraries(${PROJECT_NAME} PUBLIC glm)
target_include_directories(glm INTERFACE "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/lib/glm>$<INSTALL_INTERFACE:lib/glm>")

#VOLK
add_subdirectory("lib/volk")
target_link_libraries(${PROJECT_NAME} PUBLIC volk)
target_include_directories(volk PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/lib/volk>$<INSTALL_INTERFACE:lib/volk>")

#VMA
