# PaperRenderer Example
![PaperRenderer Example Image](PaperRendererExampleImage.png)

This is an example that uses many of the features of PaperRenderer, including but not limited to:

- Model creation
- Model instance creation
- Material creation
- Material instance creation
- A Top Level Acceleration Structure (TLAS)
- Raster and Ray Tracing render passes
- Assigning model instances to Raster and Ray Tracing render passes, including their materials (per slot for raster)
- Custom image for a HDR rendering buffer
- A tonemapping pipeline built around a custom render pass that copies the HDR buffer to the current swapchain image
- Custom buffers for UBOs and storage buffers, including a point light buffer

To build the example, make sure the CMAKE PAPER_RENDERER_BUILD_EXAMPLE option is set, this will build the example. Remember to also invoke the ShaderCompile.py file in this directory, as well as the PaperRenderer directory (2 total). These shader compile files output .spv files into /build/resources/shaders from the project directory. Transfer the files from there and /example/resources/models as the hierarchy below goes

```markdown-tree
WORKING DIRECTORY
    resources
        shaders
            BufferCopy.spv
            Default_frag.spv
            Default_vert.spv
            IndirectDrawBuild.spv
            Quad.spv
            raytrace_chit.spv
            raytrace_rgen.spv
            raytrace_rmiss.spv
            raytraceShadow_rmiss.spv
            TLASInstBuild.spv
        models
            PaperRendererExample.glb

```

I will admit that this could be done better, but I'd probably give myself an aneurysm before I figure out how to copy all these files into the cmake output directory.

These are the main features that I decided were good abstractions for a renderer and therefore created. Overall, they create a good foundation for developing a mesh rendering application, especially one using ray tracing. While this example is mostly complete, it shouldn't necessarily be considered to as the overall cleanliness of it will improve, and more features will be showcased as I develop them. With that being said, it can still be used as a reference on how to generally develop around this renderer I created.

I am open to any suggestions on improving the example, especially since I understand it can be a little hard to look at and is at ~1500 lines