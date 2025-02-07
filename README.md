# Paper Renderer
Paper Renderer is a (mostly complete) renderer abstraction which aims to integrate traditonal raster and raytracing together nicely, while deferring much of the work (mainly draw instance call counts and TLAS instances) to compute shaders. Multithreading is also supported for command buffer recording, descriptor set allocation, and queue submission.

#### Some of the features include, but aren't limited to
* Model & Model Instance creation
* Material and Material Instance creation (Material only for ray tracing)
* Raster render passes (includes object draw sorting, on a per instance level)
* Acceleration structure abstractions (BLAS for models, TLAS as a RT render pass input)
* Ray tracing render passes, which act very similar to raster render passes in the context of this renderer
* Assigning model instances to Raster and Ray Tracing render passes, including their materials (per slot for raster, though RT gets to use gl_GeometryIndexEXT as for material slots)
* Custom resources such as buffers and images
* Simple compute shader wrapper
* A bit more stuff that either isn't mentioned because it isn't that impressive or is just incomplete

#### --- Image from the example ---
![Paper Renderer Example Image](example/PaperRendererExampleImage.png)

## Dependencies
* CMake
* Python
* VulkanSDK
* Either Windows or Linux (X11 or Wayland)
## Build Instructions
1. git clone this repo, making sure to --recurse-submodules to gather dependencies (or fill them out manually)
2. Set the CMake option **PAPER_RENDERER_BUILD_EXAMPLE** to be off if you don't want to build the example
3. Run CMake, which will compile the C++ code and shaders, the latter of which gets output into "${PROJECT_BINARY_DIR}/resources/shaders/". If the example is built, it will be put into the example directory within the build directory.

## Documentation
Incomplete documentation is available in the /docs folder as HTML. Refer to that and the example for for setting up your own project integrating Paper Renderer