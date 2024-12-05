cmake_minimum_required(VERSION "3.25")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

project(PaperRendererExample)

set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install")
set(CMAKE_BINARY_DIR "${CMAKE_BINARY_DIR}/example")

set(gltf_source_dir ${PROJECT_SOURCE_DIR}/lib/tinygltf/)

add_executable(${PROJECT_NAME} main.cpp ${gltf_source_dir}json.hpp ${gltf_source_dir}stb_image.h ${gltf_source_dir}stb_image_write.h ${gltf_source_dir}tiny_gltf.h)
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(${PROJECT_NAME} PUBLIC PaperRenderer)
