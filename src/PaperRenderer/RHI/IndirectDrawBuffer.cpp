#include "IndirectDrawBuffer.h"
#include "../Model.h"

namespace PaperRenderer
{
    IndirectDrawContainer::IndirectDrawContainer(Device *device, DescriptorAllocator* descriptor, RasterPipeline const* pipeline)
        :devicePtr(device),
        descriptorsPtr(descriptor),
        pipelinePtr(pipeline)
    {
    }

    IndirectDrawContainer::~IndirectDrawContainer()
    {
    }

    void IndirectDrawContainer::addElement(DrawBufferObject &object)
    {
        object.selfIndex = drawCallTree[object.parentMesh].size();
        drawCallTree[object.parentMesh].push_back(&object);
    }

    void IndirectDrawContainer::removeElement(DrawBufferObject &object)
    {
        //replace object index with the last element in the vector, change last elements self index to match, remove last element with pop_back()
        

        if(drawCallTree.at(object.parentMesh).size() > 1)
        {
            drawCallTree.at(object.parentMesh).at(object.selfIndex) = drawCallTree.at(object.parentMesh).back();
            drawCallTree.at(object.parentMesh).at(object.selfIndex)->selfIndex = object.selfIndex;
            drawCallTree.at(object.parentMesh).pop_back();
        }
        else
        {
            drawCallTree.at(object.parentMesh).clear();
        }
        object.selfIndex = UINT64_MAX;
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

    void IndirectDrawContainer::draw(const VkCommandBuffer& cmdBuffer, const IndirectRenderingData& renderData, uint32_t currentFrame)
    {
        uint32_t drawCountIndex = 0;
        for(auto& [mesh, drawBufferObjects] : drawCallTree)
        {
            //get new descriptor set
            VkDescriptorSet objDescriptorSet = descriptorsPtr->allocateDescriptorSet(pipelinePtr->getDescriptorSetLayouts().at(DescriptorScopes::RASTER_OBJECT), currentFrame);
            
            //write uniforms
            VkDescriptorBufferInfo descriptorInfo = {};
            descriptorInfo.buffer = renderData.bufferData->getBuffer();
            descriptorInfo.offset = outputObjectsLocations.at(drawCountIndex);
            descriptorInfo.range = sizeof(ShaderOutputObject) * drawBufferObjects.size();
            BuffersDescriptorWrites write = {};
            write.binding = 0;
            write.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.infos.push_back(descriptorInfo);

            DescriptorWrites descriptorWritesInfo = {};
            descriptorWritesInfo.bufferWrites = { write };
            DescriptorAllocator::writeUniforms(devicePtr->getDevice(), objDescriptorSet, descriptorWritesInfo);

            //bind set
            DescriptorBind bindingInfo = {};
            bindingInfo.descriptorScope = DescriptorScopes::RASTER_OBJECT;
            bindingInfo.set = objDescriptorSet;
            bindingInfo.layout = pipelinePtr->getLayout();
            bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            DescriptorAllocator::bindSet(devicePtr->getDevice(), cmdBuffer, bindingInfo);

            //bind vbo and ibo and send draw calls (draw calls should be computed in the performCulling() function)
            (*(drawBufferObjects.begin()))->parentModel->bindBuffers(cmdBuffer);
            vkCmdDrawIndexedIndirectCount(
                cmdBuffer,
                renderData.bufferData->getBuffer(),
                drawCommandsLocations.at(drawCountIndex),
                renderData.bufferData->getBuffer(),
                drawCountsLocation + drawCountIndex * sizeof(uint32_t),
                drawBufferObjects.size(),
                sizeof(ShaderDrawCommand));

            //increment draw count index
            drawCountIndex++;
        }
    }
}