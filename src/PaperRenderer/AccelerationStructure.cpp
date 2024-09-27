#include "AccelerationStructure.h"
#include "PaperRenderer.h"

#include <algorithm>

namespace PaperRenderer
{
    //----------TLAS INSTANCE BUILD PIPELINE DEFINITIONS----------//

    TLASInstanceBuildPipeline::TLASInstanceBuildPipeline(RenderEngine *renderer, std::string fileDir)
        :ComputeShader(renderer),
        rendererPtr(renderer)
    {
        //uniform buffer
        BufferInfo uboInfo = {};
        uboInfo.allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        uboInfo.usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR;
        uboInfo.size = sizeof(UBOInputData);
        uniformBuffer = std::make_unique<Buffer>(rendererPtr, uboInfo);
        
        //pipeline info
        shader = { VK_SHADER_STAGE_COMPUTE_BIT, fileDir + fileName };

        VkDescriptorSetLayoutBinding inputDataDescriptor = {};
        inputDataDescriptor.binding = 0;
        inputDataDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        inputDataDescriptor.descriptorCount = 1;
        inputDataDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        descriptorSets[0].descriptorBindings[0] = inputDataDescriptor;

        VkDescriptorSetLayoutBinding inputInstancesDescriptor = {};
        inputInstancesDescriptor.binding = 1;
        inputInstancesDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        inputInstancesDescriptor.descriptorCount = 1;
        inputInstancesDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        descriptorSets[0].descriptorBindings[1] = inputInstancesDescriptor;

        VkDescriptorSetLayoutBinding inputASInstancesDescriptor = {};
        inputASInstancesDescriptor.binding = 2;
        inputASInstancesDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        inputASInstancesDescriptor.descriptorCount = 1;
        inputASInstancesDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        descriptorSets[0].descriptorBindings[2] = inputASInstancesDescriptor;

        VkDescriptorSetLayoutBinding outputASInstancesDescriptor = {};
        outputASInstancesDescriptor.binding = 3;
        outputASInstancesDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        outputASInstancesDescriptor.descriptorCount = 1;
        outputASInstancesDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        descriptorSets[0].descriptorBindings[3] = outputASInstancesDescriptor;

        buildPipeline();
    }

    TLASInstanceBuildPipeline::~TLASInstanceBuildPipeline()
    {
        uniformBuffer.reset();
    }

