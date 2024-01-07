# PaperRenderer
Lightweight, mostly bindless vulkan renderer. Doesn't compile by itself, intended to be used as part of a larger project.

Objects can be added to the renderer by getting a reference to a model already loaded by the renderer, filling in material data, and then uploading the reference to that instance to the renderer. Removing the instance just requires calling the remove function from the renderer, with the object being the parameter to remove. Objects referenced in the renderer are automatically "batched" based on their pipeline and material to reduce bindings throughout the frame.
