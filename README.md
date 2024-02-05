# PaperRenderer
Lightweight forward vulkan renderer. Doesn't compile by itself, intended to be used as part of a larger project.

Objects can be added to the renderer by getting a reference to a model already loaded by the renderer, filling in material data, and then uploading the reference to that instance to the renderer. Removing the instance just requires calling the remove function from the renderer, with the object being the parameter to remove. Objects referenced in the renderer are automatically "batched" based on their pipeline and material to reduce bindings throughout the frame.

Materials can be specified by creating a class for the material and inheriting the material base class, where the material can then be added onto an object instance where the material will automatically be referenced into the renderer. Material shaders must be coded into a glsl file, and its uniforms must also be specified into the material class to be created.

This is still my own "educational" work in progress, though I plan to add functionality for raytraced shadows and reflections (the acceleration structure is already made), hopefully even RT global illumination, though none of this at the cost of rasterization quality.