    void TLASInstanceBuildPipeline::submit(VkCommandBuffer cmdBuffer, const AccelerationStructure &accelerationStructure)
    {
        UBOInputData uboInputData = {};
        uboInputData.objectCount = accelerationStructure.accelerationStructureInstances.size();

        BufferWrite write = {};
        write.data = &uboInputData;
        write.size = sizeof(UBOInputData);
        write.offset = 0;

        uniformBuffer->writeToBuffer({ write });

        //set0 - binding 0: UBO input data
        VkDescriptorBufferInfo bufferWrite0Info = {};
        bufferWrite0Info.buffer = uniformBuffer->getBuffer();
        bufferWrite0Info.offset = 0;
        bufferWrite0Info.range = sizeof(UBOInputData);

        BuffersDescriptorWrites bufferWrite0 = {};
        bufferWrite0.binding = 0;
        bufferWrite0.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bufferWrite0.infos = { bufferWrite0Info };

        //set0 - binding 1: model instances
        VkDescriptorBufferInfo bufferWrite1Info = {};
        bufferWrite1Info.buffer = rendererPtr->instancesDataBuffer->getBuffer();
        bufferWrite1Info.offset = 0;
        bufferWrite1Info.range = rendererPtr->renderingModelInstances.size() * sizeof(ModelInstance::ShaderModelInstance);

        BuffersDescriptorWrites bufferWrite1 = {};
        bufferWrite1.binding = 1;
        bufferWrite1.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bufferWrite1.infos = { bufferWrite1Info };

        //set0 - binding 2: input objects               //BIG OL TODO
        VkDescriptorBufferInfo bufferWrite2Info = {};
        bufferWrite2Info.buffer = accelerationStructure.instancesBuffer->getBuffer();
        bufferWrite2Info.offset = 0;
        bufferWrite2Info.range = accelerationStructure.accelerationStructureInstances.size() * sizeof(ModelInstance::AccelerationStructureInstance);

        BuffersDescriptorWrites bufferWrite2 = {};
        bufferWrite2.binding = 2;
        bufferWrite2.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bufferWrite2.infos = { bufferWrite2Info };

        //set0 - binding 3: output objects
        VkDescriptorBufferInfo bufferWrite3Info = {};
        bufferWrite3Info.buffer = accelerationStructure.TLInstancesBuffer->getBuffer();
        bufferWrite3Info.offset = 0;
        bufferWrite3Info.range = accelerationStructure.accelerationStructureInstances.size() * sizeof(VkAccelerationStructureInstanceKHR);

        BuffersDescriptorWrites bufferWrite3 = {};
        bufferWrite3.binding = 3;
        bufferWrite3.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bufferWrite3.infos = { bufferWrite3Info };

        //bind, write, and dispatch
        bind(cmdBuffer);

        DescriptorWrites descriptorWritesInfo = {};
        descriptorWritesInfo.bufferWrites = { bufferWrite0, bufferWrite1, bufferWrite2, bufferWrite3 };
        descriptorWrites[0] = descriptorWritesInfo;
        writeDescriptorSet(cmdBuffer, 0);

        //dispatch
        workGroupSizes.x = ((accelerationStructure.accelerationStructureInstances.size()) / 256) + 1;
        dispatch(cmdBuffer);
    }

    //----------ACCELERATION STRUCTURE DEFINITIONS----------//

    AccelerationStructure::AccelerationStructure(RenderEngine* renderer)
        :rendererPtr(renderer)
    {        
        BufferInfo bufferInfo = {};
        bufferInfo.allocationFlags = 0;
        bufferInfo.size = 256; //arbitrary starting size
        bufferInfo.usageFlags =    VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        BLBuffer =          std::make_unique<Buffer>(rendererPtr, bufferInfo);
        bufferInfo.usageFlags =    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        TLInstancesBuffer = std::make_unique<Buffer>(rendererPtr, bufferInfo);
        bufferInfo.usageFlags =    VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        TLBuffer =          std::make_unique<Buffer>(rendererPtr, bufferInfo);
        bufferInfo.usageFlags =    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
        scratchBuffer =     std::make_unique<Buffer>(rendererPtr, bufferInfo);

        rendererPtr->accelerationStructures.push_back(this);

        rebuildInstancesBuffers();
    }

