#include "IndirectDraw.h"
#include "PaperRenderer.h"

#include <algorithm>

namespace PaperRenderer
{
    CommonMeshGroup::CommonMeshGroup(RenderEngine& renderer, const RenderPass& renderPass, const Material& material)
        :descriptorSet(renderer, renderer.getDefaultDescriptorSetLayout(INDIRECT_DRAW_MATRICES)),
        renderer(renderer),
        renderPass(renderPass),
        material(material)
    {
    }

    CommonMeshGroup::~CommonMeshGroup()
    {
        //destroy buffers
        modelMatricesBuffer.reset();
        drawCommandsBuffer.reset();
    }

    std::vector<ModelInstance*> CommonMeshGroup::verifyBufferSize()
    {
        //verify
        std::vector<ModelInstance*> returnInstances;
        if(rebuild)
        {
            returnInstances = rebuildBuffer();
        }
        
        rebuild = false;
        return returnInstances;
    }

    std::vector<ModelInstance*> CommonMeshGroup::rebuildBuffer()
    {
        //Timer
        Timer timer(renderer, "Rebuild Common Mesh Group Buffers", IRREGULAR);

        //get new size
        BufferSizeRequirements bufferSizeRequirements = getBuffersRequirements();

        //rebuild buffers
        const BufferInfo matricesBufferInfo = {
            .size = bufferSizeRequirements.matricesCount * sizeof(ShaderOutputObject),
            .usageFlags = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR,
            .allocationFlags = 0
        };
        modelMatricesBuffer = std::make_unique<Buffer>(renderer, matricesBufferInfo);

        const BufferInfo drawCommandsBufferInfo = {
            .size = bufferSizeRequirements.drawCommandCount * sizeof(DrawCommand),
            .usageFlags = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | 
                VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR,
            .allocationFlags = 0
        };
        drawCommandsBuffer = std::make_unique<Buffer>(renderer, drawCommandsBufferInfo);

        //queue transfer of draw command data
        setDrawCommandData();

        //update descriptors
        descriptorSet.updateDescriptorSet({
            .bufferWrites = {
                {
                    .infos = { {
                        .buffer = modelMatricesBuffer->getBuffer(),
                        .offset = 0,
                        .range = VK_WHOLE_SIZE
                    } },
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .binding = 0,
                }
            }
        });

        //get instances to update
        std::vector<ModelInstance*> modifiedInstances;
        modifiedInstances.reserve(instanceMeshes.size());
        for(auto& [instance, meshes] : instanceMeshes)
        {
            modifiedInstances.push_back(instance);
        }

        return modifiedInstances;
    }

    CommonMeshGroup::BufferSizeRequirements CommonMeshGroup::getBuffersRequirements()
    {
        BufferSizeRequirements sizeRequirements = {};

        //model matrices, draw commands, and offsets/indices
        uint32_t meshIndex = 0;
        for(auto& [instance, meshesData] : instanceMeshesData)
        {
            for(auto& [mesh, meshInstancesData] : meshesData)
            {
                //get new instance count
                const uint32_t instanceCount = std::max((uint32_t)(meshInstancesData.instanceCount - 1) * 2, (uint32_t)1);

                //set mesh data
                meshInstancesData.drawCommandIndex = meshIndex;
                meshInstancesData.lastRebuildInstanceCount = instanceCount;
                meshInstancesData.matricesStartIndex = sizeRequirements.matricesCount;

                //increment size requirements
                sizeRequirements.matricesCount += instanceCount;
                sizeRequirements.drawCommandCount++;

                //increment mesh counter
                meshIndex++;
            }
        }

        return sizeRequirements;
    }

    void CommonMeshGroup::setDrawCommandData() const
    {
        for(auto& [instance, meshesData] : instanceMeshesData)
        {
            for(const auto& [mesh, meshInstancesData] : meshesData)
            {
                //get command data
                const DrawCommand command = {
                    .command = {
                        .indexCount = mesh->indicesSize / mesh->indexStride,
                        .instanceCount = 0,
                        .firstIndex = 0,
                        .vertexOffset = 0,
                        .firstInstance = meshInstancesData.matricesStartIndex
                    }
                };

                //queue data transfer
                renderer.getStagingBuffer().queueDataTransfers(
                    *drawCommandsBuffer,
                    sizeof(DrawCommand) * meshInstancesData.drawCommandIndex,
                    command
                );
            }
        }
    }

