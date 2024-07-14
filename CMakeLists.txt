cmake_minimum_required(VERSION "3.25")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
project(PaperRenderer)

set(source_dir "${PROJECT_SOURCE_DIR}/src")

file(GLOB_RECURSE main_s ${source_dir}/*.cpp)
file(GLOB_RECURSE main_h ${source_dir}/*.h)

add_library(${PROJECT_NAME} STATIC ${main_s} ${main_h})

#external project
include(ExternalProject)
set(DEPENDENCY_DIR "${CMAKE_BINARY_DIR}/PaperRendererDependencies")

#VULKAN
include(FindVulkan)
target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan)
target_link_libraries(${PROJECT_NAME} PRIVATE volk)
message("-- Vulkan Version: ${Vulkan_VERSION}")

#GLFW LIBRARY
set(GLFW_GIT_DIR "https://github.com/glfw/glfw.git")
set(GLFW_SOURCE_DIR "${DEPENDENCY_DIR}/GLFW/")
set(GLFW_INCUDE_DIR "${DEPENDENCY_DIR}/GLFW/include")
set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_DOCS OFF)
set(GLFW_INSTALL OFF)
message("-- Adding glfw at ${GLFW_GIT_DIR}")
ExternalProject_Add(
    glfw
    GIT_REPOSITORY ${GLFW_GIT_DIR}
    SOURCE_DIR ${GLFW_SOURCE_DIR}
    CMAKE_ARGS
		  -DCMAKE_INSTALL_PREFIX:PATH=install
)

#GLM
set(GLM_GIT_DIR "https://github.com/g-truc/glm.git")
set(GLM_SOURCE_DIR "${DEPENDENCY_DIR}/GLM/glm")
set(GLM_INCUDE_DIR ${DEPENDENCY_DIR}/GLM/glm)
message("-- Adding glm at ${GLM_GIT_DIR}")
ExternalProject_Add(
    glm
    GIT_REPOSITORY ${GLM_GIT_DIR}
    SOURCE_DIR ${GLM_SOURCE_DIR}
    CMAKE_ARGS
		  -DCMAKE_INSTALL_PREFIX:PATH=install
)

add_dependencies(${PROJECT_NAME} glfw glm)

file(MAKE_DIRECTORY ${VULKAN_INCUDE_DIR})
file(MAKE_DIRECTORY ${GLFW_INCUDE_DIR})
file(MAKE_DIRECTORY ${GLM_INCUDE_DIR})

set(INCLUDE_DIRS "${Vulkan_INCLUDE_DIRS};${GLFW_INCUDE_DIR};${GLM_INCUDE_DIR}")
include_directories(${INCLUDE_DIRS})

