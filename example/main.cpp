#include "../src/PaperRenderer/PaperRenderer.h"

int main()
{
    //initialize renderer
    PaperRenderer::RendererCreationStruct engineInfo = {
        .shadersDir = "",
        .windowState = {
            .windowName = "Example"
        }
    };

    PaperRenderer::RenderEngine renderer(engineInfo);

    //load models
    
    return 0;
}