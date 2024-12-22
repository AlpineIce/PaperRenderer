#include "Materials.h"

//----------RASTER MATERIALS----------//

DefaultMaterial::DefaultMaterial(PaperRenderer::RenderEngine &renderer, const PaperRenderer::RasterPipelineBuildInfo &pipelineInfo, const PaperRenderer::Buffer &lightBuffer, const PaperRenderer::Buffer &lightInfoUBO)
    :PaperRenderer::Material(renderer, pipelineInfo),
    lightBuffer(lightBuffer),
    lightInfoUBO(lightInfoUBO)
{
}

DefaultMaterial::~DefaultMaterial()
{
}
