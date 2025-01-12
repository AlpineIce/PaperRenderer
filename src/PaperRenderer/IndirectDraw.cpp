#include "IndirectDraw.h"
#include "PaperRenderer.h"

#include <algorithm>

namespace PaperRenderer
{
    CommonMeshGroup::CommonMeshGroup(RenderEngine& renderer, RenderPass const* renderPass)
        :renderer(renderer),
        renderPassPtr(renderPass)
    {
    }

    CommonMeshGroup::~CommonMeshGroup()
    {
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

        //get instances to update
        std::vector<ModelInstance*> modifiedInstances;
        for(auto& [instance, meshes] : instanceMeshes)
        {
            modifiedInstances.push_back(instance);
        }

        //remove duplicates
        std::sort(modifiedInstances.begin(), modifiedInstances.end());
        auto uniqueIndices = std::unique(modifiedInstances.begin(), modifiedInstances.end());
        modifiedInstances.erase(uniqueIndices, modifiedInstances.end());

        return modifiedInstances;
    }

    CommonMeshGroup::BufferSizeRequirements CommonMeshGroup::getBuffersRequirements()
    {
        BufferSizeRequirements sizeRequirements = {};

        //draw commands count
        drawCommandCount = meshesData.size();
        sizeRequirements.drawCommandCount = drawCommandCount;

        //model matrices and offsets/indices
        uint32_t meshIndex = 0;
        for(auto& [mesh, meshInstancesData] : meshesData)
        {
            //draw commands count offset
            meshInstancesData.drawCommandIndex = meshIndex;

            //output objects
            const uint32_t instanceCount = std::max((uint32_t)(meshInstancesData.instanceCount * instanceCountOverhead), (uint32_t)8); //minimum of 8 instances to make things happy
            meshInstancesData.lastRebuildInstanceCount = instanceCount;
            meshInstancesData.matricesStartIndex = sizeRequirements.matricesCount;
            sizeRequirements.matricesCount += instanceCount;

            meshIndex++;
        }

        return sizeRequirements;
    }

    void CommonMeshGroup::setDrawCommandData() const
    {
        for(const auto& [mesh, meshInstancesData] : meshesData)
        {
            //get command data
            DrawCommand command = {};
            command.command.indexCount = mesh->indexCount;
            command.command.instanceCount = 0;
            command.command.firstIndex = mesh->iboOffset;
            command.command.vertexOffset = mesh->vboOffset;
            command.command.firstInstance = 0;

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

    void CommonMeshGroup::addInstanceMesh(ModelInstance& instance, const LODMesh& instanceMeshData)
    {        
        if(!meshesData.count(&instanceMeshData))
        {
            meshesData[&instanceMeshData].parentModelPtr = &instance.getParentModel();
            rebuild = true;
        }

        meshesData.at(&instanceMeshData).instanceCount++;

        if(meshesData.at(&instanceMeshData).instanceCount > meshesData.at(&instanceMeshData).lastRebuildInstanceCount) rebuild = true;

        this->instanceMeshes[&instance].push_back(&instanceMeshData);
    }

    void CommonMeshGroup::removeInstanceMeshes(ModelInstance& instance)
    {        
        if(instanceMeshes.count(&instance))
        {
            for(LODMesh const* meshData : instanceMeshes.at(&instance))
            {
                meshesData.at(meshData).instanceCount--;

                //remove if 0 instances
                if(meshesData.at(meshData).instanceCount < 1)
                {
                    meshesData.erase(meshData);
                }
            }
            instanceMeshes.erase(&instance);
        }
    }

    void CommonMeshGroup::draw(const VkCommandBuffer &cmdBuffer, const Material& material) const
    {
        for(const auto& [mesh, meshData] : meshesData)
        {
            if(meshData.parentModelPtr)//null safety
            {
                //assign object descriptor if used
                if(material.usesDefaultDescriptors())
                {
                    //get new descriptor set
                    VkDescriptorSet objDescriptorSet = renderer.getDescriptorAllocator().allocateDescriptorSet(material.getRasterPipeline().getDescriptorSetLayouts().at(material.getRasterPipeline().getDrawDescriptorIndex()));
                    
                    //write uniforms
                    VkDescriptorBufferInfo descriptorInfo = {};
                    descriptorInfo.buffer = modelMatricesBuffer->getBuffer();
                    descriptorInfo.offset = meshData.matricesStartIndex * sizeof(ShaderOutputObject);
                    descriptorInfo.range = sizeof(ShaderOutputObject) * meshData.instanceCount;

                    BuffersDescriptorWrites write = {};
                    write.binding = 0;
                    write.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    write.infos.push_back(descriptorInfo);

                    DescriptorWrites descriptorWritesInfo = {};
                    descriptorWritesInfo.bufferWrites = { write };
                    renderer.getDescriptorAllocator().writeUniforms(objDescriptorSet, descriptorWritesInfo);

                    //bind set
                    DescriptorBind bindingInfo = {};
                    bindingInfo.descriptorSetIndex = material.getRasterPipeline().getDrawDescriptorIndex();
                    bindingInfo.set = objDescriptorSet;
                    bindingInfo.layout = material.getRasterPipeline().getLayout();
                    bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                    renderer.getDescriptorAllocator().bindSet(cmdBuffer, bindingInfo);
                }

                //bind vbo and ibo and send draw calls (draw calls should be computed in the performCulling() function)
                meshData.parentModelPtr->bindBuffers(cmdBuffer);

                //draw
                vkCmdDrawIndexedIndirect(
                    cmdBuffer,
                    drawCommandsBuffer->getBuffer(),
                    meshData.drawCommandIndex * sizeof(uint32_t),
                    1,
                    sizeof(DrawCommand)
                );
            }
        }
    }

    void CommonMeshGroup::clearDrawCommand(const VkCommandBuffer &cmdBuffer) const
    {
        //clear instance count
        uint32_t drawCountDefaultValue = 0;
        for(const auto& [mesh, meshData] : meshesData)
        {
            //location
            const uint32_t instanceCountLocation = (sizeof(DrawCommand) * meshData.drawCommandIndex);
            
            //pre-transfer memory barrier
            const VkBufferMemoryBarrier2 preMemBarrier = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .pNext = NULL,
                .srcStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = drawCommandsBuffer->getBuffer(),
                .offset = instanceCountLocation,
                .size = sizeof(DrawCommand)
            };

            const VkDependencyInfo preDependency = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = NULL,
                .bufferMemoryBarrierCount = 1,
                .pBufferMemoryBarriers = &preMemBarrier
            };

            vkCmdPipelineBarrier2(cmdBuffer, &preDependency);

            //zero out instance count
            vkCmdFillBuffer(
                cmdBuffer,
                drawCommandsBuffer->getBuffer(),
                instanceCountLocation + offsetof(VkDrawIndexedIndirectCommand, instanceCount),
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
                .offset = instanceCountLocation,
                .size = sizeof(VkDrawIndexedIndirectCommand)
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

    void CommonMeshGroup::addOwner(const Queue &queue)
    {
        modelMatricesBuffer->addOwner(queue);
        drawCommandsBuffer->addOwner(queue);
    }
}