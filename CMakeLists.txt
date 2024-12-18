cmake_minimum_required(VERSION "3.25")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

#options
option(PAPER_RENDERER_BUILD_EXAMPLE "Build example/test" ON)

project(PaperRenderer)

set(source_dir "${PROJECT_SOURCE_DIR}/src")

file(GLOB_RECURSE main_s ${source_dir}/*.cpp)
file(GLOB_RECURSE main_h ${source_dir}/*.h)

#library
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

#VMA
message("Getting VMA")
set(VMA_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/lib/vma/include/")
target_include_directories(${PROJECT_NAME} PUBLIC "$<BUILD_INTERFACE:${VMA_INCLUDE_DIR}>$<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/vma>")

#GLM
message("Getting GLM package")
set(GLM_INCUDE_DIR "${PROJECT_SOURCE_DIR}/lib/glm/glm/")
target_include_directories(${PROJECT_NAME} PUBLIC "$<BUILD_INTERFACE:${GLM_INCUDE_DIR}>$<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/glm>")

#GLFW LIBRARY
message("GLFW")
set(GLFW_INCUDE_DIR "${PROJECT_SOURCE_DIR}/lib/glfw/include")
set(GLFW_SOURCE_DIR "${PROJECT_SOURCE_DIR}/lib/glfw/")
add_subdirectory(${GLFW_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC glfw)
target_include_directories(glfw PUBLIC "$<BUILD_INTERFACE:${GLFW_INCUDE_DIR}>$<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/GLFW>")

#INCLUDES
set(INCLUDE_DIRS "${Vulkan_INCLUDE_DIRS};${VOLK_SOURCE_DIR};${VMA_INCLUDE_DIR};${GLFW_INCUDE_DIR};${GLM_INCUDE_DIR}")
include_directories(${INCLUDE_DIRS})

add_dependencies(${PROJECT_NAME} glfw)

#SHADER COMPILE
#[===[
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/resources/shaders/)
add_custom_target(
    paper_shader_compile_target ALL
    COMMENT "Compiling core shaders"
)

add_custom_command(
    TARGET paper_shader_compile_target
    POST_BUILD
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/ShaderCompile.py ${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders/ ${PROJECT_BINARY_DIR}/resources/shaders/
)

]===]

#EXAMPLE
if(PAPER_RENDERER_BUILD_EXAMPLE)
    message("Paper Renderer Example")
    add_subdirectory(example)
endif()

