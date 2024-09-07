TMOUT=300

#----------C++ BUILD----------#

#build folder
mkdir ./build
cd build



#init cmake
cmake --build ../ --config Debug -j

#run makefile
make -j14 -d

cd ../

#----------RESOURCES----------#

#resources folder
mkdir ./build/resources

#shaders
mkdir ./build/resources/shaders
glslangValidator -V -g --target-env vulkan1.3 "./lib/PaperRenderer/src/PaperRenderer/Shaders/IndirectDrawBuild.comp" -o "build/resources/shaders/IndirectDrawBuild.spv"
glslangValidator -V -g --target-env vulkan1.3 "./lib/PaperRenderer/src/PaperRenderer/Shaders/TLASInstBuild.comp" -o "build/resources/shaders/TLASInstBuild.spv"

glslangValidator -V -g --target-env vulkan1.3 "./src/Game/Resources/Shaders/Compute/Tonemap.comp" -o "build/resources/shaders/Tonemap.spv"

glslangValidator -V -g --target-env vulkan1.3 "./lib/PaperRenderer/src/PaperRenderer/Shaders/Default.vert" -o "build/resources/shaders/Default_vert.spv"
glslangValidator -V -g --target-env vulkan1.3 "./lib/PaperRenderer/src/PaperRenderer/Shaders/Default.frag" -o "build/resources/shaders/Default_frag.spv"
glslangValidator -V -g --target-env vulkan1.3 "./src/Game/Resources/Shaders/DefaultVertex.vert" -o "build/resources/shaders/DefaultVertex.spv"
glslangValidator -V -g --target-env vulkan1.3 "./src/Game/Resources/Shaders/Prepass.frag" -o "build/resources/shaders/Prepass.spv"
glslangValidator -V -g --target-env vulkan1.3 "./src/Game/Resources/Shaders/Prepass.frag" -o "build/resources/shaders/Prepass.spv"
glslangValidator -V -g --target-env vulkan1.3 "./src/Game/Resources/Shaders/TexturelessPBR.frag" -o "build/resources/shaders/TexturelessPBR.spv"
glslangValidator -V -g --target-env vulkan1.3 "./src/Game/Resources/Shaders/TwoSidedFoliage.frag" -o "build/resources/shaders/TwoSidedFoliage.spv"
glslangValidator -V -g --target-env vulkan1.3 "./src/Game/Resources/Shaders/Quad.vert" -o "build/resources/shaders/Quad.spv"
glslangValidator -V -g --target-env vulkan1.3 "./src/Game/Resources/Shaders/BufferCopy.frag" -o "build/resources/shaders/BufferCopy.spv"
glslangValidator -V -g --target-env vulkan1.3 "./src/Game/Resources/Shaders/RT/raytrace.rchit" -o "build/resources/shaders/RTChit.spv"
glslangValidator -V -g --target-env vulkan1.3 "./src/Game/Resources/Shaders/RT/raytrace.rgen" -o "build/resources/shaders/RTRayGen.spv"
glslangValidator -V -g --target-env vulkan1.3 "./src/Game/Resources/Shaders/RT/raytrace.rmiss" -o "build/resources/shaders/RTMiss.spv"
glslangValidator -V -g --target-env vulkan1.3 "./src/Game/Resources/Shaders/RT/raytraceShadow.rmiss" -o "build/resources/shaders/RTShadow.spv"