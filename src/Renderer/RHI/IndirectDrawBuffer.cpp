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

    std::vector<std::vector<ObjectPreprocessStride>> IndirectDrawContainer::getObjectSizes(uint32_t currentBufferSize)
    {
        std::vector<std::vector<ObjectPreprocessStride>> returnData;
        uint32_t lastSize = 0;
        for(auto& [mesh, node] : drawCallTree) //per vertex/index buffer instance (mesh) group culling
        {
            objectGroupLocations.push_back(currentBufferSize + lastSize);
            //storage buffer
            std::vector<ObjectPreprocessStride> bufferData(node.objects.size());

            //add necessary data
            uint32_t i = 0;
            for(auto object = node.objects.begin(); object != node.objects.end(); object++) //todo get rid of this shi
            {
                //objects
                bufferData.at(i).inputObject = {
                    .modelMatrix = *(*object)->modelMatrix,
                    .position = glm::vec4(*(*object)->position, (*object)->mesh->getSphericalBounding()) //w component of shader position is the spherical bounds;
                };

                //draw commands
                ShaderDrawCommand command = {};
                command.command.indexCount = (*object)->mesh->getIndexBuffer().getLength();
                command.command.instanceCount = 1; //TODO INSTANCING
                command.command.firstIndex = 0;
                command.command.vertexOffset = 0;
                command.command.firstInstance = i;

                bufferData.at(i).inputCommand = command;

                i++;
            }

            returnData.push_back(bufferData);
            lastSize = bufferData.size() * sizeof(ObjectPreprocessStride);
        }
        return returnData;
    }

    uint32_t IndirectDrawContainer::getDrawCountsSize(uint32_t currentBufferSize)
    {
        this->drawCountsLocation = currentBufferSize;
        return drawCallTree.size();
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
                .drawCountIndex = drawCountIndex
            };
            node.cullingInputData.at(currentFrame) = std::make_shared<UniformBuffer>(devicePtr, commandsPtr, sizeof(CullingInputData));
            node.cullingInputData.at(currentFrame)->updateUniformBuffer(&inputData, sizeof(CullingInputData));

            //set1
            VkDescriptorSet set1Descriptor = descriptorsPtr->allocateDescriptorSet(cullingPipeline->getDescriptorSetLayouts().at(1), currentFrame);

            //set1 - binding 0
            descriptorsPtr->writeUniform(
                node.cullingInputData.at(currentFrame)->getBuffer(),
                sizeof(CullingInputData),
                0,
                0,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                set1Descriptor);

            //set1 - binding 1
            descriptorsPtr->writeUniform(
                buffer->getBuffer(),
                sizeof(ObjectPreprocessStride) * node.objects.size(),
                objectGroupLocations.at(drawCountIndex),
                1,
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
                sizeof(ObjectPreprocessStride) * node.objects.size(),
                objectGroupLocations.at(drawCountIndex), //idk how tf to get offset to work without a stride
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

            //bind vbo and send draw calls (draw calls should be computed in the performCulling() function)
            VkDeviceSize offset[1] = {0};
            vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &((*(node.objects.begin()))->mesh->getVertexBuffer().getBuffer()), offset);
            vkCmdBindIndexBuffer(cmdBuffer, ((*(node.objects.begin()))->mesh->getIndexBuffer().getBuffer()), 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexedIndirectCount(
                cmdBuffer,
                renderData->bufferData->getBuffer(),
                objectGroupLocations.at(drawCountIndex) + offsetof(ObjectPreprocessStride, inputCommand),
                renderData->bufferData->getBuffer(),
                drawCountsLocation + drawCountIndex * sizeof(uint32_t),
                node.objects.size(),
                sizeof(ObjectPreprocessStride));

            drawCountIndex++;
        }
    }
}