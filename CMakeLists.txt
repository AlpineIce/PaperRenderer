cmake_minimum_required(VERSION "3.25.1")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
project(VulkanRenderer)

#VULKAN LIBRARY
find_package(Vulkan REQUIRED)

set(source_dir "${PROJECT_SOURCE_DIR}/src")
set(stbimage_dir "${PROJECT_SOURCE_DIR}/lib/stb_image")
set(vma_dir "${PROJECT_SOURCE_DIR}/lib/VulkanMemoryAllocator/include")

file(GLOB_RECURSE main_s ${source_dir}/*.cpp)
file(GLOB_RECURSE main_h ${source_dir}/*.h)
file(GLOB_RECURSE stbimage_source_files "${stbimage_dir}/*.h")
file(GLOB_RECURSE stbimage_source_files "${vma_dir}/*.h")

add_executable(${PROJECT_NAME} ${main_s} ${main_h})
target_include_directories(${PROJECT_NAME} PUBLIC "${source_dir}/Engine")

target_link_libraries(VulkanRenderer PRIVATE Vulkan::Vulkan)

#GLFW LIBRARY
add_subdirectory("lib/glfw")
target_link_libraries(${PROJECT_NAME} PUBLIC glfw)
target_include_directories(glfw PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/lib/GLFW/include>$<INSTALL_INTERFACE:lib/GLFW/include>")

#GLM
add_subdirectory("lib/glm")
target_link_libraries(${PROJECT_NAME} PUBLIC glm)
target_include_directories(glm INTERFACE "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/lib/glm>$<INSTALL_INTERFACE:lib/glm>")

#STB IMAGE
target_include_directories(${PROJECT_NAME} PUBLIC "${stbimage_dir}")

#VULKAN MEMORY ALLOCATOR
add_subdirectory("lib/VulkanMemoryAllocator")
target_link_libraries(${PROJECT_NAME} PUBLIC VulkanMemoryAllocator)
target_include_directories(VulkanMemoryAllocator INTERFACE "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/lib/VulkanMemoryAllocator/include>$<INSTALL_INTERFACE:lib/VulkanMemoryAllocator/include>")

#ASSIMP
add_subdirectory("lib/assimp")
target_link_libraries(${PROJECT_NAME} PUBLIC assimp)
target_include_directories(assimp PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/lib/assimp/include>$<INSTALL_INTERFACE:lib/assimp/include>")
