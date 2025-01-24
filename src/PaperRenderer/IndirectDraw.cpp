#include "IndirectDraw.h"
#include "PaperRenderer.h"

#include <algorithm>

namespace PaperRenderer
{
    CommonMeshGroup::CommonMeshGroup(RenderEngine& renderer, const RenderPass& renderPass, const Material& material)
        :renderer(renderer),
        renderPass(renderPass),
        material(material)
    {
        //create object descriptor group if material has the set layout
        for(const auto& [index, layout] : material.getDescriptorGroup().getSetLayouts())
        {
            if(layout == renderer.getDefaultDescriptorSetLayout(INDIRECT_DRAW_MATRICES))
            {
                const std::unordered_map<uint32_t, VkDescriptorSetLayout> layouts = { { index, layout } };
                descriptorGroup = std::make_unique<DescriptorGroup>(renderer, layouts);
                drawDescriptorIndex = index;
                
                break;
            }
        }
    }

    CommonMeshGroup::~CommonMeshGroup()
    {
        //destroy descriptor group
        descriptorGroup.reset();

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
        const DescriptorWrites descriptorWritesInfo = {
            .bufferWrites = { {
                .infos = { {
                    .buffer = modelMatricesBuffer->getBuffer(),
                    .offset = 0,
                    .range = VK_WHOLE_SIZE
                } },
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .binding = 0,
            } }
        };

        descriptorGroup->updateDescriptorSets({ { drawDescriptorIndex, descriptorWritesInfo } });

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
                        .indexCount = mesh->indexCount,
                        .instanceCount = 0,
                        .firstIndex = mesh->iboOffset,
                        .vertexOffset = (int32_t)mesh->vboOffset,
                        .firstInstance = meshInstancesData.matricesStartIndex
                    }
                };

                std::vector<char> data(sizeof(DrawCommand));
                memcpy(data.data(), &command, sizeof(DrawCommand));

                //queue data transfer
                renderer.getStagingBuffer().queueDataTransfers(
                    *drawCommandsBuffer,
                    sizeof(DrawCommand) * meshInstancesData.drawCommandIndex,
                    data
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
        if(descriptorGroup)
        {
            descriptorGroup->bindSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, material.getRasterPipeline().getLayout(), {});
        }

        //submit draw calls
        for(const auto& [instance, meshesData] : instanceMeshesData)
        {
            for(const auto& [mesh, meshData] : meshesData)
            {
                //bind vbo and ibo
                if(instance)
                {
                    instance->bindBuffers(cmdBuffer);
                }
                else
                {
                    meshData.parentModelPtr->bindBuffers(cmdBuffer);
                }

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
        //pre-transfer memory barrier
        const VkBufferMemoryBarrier2 preMemBarrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = drawCommandsBuffer->getBuffer(),
            .offset = 0,
            .size = VK_WHOLE_SIZE
        };

        const VkDependencyInfo preDependency = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = NULL,
            .bufferMemoryBarrierCount = 1,
            .pBufferMemoryBarriers = &preMemBarrier
        };

        vkCmdPipelineBarrier2(cmdBuffer, &preDependency);

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

                //post memory barrier
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
                    .offset = drawCommandLocation + offsetof(VkDrawIndexedIndirectCommand, instanceCount),
                    .size = sizeof(VkDrawIndexedIndirectCommand::instanceCount)
                };

                const VkDependencyInfo postDependency = {
                    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .pNext = NULL,
                    .bufferMemoryBarrierCount = 1,
                    .pBufferMemoryBarriers = &postMemBarrier
                };

                vkCmdPipelineBarrier2(cmdBuffer, &postDependency);
            }
        }
    }

    void CommonMeshGroup::addOwner(const Queue &queue)
    {
        modelMatricesBuffer->addOwner(queue);
        drawCommandsBuffer->addOwner(queue);
    }
}