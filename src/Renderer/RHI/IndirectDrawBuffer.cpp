#include "IndirectDrawBuffer.h"

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
        if(drawCallTree.count(object.mesh) == 0)
        {
            drawCallTree[object.mesh].cullingInputData.resize(commandsPtr->getFrameCount());

            for(uint32_t i = 0; i < commandsPtr->getFrameCount(); i++)
            {
                drawCallTree[object.mesh].cullingInputData.at(i) = std::make_shared<UniformBuffer>(devicePtr, commandsPtr, sizeof(CullingInputData));
            }
        }
        drawCallTree[object.mesh].objects.push_back(&object);
        object.reference = drawCallTree.at(object.mesh).objects.end();
        object.reference--;
    }

    void IndirectDrawContainer::removeElement(DrawBufferObject &object)
    {
        drawCallTree.at(object.mesh).objects.erase(object.reference);
        object.reference = std::list<Renderer::DrawBufferObject*>::iterator();
    }

    uint32_t IndirectDrawContainer::getOutputObjectSize(uint32_t currentBufferSize)
    {
        outputObjectsLocations.clear();
        uint32_t returnSize = 0;
        for(auto& [mesh, node] : drawCallTree)
        {
            outputObjectsLocations.push_back(currentBufferSize + returnSize);
            for(auto object = node.objects.begin(); object != node.objects.end(); object++)
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
        for(auto& [mesh, node] : drawCallTree)
        {
            drawCommandsLocations.push_back(currentBufferSize + returnSize);
            for(auto object = node.objects.begin(); object != node.objects.end(); object++)
            {
                returnSize += sizeof(ShaderDrawCommand);
            }
        }
        return returnSize;
    }

    std::vector<ShaderInputObject> IndirectDrawContainer::getInputObjects(uint32_t currentBufferSize)
    {
        inputObjectsLocations.clear();
        std::vector<ShaderInputObject> returnData;
        for(auto& [mesh, node] : drawCallTree) //per vertex/index buffer instance (mesh) group culling
        {
            inputObjectsLocations.push_back(currentBufferSize + returnData.size() * sizeof(ShaderInputObject)); 

            //add necessary data
            uint32_t i = 0;
            for(auto object = node.objects.begin(); object != node.objects.end(); object++)
            {
                //objects
                ShaderInputObject inputObject = {
                    .modelMatrix = *(*object)->modelMatrix,
                    .position = glm::vec4(*(*object)->position, (*object)->mesh->getSphericalBounding()) //w component of shader position is the spherical bounds;
                };

                returnData.push_back(inputObject);
            }
        }
        return returnData;
    }

    uint32_t IndirectDrawContainer::getDrawCountsSize(uint32_t currentBufferSize)
    {
        this->drawCountsLocation = currentBufferSize;
        return drawCallTree.size() * sizeof(uint32_t);
    }

    void IndirectDrawContainer::dispatchCulling(const VkCommandBuffer& cmdBuffer, ComputePipeline const* cullingPipeline, const CullingFrustum& frustum, StorageBuffer const* buffer, glm::mat4 projection, glm::mat4 view, uint32_t currentFrame)
    {
        VkDescriptorSet set0Descriptor = descriptorsPtr->allocateDescriptorSet(cullingPipeline->getDescriptorSetLayouts().at(0), currentFrame);

        //set0 - binding 0
        descriptorsPtr->writeUniform(
            buffer->getBuffer(),
            sizeof(uint32_t) * drawCallTree.size(),
            drawCountsLocation,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            set0Descriptor);

        vkCmdBindDescriptorSets(cmdBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            cullingPipeline->getLayout(),
            0, //bind point
            1,
            &set0Descriptor,
            0,
            0);

        uint32_t drawCountIndex = 0;
        for(auto& [mesh, node] : drawCallTree) //per vertex/index buffer instance (mesh) group culling
        {
            //update uniform buffer
            CullingInputData inputData = {
                .projection = projection,
                .view = view,
                .frustumData = frustum,
                .matrixCount = (uint32_t)node.objects.size(),
                .drawCountIndex = drawCountIndex,
                .indexCount = mesh->getIndexBuffer().getLength()
            };
            
            node.cullingInputData.at(currentFrame)->updateUniformBuffer(&inputData, sizeof(CullingInputData));

            //set1
            VkDescriptorSet set1Descriptor = descriptorsPtr->allocateDescriptorSet(cullingPipeline->getDescriptorSetLayouts().at(1), currentFrame);

            //set1 - binding 0: input data
            descriptorsPtr->writeUniform(
                node.cullingInputData.at(currentFrame)->getBuffer(),
                sizeof(CullingInputData),
                0,
                0,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                set1Descriptor);

            //set1 - binding 1: input objects
            descriptorsPtr->writeUniform(
                buffer->getBuffer(),
                sizeof(ShaderInputObject) * node.objects.size(),
                inputObjectsLocations.at(drawCountIndex),
                1,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                set1Descriptor);

            //set1 - binding 2: output objects
            descriptorsPtr->writeUniform(
                buffer->getBuffer(),
                sizeof(ShaderOutputObject) * node.objects.size(),
                outputObjectsLocations.at(drawCountIndex),
                2,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                set1Descriptor);

            //set1 - binding 3: output draw commands
            descriptorsPtr->writeUniform(
                buffer->getBuffer(),
                sizeof(ShaderDrawCommand) * node.objects.size(),
                drawCommandsLocations.at(drawCountIndex),
                3,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                set1Descriptor);

            vkCmdBindDescriptorSets(cmdBuffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                cullingPipeline->getLayout(),
                1, //bind point
                1,
                &set1Descriptor,
                0,
                0);

            int groupcount = ((node.objects.size()) / 64) + 1;
            vkCmdDispatch(cmdBuffer, groupcount, 1, 1);

            drawCountIndex++;
        }
    }

    void IndirectDrawContainer::draw(const VkCommandBuffer& cmdBuffer, IndirectRenderingData const* renderData, uint32_t currentFrame)
    {
        uint32_t drawCountIndex = 0;
        for(auto& [mesh, node] : drawCallTree)
        {
            VkDescriptorSet objDescriptorSet = descriptorsPtr->allocateDescriptorSet(pipelinePtr->getDescriptorSetLayouts().at(2), currentFrame);
            
            descriptorsPtr->writeUniform(
                renderData->bufferData->getBuffer(),
                sizeof(ShaderOutputObject) * node.objects.size(),
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
            VkDeviceSize offset[1] = {0};
            vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &((*(node.objects.begin()))->mesh->getVertexBuffer().getBuffer()), offset);
            vkCmdBindIndexBuffer(cmdBuffer, ((*(node.objects.begin()))->mesh->getIndexBuffer().getBuffer()), 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexedIndirectCount(
                cmdBuffer,
                renderData->bufferData->getBuffer(),
                drawCommandsLocations.at(drawCountIndex),
                renderData->bufferData->getBuffer(),
                drawCountsLocation + drawCountIndex * sizeof(uint32_t),
                node.objects.size(),
                sizeof(ShaderDrawCommand));

            drawCountIndex++;
        }
    }
}