    void CommonMeshGroup::addInstanceMesh(ModelInstance& instance, const LODMesh& instanceMeshData)
    {
        //use instance pointer if using unique geometry; otherwise NULL index
        ModelInstance const* instancePtr = instance.uniqueGeometryData.isUsed ? &instance : NULL;

        if(!instanceMeshesData[instancePtr].count(&instanceMeshData))
        {
            instanceMeshesData[instancePtr][&instanceMeshData].parentModelPtr = &instance.getParentModel();
            rebuild = true;
        }

        instanceMeshesData[instancePtr][&instanceMeshData].instanceCount++;

        if(instanceMeshesData[instancePtr][&instanceMeshData].instanceCount > instanceMeshesData[instancePtr][&instanceMeshData].lastRebuildInstanceCount) rebuild = true;
        
        //add instance mesh references
        instanceMeshes[&instance].push_back(&instanceMeshData);
    }

    void CommonMeshGroup::removeInstanceMeshes(ModelInstance& instance)
    {
        //use instance pointer if using unique geometry; otherwise NULL index
        ModelInstance const* instancePtr = instance.uniqueGeometryData.isUsed ? &instance : NULL;

        if(instanceMeshes.count(&instance))
        {
            for(LODMesh const* meshData : instanceMeshes[&instance])
            {
                instanceMeshesData[instancePtr][meshData].instanceCount--;

                //remove if 0 instances
                if(instanceMeshesData[instancePtr][meshData].instanceCount < 1)
                {
                    instanceMeshesData[instancePtr].erase(meshData);
                }
            }

            //remove instance mesh references
            instanceMeshes.erase(&instance);
        }
    }

    void CommonMeshGroup::draw(const VkCommandBuffer &cmdBuffer) const
    {
        //bind matrices descriptor if used
        
        if(material.getDrawMatricesDescriptorIndex() != 0xFFFFFFFF)
        {
            const DescriptorBinding binding = {
                .bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .pipelineLayout = material.getRasterPipeline().getLayout(),
                .descriptorSetIndex = material.getDrawMatricesDescriptorIndex(),
                .dynamicOffsets = {}
            };
            descriptorSet.bindDescriptorSet(cmdBuffer, binding);
        }

        //submit draw calls
        for(const auto& [instance, meshesData] : instanceMeshesData)
        {
            for(const auto& [mesh, meshData] : meshesData)
            {
                //bind vbo and ibo
                const VkDeviceSize offsets[1] = { mesh->vboOffset };
                if(instance && instance->getUniqueGeometryData().uniqueVBO)
                {
                    vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &instance->getUniqueGeometryData().uniqueVBO->getBuffer(), offsets);
                }
                else
                {
                    vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &meshData.parentModelPtr->getVBO().getBuffer(), offsets);
                }
                vkCmdBindIndexBuffer(cmdBuffer, meshData.parentModelPtr->getIBO().getBuffer(), mesh->iboOffset, mesh->indexType);

                //draw
                vkCmdDrawIndexedIndirect(
                    cmdBuffer,
                    drawCommandsBuffer->getBuffer(),
                    meshData.drawCommandIndex * sizeof(DrawCommand),
                    1,
                    sizeof(DrawCommand)
                );
            }
        }
    }

    void CommonMeshGroup::clearDrawCommand(const VkCommandBuffer &cmdBuffer) const
    {
        //clear instance count
        const uint32_t drawCountDefaultValue = 0;
        for(const auto& [instance, meshesData] : instanceMeshesData)
        {
            for(const auto& [mesh, meshData] : meshesData)
            {
                //location
                const uint32_t drawCommandLocation = (sizeof(DrawCommand) * meshData.drawCommandIndex);
                
                //zero out instance count
                vkCmdFillBuffer(
                    cmdBuffer,
                    drawCommandsBuffer->getBuffer(),
                    drawCommandLocation + offsetof(VkDrawIndexedIndirectCommand, instanceCount),
                    sizeof(VkDrawIndexedIndirectCommand::instanceCount),
                    drawCountDefaultValue
                );  
            }
        }
        
        //memory barrier
        const VkBufferMemoryBarrier2 postMemBarrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = drawCommandsBuffer->getBuffer(),
            .offset = 0,
            .size = VK_WHOLE_SIZE
        };

        const VkDependencyInfo postDependency = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = NULL,
            .bufferMemoryBarrierCount = 1,
            .pBufferMemoryBarriers = &postMemBarrier
        };

        vkCmdPipelineBarrier2(cmdBuffer, &postDependency);
    }

    void CommonMeshGroup::addOwner(Queue& queue)
    {
        modelMatricesBuffer->addOwner(queue);
        drawCommandsBuffer->addOwner(queue);
    }
}