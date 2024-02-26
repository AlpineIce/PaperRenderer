#include "IndirectDrawBuffer.h"
#include "../Model.h"

namespace Renderer
{
    IndirectDrawContainer::IndirectDrawContainer(Device *device, CmdBufferAllocator *commands, DescriptorAllocator* descriptor, RasterPipeline const* pipeline)
        :devicePtr(device),
        commandsPtr(commands),
        descriptorsPtr(descriptor),
        pipelinePtr(pipeline)
    {
    }

    IndirectDrawContainer::~IndirectDrawContainer()
    {
    }

    void IndirectDrawContainer::addElement(DrawBufferObject &object)
    {
        drawCallTree[object.parentMesh].push_back(&object);
        object.reference = drawCallTree.at(object.parentMesh).end();
        object.reference--;
    }

    void IndirectDrawContainer::removeElement(DrawBufferObject &object)
    {
        drawCallTree.at(object.parentMesh).erase(object.reference);
        object.reference = std::list<Renderer::DrawBufferObject*>::iterator();
    }
    
    uint32_t IndirectDrawContainer::getOutputObjectSize(uint32_t currentBufferSize)
    {
        outputObjectsLocations.clear();
        uint32_t returnSize = 0;
        for(auto& [mesh, drawBufferObjects] : drawCallTree)
        {
            mesh->outputObjectsOffset = currentBufferSize + returnSize;
            outputObjectsLocations.push_back(currentBufferSize + returnSize);
            for(auto object = drawBufferObjects.begin(); object != drawBufferObjects.end(); object++)
            {
                returnSize += sizeof(ShaderOutputObject);
            }
        }
        return returnSize;
    }

    uint32_t IndirectDrawContainer::getDrawCommandsSize(uint32_t currentBufferSize)
    {
        drawCommandsLocations.clear();
        uint32_t returnSize = 0;
        for(auto& [mesh, drawBufferObjects] : drawCallTree)
        {
            mesh->drawCommandsOffset = currentBufferSize + returnSize;
            drawCommandsLocations.push_back(currentBufferSize + returnSize);
            for(auto object = drawBufferObjects.begin(); object != drawBufferObjects.end(); object++)
            {
                returnSize += sizeof(ShaderDrawCommand);
            }
        }
        
        return returnSize;
    }

    uint32_t IndirectDrawContainer::getDrawCountsSize(uint32_t currentBufferSize)
    {
        this->drawCountsLocation = currentBufferSize;
        uint32_t meshIndex = 0;
        for(auto& [mesh, drawBufferObjects] : drawCallTree)
        {
            mesh->drawCountsOffset = currentBufferSize + meshIndex * sizeof(uint32_t);
            meshIndex++;
        }
        
        return drawCallTree.size() * sizeof(uint32_t);
    }

    void IndirectDrawContainer::draw(const VkCommandBuffer& cmdBuffer, IndirectRenderingData const* renderData, uint32_t currentFrame)
    {
        uint32_t drawCountIndex = 0;
        for(auto& [mesh, drawBufferObjects] : drawCallTree)
        {
            VkDescriptorSet objDescriptorSet = descriptorsPtr->allocateDescriptorSet(pipelinePtr->getDescriptorSetLayouts().at(2), currentFrame);
            
            descriptorsPtr->writeUniform(
                renderData->bufferData->getBuffer(),
                sizeof(ShaderOutputObject) * drawBufferObjects.size(),
                outputObjectsLocations.at(drawCountIndex),
                0,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                objDescriptorSet);
            
            vkCmdBindDescriptorSets(
                cmdBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelinePtr->getLayout(),
                2, //object bind point
                1,
                &objDescriptorSet,
                0,
                0);

            //bind vbo and ibo and send draw calls (draw calls should be computed in the performCulling() function)
            (*(drawBufferObjects.begin()))->parentModel->bindBuffers(cmdBuffer);

            vkCmdDrawIndexedIndirectCount(
                cmdBuffer,
                renderData->bufferData->getBuffer(),
                drawCommandsLocations.at(drawCountIndex),
                renderData->bufferData->getBuffer(),
                drawCountsLocation + drawCountIndex * sizeof(uint32_t),
                drawBufferObjects.size(),
                sizeof(ShaderDrawCommand));

            drawCountIndex++;
        }
    }
}