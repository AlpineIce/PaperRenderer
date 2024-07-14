cmake_minimum_required(VERSION "3.25")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install")
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
set(VOLK_INCUDE_DIR "${PROJECT_SOURCE_DIR}/lib/volk")
message("-- Vulkan Version: ${Vulkan_VERSION}")

#VOLK
set(VOLK_GIT_DIR "https://github.com/zeux/volk.git")
set(VOLK_SOURCE_DIR "${DEPENDENCY_DIR}/volk/")
message("-- Adding volk at ${VOLK_GIT_DIR}")
ExternalProject_Add(
    volk
    GIT_REPOSITORY ${VOLK_GIT_DIR}
    SOURCE_DIR ${VOLK_SOURCE_DIR}
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env VOLK_STATIC_DEFINES
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
		-DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
)

#GLFW LIBRARY
set(GLFW_GIT_DIR "https://github.com/glfw/glfw.git")
set(GLFW_SOURCE_DIR "${DEPENDENCY_DIR}/GLFW/")
message("-- Adding glfw at ${GLFW_GIT_DIR}")
ExternalProject_Add(
    glfw
    GIT_REPOSITORY ${GLFW_GIT_DIR}
    SOURCE_DIR ${GLFW_SOURCE_DIR}
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
		-DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
)

#GLM
set(GLM_GIT_DIR "https://github.com/g-truc/glm.git")
set(GLM_SOURCE_DIR "${DEPENDENCY_DIR}/GLM/glm")
message("-- Adding glm at ${GLM_GIT_DIR}")
ExternalProject_Add(
    glm
    GIT_REPOSITORY ${GLM_GIT_DIR}
    SOURCE_DIR ${GLM_SOURCE_DIR}
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
		-DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
)

add_dependencies(${PROJECT_NAME} volk glfw glm)
include_directories(${CMAKE_INSTALL_PREFIX})

