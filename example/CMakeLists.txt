cmake_minimum_required(VERSION "3.25")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

project(PaperRendererExample)

set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install")
set(CMAKE_BINARY_DIR "${CMAKE_BINARY_DIR}/example")

#imgui
set(imgui_dir ${PROJECT_SOURCE_DIR}/lib/imgui)
include_directories(${imgui_dir} ${imgui_dir}/backends)
set(imgui_source ${imgui_dir}/backends/imgui_impl_glfw.cpp ${imgui_dir}/backends/imgui_impl_vulkan.cpp ${imgui_dir}/imgui.cpp ${imgui_dir}/imgui_draw.cpp ${imgui_dir}/imgui_demo.cpp ${imgui_dir}/imgui_tables.cpp ${imgui_dir}/imgui_widgets.cpp)
add_compile_definitions(IMGUI_IMPL_VULKAN_USE_VOLK)

#glTF
set(gltf_source_dir ${PROJECT_SOURCE_DIR}/lib/tinygltf/)

#this
file(GLOB_RECURSE example_sources_s ${PROJECT_SOURCE_DIR}/src/*.cpp)
file(GLOB_RECURSE example_sources_h ${PROJECT_SOURCE_DIR}/src/*.h)

include_directories(${PROJECT_SOURCE_DIR})

add_executable(${PROJECT_NAME} ${example_sources_s} ${example_sources_h} ${gltf_source_dir}json.hpp ${gltf_source_dir}stb_image.h ${gltf_source_dir}stb_image_write.h ${gltf_source_dir}tiny_gltf.h ${imgui_source})
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(${PROJECT_NAME} PUBLIC PaperRenderer)
add_dependencies(${PROJECT_NAME} PaperRenderer)

#COMPILE SHADERS
add_custom_target(
    example_shader_compile_target ALL
    COMMENT "Compiling example shaders"
)

add_custom_command(
    TARGET example_shader_compile_target
    POST_BUILD
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/ShaderCompile.py ${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders/ ${CMAKE_CURRENT_BINARY_DIR}/resources/shaders/
)

#COPY FILES
message(STATUS "Copying resource files into ${PROJECT_BINARY_DIR}/resources")
file(COPY ${PROJECT_SOURCE_DIR}/resources/models DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/resources) #models
