#script to compile shaders from raw glsl code to SPIR-V code
./Bin/glslc -O "res/shaders/triangle.vert" -o "build/resources/shaders/vert.spv"
./Bin/glslc -O "res/shaders/triangle.frag" -o "build/resources/shaders/frag.spv"