    AccelerationStructure::~AccelerationStructure()
    {
        instancesBuffer.reset();
        instanceDescriptionsBuffer.reset();

        vkDestroyAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), topStructure, nullptr);
        for(auto& [ptr, structure] : bottomStructures)
        {
            vkDestroyAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), structure.structure, nullptr);
        }
        bottomStructures.clear();

        rendererPtr->accelerationStructures.remove(this);
    }

    void AccelerationStructure::rebuildInstancesBuffers()
    {
        //create new buffers

        //instances
        VkDeviceSize newInstancesBufferSize = 0;
        newInstancesBufferSize += std::max((VkDeviceSize)(accelerationStructureInstances.size() * sizeof(ModelInstance::AccelerationStructureInstance) * instancesOverhead),
            (VkDeviceSize)(sizeof(ModelInstance::AccelerationStructureInstance) * 64));

        BufferInfo instancesBufferInfo = {};
        instancesBufferInfo.allocationFlags = 0;
        instancesBufferInfo.size = newInstancesBufferSize;
        instancesBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;
        std::unique_ptr<Buffer> newInstancesBuffer = std::make_unique<Buffer>(rendererPtr, instancesBufferInfo);

        //instances description
        VkDeviceSize newInstanceDescriptionsBufferSize = 0;
        newInstanceDescriptionsBufferSize += std::max((VkDeviceSize)(accelerationStructureInstances.size() * sizeof(InstanceDescription) * instancesOverhead),
            (VkDeviceSize)(sizeof(InstanceDescription) * 64));

        BufferInfo instanceDescriptionsBufferInfo = {};
        instanceDescriptionsBufferInfo.allocationFlags = 0;
        instanceDescriptionsBufferInfo.size = newInstanceDescriptionsBufferSize;
        instanceDescriptionsBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;
        std::unique_ptr<Buffer> newInstanceDescriptionsBuffer = std::make_unique<Buffer>(rendererPtr, instanceDescriptionsBufferInfo);

        //copy old data into new if old existed
        if(instancesBuffer && instanceDescriptionsBuffer)
        {
            VkBufferCopy instancesCopyRegion = {};
            instancesCopyRegion.srcOffset = 0;
            instancesCopyRegion.dstOffset = 0;
            instancesCopyRegion.size = std::min(accelerationStructureInstances.size() * sizeof(ModelInstance::AccelerationStructureInstance), instancesBuffer->getSize());

            VkBufferCopy instanceDescriptionsCopyRegion = {};
            instanceDescriptionsCopyRegion.srcOffset = 0;
            instanceDescriptionsCopyRegion.dstOffset = 0;
            instanceDescriptionsCopyRegion.size = std::min(accelerationStructureInstances.size() * sizeof(InstanceDescription), instanceDescriptionsBuffer->getSize());

            SynchronizationInfo syncInfo = {};
            syncInfo.queueType = TRANSFER;
            syncInfo.fence = Commands::getUnsignaledFence(rendererPtr);

            //start command buffer
            VkCommandBuffer cmdBuffer = Commands::getCommandBuffer(rendererPtr, syncInfo.queueType);

            VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.pNext = NULL;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            vkBeginCommandBuffer(cmdBuffer, &beginInfo);
            vkCmdCopyBuffer(cmdBuffer, instancesBuffer->getBuffer(), newInstancesBuffer->getBuffer(), 1, &instancesCopyRegion);
            vkCmdCopyBuffer(cmdBuffer, instanceDescriptionsBuffer->getBuffer(), newInstanceDescriptionsBuffer->getBuffer(), 1, &instanceDescriptionsCopyRegion);
            vkEndCommandBuffer(cmdBuffer);

            //submit
            Commands::submitToQueue(syncInfo, { cmdBuffer });

            rendererPtr->recycleCommandBuffer({ cmdBuffer, syncInfo.queueType });

            vkWaitForFences(rendererPtr->getDevice()->getDevice(), 1, &syncInfo.fence, VK_TRUE, UINT64_MAX);
            vkDestroyFence(rendererPtr->getDevice()->getDevice(), syncInfo.fence, nullptr);
        }
        
        //replace old buffers
        instancesBuffer = std::move(newInstancesBuffer);
        instanceDescriptionsBuffer = std::move(newInstanceDescriptionsBuffer);
    }

    void AccelerationStructure::setBuildData()
    {
        BottomBuildData BLBuildData = {};
        TopBuildData TLBuildData = {};

        //setup bottom level geometries (this one is broken dont un-comment)
        BLBuildData.modelsGeometries.reserve(blasBuildModels.size());
        BLBuildData.buildRangeInfos.reserve(blasBuildModels.size());
        for(auto& model : blasBuildModels)
        {
            std::vector<VkAccelerationStructureGeometryKHR> modelGeometries;
            std::vector<uint32_t> modelPrimitiveCounts;
            std::vector<VkAccelerationStructureBuildRangeInfoKHR> modelBuildRangeInfos;

            //get per material group geometry data
            for(const std::vector<LODMesh> meshes : model->getLODs().at(0).meshMaterialData) //use LOD 0 for BLAS
            {
                VkDeviceSize vertexCount = 0;
                VkDeviceSize indexCount = 0;
                VkDeviceAddress vertexOffset = meshes.at(0).vboOffset;
                VkDeviceAddress indexOffset = meshes.at(0).iboOffset;

                for(const LODMesh& mesh : meshes) //per mesh in mesh group data
                {
                    vertexCount += mesh.vertexCount;
                    indexCount += mesh.indexCount;
                }

                //buffer information
                VkAccelerationStructureGeometryTrianglesDataKHR trianglesGeometry = {};
                trianglesGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                trianglesGeometry.pNext = NULL;
                trianglesGeometry.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
                trianglesGeometry.vertexData = VkDeviceOrHostAddressConstKHR{.deviceAddress = model->getVBOAddress()};
                trianglesGeometry.maxVertex = vertexCount;
                trianglesGeometry.vertexStride = model->getVertexDescription().stride;
                trianglesGeometry.indexType = VK_INDEX_TYPE_UINT32;
                trianglesGeometry.indexData = VkDeviceOrHostAddressConstKHR{.deviceAddress = model->getIBOAddress()};
                //trianglesGeometry.transformData = transformMatrixBufferAddress;

                //geometries
                VkAccelerationStructureGeometryKHR structureGeometry = {};
                structureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
                structureGeometry.pNext = NULL;
                structureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
                structureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                structureGeometry.geometry.triangles = trianglesGeometry;
                
                VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo;
                buildRangeInfo.primitiveCount = indexCount / 3;
                buildRangeInfo.primitiveOffset = indexOffset * sizeof(uint32_t);
                buildRangeInfo.firstVertex = vertexOffset;
                buildRangeInfo.transformOffset = 0;

                modelGeometries.push_back(structureGeometry);
                modelPrimitiveCounts.push_back(indexCount / 3);
                modelBuildRangeInfos.push_back(buildRangeInfo);
            }
            BLBuildData.modelsGeometries.push_back(std::move(modelGeometries));
            BLBuildData.buildRangeInfos.push_back(std::move(modelBuildRangeInfos));
            
            //per model build information
            VkAccelerationStructureBuildGeometryInfoKHR buildGeoInfo = {};
            buildGeoInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            buildGeoInfo.pNext = NULL;
            buildGeoInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;// | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
            buildGeoInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            buildGeoInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            buildGeoInfo.srcAccelerationStructure = VK_NULL_HANDLE;
            buildGeoInfo.geometryCount = BLBuildData.modelsGeometries.rbegin()->size();
            buildGeoInfo.pGeometries = BLBuildData.modelsGeometries.rbegin()->data();
            buildGeoInfo.ppGeometries = NULL;

            VkAccelerationStructureBuildSizesInfoKHR buildSize = {};
            buildSize.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

            vkGetAccelerationStructureBuildSizesKHR(
                rendererPtr->getDevice()->getDevice(),
                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                &buildGeoInfo,
                modelPrimitiveCounts.data(),
                &buildSize);

            BLBuildData.buildGeometries.push_back(buildGeoInfo);
            BLBuildData.buildSizes.push_back(buildSize);
        }
        
        
        //add up total sizes and offsets needed
        for(uint32_t i = 0; i < BLBuildData.buildGeometries.size(); i++)
        {
            BLBuildData.scratchOffsets.push_back(BLBuildData.totalScratchSize);
            BLBuildData.totalScratchSize += BLBuildData.buildSizes.at(i).buildScratchSize;
            BLBuildData.asOffsets.push_back(BLBuildData.totalBuildSize);
            BLBuildData.totalBuildSize += (BLBuildData.buildSizes.at(i).accelerationStructureSize); 
            BLBuildData.totalBuildSize += 256 - (BLBuildData.totalBuildSize % 256); //must be a multiple of 256 bytes;
        }

        //----------TOP LEVEL----------//

        //instances
        instancesCount = accelerationStructureInstances.size();
        instancesBufferSize = instancesCount * sizeof(VkAccelerationStructureInstanceKHR);

        //geometries
        VkAccelerationStructureGeometryInstancesDataKHR geoInstances = {};
        geoInstances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geoInstances.pNext = NULL;
        geoInstances.arrayOfPointers = VK_FALSE;
        geoInstances.data = VkDeviceOrHostAddressConstKHR{.deviceAddress = TLInstancesBuffer->getBufferDeviceAddress()};

        VkAccelerationStructureGeometryDataKHR geometry = {};
        geometry.instances = geoInstances;

        TLBuildData.structureGeometry = {};
        TLBuildData.structureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        TLBuildData.structureGeometry.pNext = NULL;
        TLBuildData.structureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR; //TODO TRANSPARENCY
        TLBuildData.structureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        TLBuildData.structureGeometry.geometry = std::move(geometry);

        // Get the size requirements for buffers involved in the acceleration structure build process
        VkAccelerationStructureBuildGeometryInfoKHR buildGeoInfo = {};
        buildGeoInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildGeoInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildGeoInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildGeoInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildGeoInfo.srcAccelerationStructure = VK_NULL_HANDLE;
        buildGeoInfo.geometryCount = 1;
        buildGeoInfo.pGeometries   = &TLBuildData.structureGeometry;

        const uint32_t primitiveCount = instancesCount;

        VkAccelerationStructureBuildSizesInfoKHR TLBuildSizes = {};
        TLBuildSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

        vkGetAccelerationStructureBuildSizesKHR(
            rendererPtr->getDevice()->getDevice(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildGeoInfo,
            &primitiveCount,
            &TLBuildSizes);

        //rebuild buffers if needed (if needed size is greater than current alocation, or is 70% less than what's needed)
        TLBuildData.buildGeoInfo = std::move(buildGeoInfo);
        TLBuildData.buildSizes = std::move(TLBuildSizes);

        //----------CHECK IF ALLOCATION REBUILDS ARE NEEDED----------//

        //blas
        if(BLBuildData.totalBuildSize > BLBuffer->getSize()) //TODO CHECK IF SIZE IS WAY OVER WHAT IT NEEDS TO BE
        {
            BufferInfo bufferInfo = {};
            bufferInfo.allocationFlags = 0;
            bufferInfo.size = BLBuildData.totalBuildSize * 1.1; //allocate 10% more than what's currently needed
            bufferInfo.usageFlags = VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            BLBuffer = std::make_unique<Buffer>(rendererPtr, bufferInfo);
        }
        
        //tlas instances
        if(instancesBufferSize > TLInstancesBuffer->getSize())
        {
            BufferInfo bufferInfo = {};
            bufferInfo.allocationFlags = 0;
            bufferInfo.size = instancesBufferSize * 1.2; //allocate 20% more than what's currently needed
            bufferInfo.usageFlags = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            TLInstancesBuffer = std::make_unique<Buffer>(rendererPtr, bufferInfo);

            TLBuildData.structureGeometry.geometry.instances.data = VkDeviceOrHostAddressConstKHR{.deviceAddress = TLInstancesBuffer->getBufferDeviceAddress()};
        }

        //tlas
        if(TLBuildSizes.accelerationStructureSize > TLBuffer->getSize() || TLBuildSizes.accelerationStructureSize < TLBuffer->getSize() * 0.5)
        {
            BufferInfo bufferInfo = {};
            bufferInfo.allocationFlags = 0;
            bufferInfo.size = TLBuildSizes.accelerationStructureSize * 1.2; //allocate 20% more than what's currently needed
            bufferInfo.usageFlags = VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
            TLBuffer = std::make_unique<Buffer>(rendererPtr, bufferInfo);
        }

        //scratch
        VkDeviceSize scratchSize = std::max(BLBuildData.totalScratchSize, TLBuildSizes.buildScratchSize);
        if(scratchSize > scratchBuffer->getSize() || scratchSize < scratchBuffer->getSize() * 0.7)
        {
            BufferInfo bufferInfo = {};
            bufferInfo.allocationFlags = 0;
            bufferInfo.size = scratchSize * 1.1;
            bufferInfo.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
            scratchBuffer = std::make_unique<Buffer>(rendererPtr, bufferInfo);
        }

        //set BLAS addresses
        uint32_t modelIndex = 0;
        for(auto& model : blasBuildModels) //important to note here that the iteration order is the exact same as the one for collecting the initial data
        {
            bottomStructures.at(model).bufferAddress = BLBuffer->getBufferDeviceAddress() + BLBuildData.asOffsets.at(modelIndex);
            modelIndex++;

            //set references instances BLAS address
            for(ModelInstance* instance : bottomStructures.at(model).referencedInstances)
            {
                uint64_t blasAddress = bottomStructures.at(instance->getParentModelPtr()).bufferAddress;
                instance->accelerationStructureSelfReferences.at(this).blasAddress = blasAddress;

                //queue data transfer
                toUpdateInstances.push_front(instance);
            }
        }

        buildData = { std::move(BLBuildData), std::move(TLBuildData) };
    }

    void AccelerationStructure::updateAccelerationStructures(SynchronizationInfo syncInfo)
    {
        //start command buffer
        VkCommandBuffer cmdBuffer = Commands::getCommandBuffer(rendererPtr, syncInfo.queueType);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(cmdBuffer, &beginInfo);

        //blas builds (if needed)
        bool blasBuildNeeded = blasBuildModels.size();
        if(blasBuildNeeded) 
        {
            createBottomLevel(cmdBuffer, buildData.bottomData);

            //memory barriers
            VkBufferMemoryBarrier2 BLASMemBarrier = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .pNext = NULL,
                .srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = BLBuffer->getBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE
            };

            VkDependencyInfo buildDataDependencyInfo = {};
            buildDataDependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            buildDataDependencyInfo.pNext = NULL;
            buildDataDependencyInfo.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
            buildDataDependencyInfo.bufferMemoryBarrierCount = 1;
            buildDataDependencyInfo.pBufferMemoryBarriers = &BLASMemBarrier;

            vkCmdPipelineBarrier2(cmdBuffer, &buildDataDependencyInfo);
        }

        //build TLAS instance data
        rendererPtr->tlasInstanceBuildPipeline.submit(cmdBuffer, *this);

        //TLAS instance data memory barrier
        VkBufferMemoryBarrier2 tlasInstanceMemBarrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = TLInstancesBuffer->getBuffer(),
            .offset = 0,
            .size = VK_WHOLE_SIZE
        };

        VkDependencyInfo tlasInstanceDependencyInfo = {};
        tlasInstanceDependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        tlasInstanceDependencyInfo.pNext = NULL;
        tlasInstanceDependencyInfo.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        tlasInstanceDependencyInfo.bufferMemoryBarrierCount = 1;
        tlasInstanceDependencyInfo.pBufferMemoryBarriers = &tlasInstanceMemBarrier;

        vkCmdPipelineBarrier2(cmdBuffer, &tlasInstanceDependencyInfo);

        createTopLevel(cmdBuffer, buildData.topData);

        vkEndCommandBuffer(cmdBuffer);

        //submit
        Commands::submitToQueue(syncInfo, { cmdBuffer });

        rendererPtr->recycleCommandBuffer({ cmdBuffer, syncInfo.queueType });
    }

    void AccelerationStructure::createBottomLevel(VkCommandBuffer cmdBuffer, BottomBuildData buildData)
    {
        //build all models in queue
        uint32_t modelIndex = 0;
        for(auto& model : blasBuildModels)
        {
            if(bottomStructures.at(model).structure)
            {
                vkDestroyAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), bottomStructures.at(model).structure, nullptr);
            }

            buildData.buildGeometries.at(modelIndex).scratchData.deviceAddress = scratchBuffer->getBufferDeviceAddress() + buildData.scratchOffsets.at(modelIndex);

            VkAccelerationStructureCreateInfoKHR accelStructureInfo = {};
            accelStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
            accelStructureInfo.pNext = NULL;
            accelStructureInfo.createFlags = 0;
            accelStructureInfo.buffer = BLBuffer->getBuffer();
            accelStructureInfo.offset = buildData.asOffsets.at(modelIndex); //TODO OFFSET NEEDS FIXING... ACTUALLY A LOT NEEDS FIXING SO THAT THE BLAS CAN PROPERLY UPDATE
            accelStructureInfo.size = buildData.buildSizes.at(modelIndex).accelerationStructureSize;
            accelStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            
            vkCreateAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), &accelStructureInfo, nullptr, &bottomStructures.at(model).structure);
            buildData.buildGeometries.at(modelIndex).dstAccelerationStructure = bottomStructures.at(model).structure;

            modelIndex++;
        }
        blasBuildModels.clear();
        
        //build process
        std::vector<VkAccelerationStructureBuildRangeInfoKHR*> buildRangesPtrArray;
        for(auto buildRanges : buildData.buildRangeInfos) //model
        {
            std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangesArray;
            for(auto buildRange : buildRanges) //mesh
            {
                buildRangesArray.push_back(buildRange);
            }
            void* buildDataPtr = malloc(buildRangesArray.size() * sizeof(VkAccelerationStructureBuildRangeInfoKHR));
            memcpy(buildDataPtr, buildRangesArray.data(), buildRangesArray.size() * sizeof(VkAccelerationStructureBuildRangeInfoKHR));
            buildRangesPtrArray.push_back((VkAccelerationStructureBuildRangeInfoKHR*)buildDataPtr);
        }

        std::vector<VkAccelerationStructureBuildGeometryInfoKHR> vectorBuildGeos;
        for(const VkAccelerationStructureBuildGeometryInfoKHR& buildGeo : buildData.buildGeometries)
        {
            vectorBuildGeos.push_back(buildGeo);
        }

        //BLAS build
        vkCmdBuildAccelerationStructuresKHR(cmdBuffer, vectorBuildGeos.size(), vectorBuildGeos.data(), buildRangesPtrArray.data());

        for(auto& ptr : buildRangesPtrArray)
        {
            free(ptr);
        }
    }

    void AccelerationStructure::createTopLevel(VkCommandBuffer cmdBuffer, TopBuildData buildData)
    {
        //destroy old
        vkDestroyAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), topStructure, nullptr);
        buildData.buildGeoInfo.pGeometries = &buildData.structureGeometry; //idk why this gets corrupted halfway through

        VkAccelerationStructureCreateInfoKHR accelStructureInfo = {};
        accelStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        accelStructureInfo.pNext = NULL;
        accelStructureInfo.createFlags = 0;
        accelStructureInfo.buffer = TLBuffer->getBuffer();
        accelStructureInfo.offset = 0;
        accelStructureInfo.size = buildData.buildSizes.accelerationStructureSize;
        accelStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

        vkCreateAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), &accelStructureInfo, nullptr, &topStructure);
        buildData.buildGeoInfo.scratchData.deviceAddress = scratchBuffer->getBufferDeviceAddress();
        buildData.buildGeoInfo.dstAccelerationStructure = topStructure;

        VkAccelerationStructureBuildRangeInfoKHR buildRange;
        buildRange.primitiveCount = instancesCount;
        buildRange.primitiveOffset = 0;
        buildRange.firstVertex = 0;
        buildRange.transformOffset = 0;
        std::vector<VkAccelerationStructureBuildRangeInfoKHR*> buildRangesPtrArray = {&buildRange};

        vkCmdBuildAccelerationStructuresKHR(cmdBuffer, 1, &buildData.buildGeoInfo, buildRangesPtrArray.data());
    }

    void AccelerationStructure::queueInstanceTransfers()
    {
        //set build data
        setBuildData();

        //check buffer sizes
        if(instancesBuffer->getSize() / sizeof(ModelInstance::AccelerationStructureInstance) < accelerationStructureInstances.size() ||
            instancesBuffer->getSize() / sizeof(ModelInstance::AccelerationStructureInstance) > accelerationStructureInstances.size() * 2)
        {
            rebuildInstancesBuffers(); //TODO SYNCHRONIZATION
        }

        //sort instances; remove duplicates
        std::sort(toUpdateInstances.begin(), toUpdateInstances.end());
        auto sortedInstances = std::unique(toUpdateInstances.begin(), toUpdateInstances.end());
        toUpdateInstances.erase(sortedInstances, toUpdateInstances.end());

        //queue instance data
        for(ModelInstance* instance : toUpdateInstances)
        {
            //skip if instance is NULL
            if(!instance) continue;

            //write instance data
            ModelInstance::AccelerationStructureInstance instanceShaderData = {};
            instanceShaderData.blasReference = instance->accelerationStructureSelfReferences.at(this).blasAddress;
            instanceShaderData.modelInstanceIndex = instance->rendererSelfIndex;

            std::vector<char> instanceData(sizeof(ModelInstance::AccelerationStructureInstance));
            memcpy(instanceData.data(), &instanceShaderData, instanceData.size());
            
            //queue data transfer
            rendererPtr->getEngineStagingBuffer()->queueDataTransfers(*instancesBuffer, sizeof(ModelInstance::AccelerationStructureInstance) * instance->rendererSelfIndex, instanceData);

            //write description data
            InstanceDescription descriptionShaderData = {};
            descriptionShaderData.modelDataOffset = instance->getParentModelPtr()->getShaderDataLocation(); //TODO MODEL DATA COMPACTION ERROR

            std::vector<char> descriptionData(sizeof(InstanceDescription));
            memcpy(descriptionData.data(), &descriptionShaderData, descriptionData.size());
            
            //queue data transfer
            rendererPtr->getEngineStagingBuffer()->queueDataTransfers(*instanceDescriptionsBuffer, sizeof(InstanceDescription) * instance->rendererSelfIndex, descriptionData);
        }

        //clear deques
        toUpdateInstances.clear();
    }

    void AccelerationStructure::addInstance(ModelInstance *instance)
    {
        instanceAddRemoveMutex.lock();

        //add reference
        instance->accelerationStructureSelfReferences[this].selfIndex = accelerationStructureInstances.size();
        accelerationStructureInstances.push_back(instance);

        //queue data transfer
        toUpdateInstances.push_front(instance);

        //add model reference and queue BLAS build if needed
        if(!bottomStructures.count(instance->getParentModelPtr()))
        {
            blasBuildModels.push_back(instance->getParentModelPtr());
            bottomStructures[instance->getParentModelPtr()] = {};
        }
        bottomStructures.at(instance->getParentModelPtr()).referencedInstances.push_back(instance);

        //set BLAS address
        instance->accelerationStructureSelfReferences.at(this).blasAddress = bottomStructures.at(instance->getParentModelPtr()).bufferAddress;

        instanceAddRemoveMutex.unlock();
    }
    
    void AccelerationStructure::removeInstance(ModelInstance *instance)
    {
        //remove reference
        if(accelerationStructureInstances.size() > 1)
        {
            uint32_t& selfReference = instance->accelerationStructureSelfReferences.at(this).selfIndex;
            accelerationStructureInstances.at(selfReference) = accelerationStructureInstances.back();
            accelerationStructureInstances.at(selfReference)->accelerationStructureSelfReferences.at(this).selfIndex = selfReference;

            //queue data transfer
            toUpdateInstances.push_front(accelerationStructureInstances.at(instance->accelerationStructureSelfReferences.at(this).selfIndex));
            
            accelerationStructureInstances.pop_back();
        }
        else
        {
            accelerationStructureInstances.clear();
        }

        //null out any instances that may be queued
        for(ModelInstance*& thisInstance : toUpdateInstances)
        {
            if(thisInstance == instance)
            {
                thisInstance = NULL;
            }
        }

        instance->accelerationStructureSelfReferences.erase(this);
        bottomStructures.at(instance->getParentModelPtr()).referencedInstances.remove(instance);
    }
}
