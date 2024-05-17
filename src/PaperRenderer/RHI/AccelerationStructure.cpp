#include "AccelerationStructure.h"
#include "../Model.h"

namespace PaperRenderer
{
    AccelerationStructure::AccelerationStructure(Device *device)
        :devicePtr(device),
        topStructure(VK_NULL_HANDLE)
    {
        ASAllocations0.resize(PaperMemory::Commands::getFrameCount());
        ASAllocations1.resize(PaperMemory::Commands::getFrameCount());
        BLBuffers.resize(PaperMemory::Commands::getFrameCount());
        BLScratchBuffers.resize(PaperMemory::Commands::getFrameCount());
        TLInstancesBuffers.resize(PaperMemory::Commands::getFrameCount());
        TLBuffers.resize(PaperMemory::Commands::getFrameCount());
        TLScratchBuffers.resize(PaperMemory::Commands::getFrameCount());
        blasSignalSemaphores.resize(PaperMemory::Commands::getFrameCount());

        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            //IMPORTANT NOTE HERE: BUFFERS ONLY USE THE COMPUTE FAMILY INDEX FOR ACCEL. STRUCTURE OPS, NOT THE GRAPHICS FAMILY
            PaperMemory::BufferInfo bufferInfo = {};
            bufferInfo.queueFamilyIndices = { devicePtr->getQueues().at(PaperMemory::QueueType::COMPUTE).queueFamilyIndex };
            bufferInfo.size = 256; //arbitrary starting size
            
            //allocation 0
            bufferInfo.usageFlags =    VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            BLBuffers.at(i) =          std::make_unique<PaperMemory::Buffer>(devicePtr->getDevice(), bufferInfo);
            bufferInfo.usageFlags =    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            BLScratchBuffers.at(i) =   std::make_unique<PaperMemory::Buffer>(devicePtr->getDevice(), bufferInfo);
            bufferInfo.usageFlags =    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            TLInstancesBuffers.at(i) = std::make_unique<PaperMemory::Buffer>(devicePtr->getDevice(), bufferInfo);
            rebuildAllocations0(i);

            //allocation 1
            bufferInfo.usageFlags =    VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            TLBuffers.at(i) =          std::make_unique<PaperMemory::Buffer>(devicePtr->getDevice(), bufferInfo);
            bufferInfo.usageFlags =    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            TLScratchBuffers.at(i) =   std::make_unique<PaperMemory::Buffer>(devicePtr->getDevice(), bufferInfo);
            rebuildAllocations1(i);

            //synchronization things
            blasSignalSemaphores.at(i) = PaperMemory::Commands::getSemaphore(devicePtr->getDevice());
        }
    }

    AccelerationStructure::~AccelerationStructure()
    {
        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            vkDestroySemaphore(devicePtr->getDevice(), blasSignalSemaphores.at(i), nullptr);
        }

        vkDestroyAccelerationStructureKHR(devicePtr->getDevice(), topStructure, nullptr);
        for(auto& [ptr, structure] : bottomStructures)
        {
            vkDestroyAccelerationStructureKHR(devicePtr->getDevice(), structure.structure, nullptr);
        }
        bottomStructures.clear();
    }

    void AccelerationStructure::verifyBufferSizes(const std::unordered_map<Model*, std::vector<ModelInstance*>> &modelInstances, uint32_t currentFrame)
    {
        BLBuildData = BottomBuildData{}; //reset build data

        //get model and instance data in neater format
        std::vector<ModelInstance*> vectorModelInstances;
        for(const auto& [model, instances] : modelInstances)
        {
            BLBuildData.buildModels.push_back(model);
            for(auto instance = instances.begin(); instance != instances.end(); instance++)
            {
                vectorModelInstances.push_back(*instance);
            }
        }
        instancesCount = vectorModelInstances.size();
        instancesBufferSize = vectorModelInstances.size() * sizeof(VkAccelerationStructureInstanceKHR);

        //setup bottom level geometries
        for(Model* model : BLBuildData.buildModels)
        {
            std::vector<VkAccelerationStructureGeometryKHR> modelGeometries;
            std::vector<uint32_t> modelPrimitiveCounts;
            std::vector<VkAccelerationStructureBuildRangeInfoKHR> modelBuildRangeInfos;

            //get per mesh group geometry data
            for(const auto& [matIndex, meshes] : model->getLODs().at(0).meshes) //use LOD 0 for BLAS
            {
                for(const LODMesh& mesh : meshes) //per mesh in mesh group data
                {
                    //buffer information
                    VkAccelerationStructureGeometryTrianglesDataKHR trianglesGeometry = {};
                    trianglesGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                    trianglesGeometry.pNext = NULL;
                    trianglesGeometry.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
                    trianglesGeometry.vertexData = VkDeviceOrHostAddressConstKHR{.deviceAddress = model->getVBOAddress() + mesh.vboOffset};
                    trianglesGeometry.maxVertex = mesh.vertexCount;
                    trianglesGeometry.vertexStride = sizeof(PaperMemory::Vertex);
                    trianglesGeometry.indexType = VK_INDEX_TYPE_UINT32;
                    trianglesGeometry.indexData = VkDeviceOrHostAddressConstKHR{.deviceAddress = model->getIBOAddress() + mesh.iboOffset};
                    //trianglesGeometry.transformData = transformMatrixBufferAddress;

                    //geometries
                    VkAccelerationStructureGeometryKHR structureGeometry = {};
                    structureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
                    structureGeometry.pNext = NULL;
                    structureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
                    structureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                    structureGeometry.geometry.triangles = trianglesGeometry;
                    
                    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo;
                    buildRangeInfo.primitiveCount = mesh.indexCount / 3;
                    buildRangeInfo.primitiveOffset = 0;
                    buildRangeInfo.firstVertex = 0;
                    buildRangeInfo.transformOffset = 0;

                    modelGeometries.push_back(std::move(structureGeometry));
                    modelPrimitiveCounts.push_back(mesh.indexCount / 3);
                    modelBuildRangeInfos.push_back(std::move(buildRangeInfo));
                }
            }
            BLBuildData.modelsGeometries.push_back(std::move(modelGeometries));
            BLBuildData.buildRangeInfos.push_back(std::move(modelBuildRangeInfos));
            
            //per model build information
            VkAccelerationStructureBuildGeometryInfoKHR buildGeoInfo = {};
            buildGeoInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            buildGeoInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            buildGeoInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;// | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
            buildGeoInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            buildGeoInfo.srcAccelerationStructure = VK_NULL_HANDLE;
            buildGeoInfo.geometryCount = BLBuildData.modelsGeometries.rbegin()->size();
            buildGeoInfo.pGeometries = BLBuildData.modelsGeometries.rbegin()->data();
            buildGeoInfo.ppGeometries = NULL;

            VkAccelerationStructureBuildSizesInfoKHR buildSize = {};
            buildSize.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

            vkGetAccelerationStructureBuildSizesKHR(
                devicePtr->getDevice(),
                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                &buildGeoInfo,
                modelPrimitiveCounts.data(),
                &buildSize);

            BLBuildData.buildGeometries[model] = buildGeoInfo;
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
    
        //rebuild buffers if needed (if needed size is greater than current alocation, or is 70% less than what's needed)
        bool rebuildFlag = false;

        if(BLBuildData.totalBuildSize > BLBuffers.at(currentFrame)->getSize() || BLBuildData.totalBuildSize < BLBuffers.at(currentFrame)->getSize() * 0.7)
        {
            rebuildFlag = true;
        }
        if(BLBuildData.totalScratchSize > BLScratchBuffers.at(currentFrame)->getSize() || BLBuildData.totalScratchSize < BLScratchBuffers.at(currentFrame)->getSize() * 0.7)
        {
            rebuildFlag = true;
        }
        if(instancesBufferSize > TLInstancesBuffers.at(currentFrame)->getSize() || instancesBufferSize < TLInstancesBuffers.at(currentFrame)->getSize() * 0.7)
        {
            rebuildFlag = true;
        }

        //rebuild all buffers with new size and allocation if needed
        if(rebuildFlag)
        {
            //BL buffers
            PaperMemory::BufferInfo bufferInfo0 = {};
            bufferInfo0.queueFamilyIndices = { devicePtr->getQueues().at(PaperMemory::QueueType::COMPUTE).queueFamilyIndex };
            bufferInfo0.size = BLBuildData.totalBuildSize * 1.2; //allocate 20% more than what's currently needed
            bufferInfo0.usageFlags = VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            BLBuffers.at(currentFrame) = std::make_unique<PaperMemory::Buffer>(devicePtr->getDevice(), bufferInfo0);

            //BL scratch
            PaperMemory::BufferInfo bufferInfo1 = {};
            bufferInfo1.queueFamilyIndices = { devicePtr->getQueues().at(PaperMemory::QueueType::COMPUTE).queueFamilyIndex };
            bufferInfo1.size = BLBuildData.totalScratchSize * 1.2; //allocate 20% more than what's currently needed
            bufferInfo1.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            BLScratchBuffers.at(currentFrame) = std::make_unique<PaperMemory::Buffer>(devicePtr->getDevice(), bufferInfo1);

            //TL instances
            PaperMemory::BufferInfo bufferInfo2 = {};
            bufferInfo2.queueFamilyIndices = { devicePtr->getQueues().at(PaperMemory::QueueType::COMPUTE).queueFamilyIndex };
            bufferInfo2.size = instancesBufferSize * 1.2; //allocate 20% more than what's currently needed
            bufferInfo2.usageFlags = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            TLInstancesBuffers.at(currentFrame) = std::make_unique<PaperMemory::Buffer>(devicePtr->getDevice(), bufferInfo2);

            rebuildAllocations0(currentFrame);
        }
    }

    void AccelerationStructure::rebuildAllocations0(uint32_t currentFrame)
    {
        //find new size
        VkDeviceSize newSize = 0;
        newSize += PaperMemory::DeviceAllocation::padToMultiple(BLBuffers.at(currentFrame)->getMemoryRequirements().size, BLScratchBuffers.at(currentFrame)->getMemoryRequirements().alignment);
        newSize += PaperMemory::DeviceAllocation::padToMultiple(BLScratchBuffers.at(currentFrame)->getMemoryRequirements().size, TLInstancesBuffers.at(currentFrame)->getMemoryRequirements().alignment);
        newSize += TLInstancesBuffers.at(currentFrame)->getMemoryRequirements().size;

        //rebuild allocation (no need for copying since the buffer data changes every frame by the compute shader)
        PaperMemory::DeviceAllocationInfo allocInfo = {};
        allocInfo.allocationSize = newSize;
        allocInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        allocInfo.allocFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        ASAllocations0.at(currentFrame) = std::make_unique<PaperMemory::DeviceAllocation>(devicePtr->getDevice(), devicePtr->getGPU(), allocInfo);

        //assign allocation to buffers
        int errorCheck = 0;
        errorCheck += BLBuffers.at(currentFrame)->assignAllocation(ASAllocations0.at(currentFrame).get());
        errorCheck += BLScratchBuffers.at(currentFrame)->assignAllocation(ASAllocations0.at(currentFrame).get());
        errorCheck += TLInstancesBuffers.at(currentFrame)->assignAllocation(ASAllocations0.at(currentFrame).get());

        if(errorCheck != 0)
        {
            throw std::runtime_error("Acceleration Structure allocation rebuild failed"); //programmer error
        }

    }
    
    void AccelerationStructure::rebuildAllocations1(uint32_t currentFrame)
    {
        //find new size
        VkDeviceSize newSize = 0;
        newSize += PaperMemory::DeviceAllocation::padToMultiple(TLBuffers.at(currentFrame)->getMemoryRequirements().size, TLScratchBuffers.at(currentFrame)->getMemoryRequirements().alignment);
        newSize += TLScratchBuffers.at(currentFrame)->getMemoryRequirements().size;
        
        //rebuild allocation (no need for copying since the buffer data changes every frame by the compute shader)
        PaperMemory::DeviceAllocationInfo allocInfo = {};
        allocInfo.allocationSize = newSize;
        allocInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        allocInfo.allocFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        ASAllocations1.at(currentFrame) = std::make_unique<PaperMemory::DeviceAllocation>(devicePtr->getDevice(), devicePtr->getGPU(), allocInfo);

        //assign allocation to buffers
        int errorCheck = 0;
        errorCheck += TLBuffers.at(currentFrame)->assignAllocation(ASAllocations1.at(currentFrame).get());
        errorCheck += TLScratchBuffers.at(currentFrame)->assignAllocation(ASAllocations1.at(currentFrame).get());

        if(errorCheck != 0)
        {
            throw std::runtime_error("Acceleration Structure allocation rebuild failed"); //programmer error
        }
    }

    PaperMemory::CommandBuffer AccelerationStructure::updateBLAS(const std::unordered_map<Model*, std::vector<ModelInstance*>> &modelInstances, const PaperMemory::SynchronizationInfo& synchronizationInfo, uint32_t currentFrame)
    {
        verifyBufferSizes(modelInstances, currentFrame);
        return createBottomLevel(synchronizationInfo, currentFrame);
    }

    PaperMemory::CommandBuffer AccelerationStructure::updateTLAS(const PaperMemory::SynchronizationInfo& synchronizationInfo, uint32_t currentFrame)
    {
        return createTopLevel(synchronizationInfo, currentFrame);
    }

    PaperMemory::CommandBuffer AccelerationStructure::createBottomLevel(const PaperMemory::SynchronizationInfo &synchronizationInfo, uint32_t currentFrame)
    {
        /*VkTransformMatrixKHR defaultTransform = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f};

        //create transform buffer
        const VkBufferUsageFlags bufferUsageFlags = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        StagingBuffer transformationMatrixStaging(devicePtr, commandsPtr, sizeof(defaultTransform));
        transformationMatrixStaging.mapData(&defaultTransform, 0, sizeof(defaultTransform));

        Buffer transformMatrixBuffer(devicePtr, commandsPtr, sizeof(defaultTransform));
        transformMatrixBuffer.createBuffer(bufferUsageFlags, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
        transformMatrixBuffer.copyFromBuffer(transformationMatrixStaging, std::vector<SemaphorePair>(), std::vector<SemaphorePair>(), VK_NULL_HANDLE);*/

        //clear previous structures
        for(auto& [modelPtr, structure] : bottomStructures)
        {
            vkDestroyAccelerationStructureKHR(devicePtr->getDevice(), structure.structure, nullptr);
        }
        bottomStructures.clear();

        //create BLASes (funny grammar moment)
        uint32_t modelIndex = 0;
        for(Model const* model : BLBuildData.buildModels)
        {
            bottomStructures[model].structure = VK_NULL_HANDLE;
            bottomStructures[model].bufferAddress = BLBuffers.at(currentFrame)->getBufferDeviceAddress() + BLBuildData.asOffsets.at(modelIndex);
            BLBuildData.buildGeometries[model].scratchData.deviceAddress = BLScratchBuffers.at(currentFrame)->getBufferDeviceAddress() + BLBuildData.scratchOffsets.at(modelIndex);

            VkAccelerationStructureCreateInfoKHR accelStructureInfo = {};
            accelStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
            accelStructureInfo.pNext = NULL;
            accelStructureInfo.createFlags = 0;
            accelStructureInfo.buffer = BLBuffers.at(currentFrame)->getBuffer();
            accelStructureInfo.offset = BLBuildData.asOffsets.at(modelIndex);
            accelStructureInfo.size = BLBuildData.buildSizes.at(modelIndex).accelerationStructureSize;
            accelStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            
            vkCreateAccelerationStructureKHR(devicePtr->getDevice(), &accelStructureInfo, nullptr, &bottomStructures[model].structure);
            BLBuildData.buildGeometries.at(model).dstAccelerationStructure = bottomStructures[model].structure;

            modelIndex++;
        }
        
        //build process
        std::vector<VkAccelerationStructureBuildRangeInfoKHR*> buildRangesPtrArray;
        for(auto buildRanges : BLBuildData.buildRangeInfos) //model
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
        for(const auto& [model, buildGeo] : BLBuildData.buildGeometries)
        {
            vectorBuildGeos.push_back(buildGeo);
        }

        //command buffer and BLAS build
        VkCommandBuffer cmdBuffer = PaperMemory::Commands::getCommandBuffer(devicePtr->getDevice(), PaperMemory::QueueType::COMPUTE);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginInfo.pInheritanceInfo = NULL;
        
        vkBeginCommandBuffer(cmdBuffer, &beginInfo);
        vkCmdBuildAccelerationStructuresKHR(cmdBuffer, vectorBuildGeos.size(), vectorBuildGeos.data(), buildRangesPtrArray.data());
        vkEndCommandBuffer(cmdBuffer);

        //synchronization
        PaperMemory::SynchronizationInfo blasSyncInfo = {};
        blasSyncInfo.queueType = PaperMemory::QueueType::COMPUTE;
        blasSyncInfo.waitPairs = {};
        blasSyncInfo.signalPairs = { { blasSignalSemaphores.at(currentFrame), VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR} };
        blasSyncInfo.fence = VK_NULL_HANDLE;

        //insert "injected" synchronization
        blasSyncInfo.waitPairs.insert(blasSyncInfo.waitPairs.end(), synchronizationInfo.waitPairs.begin(), synchronizationInfo.waitPairs.end());
        blasSyncInfo.signalPairs.insert(blasSyncInfo.signalPairs.end(), synchronizationInfo.signalPairs.begin(), synchronizationInfo.signalPairs.end());
        blasSyncInfo.fence = synchronizationInfo.fence;

        PaperMemory::Commands::submitToQueue(devicePtr->getDevice(), blasSyncInfo, { cmdBuffer });

        for(auto& ptr : buildRangesPtrArray)
        {
            free(ptr);
        }

        return { cmdBuffer, PaperMemory::COMPUTE };
    }

    PaperMemory::CommandBuffer AccelerationStructure::createTopLevel(const PaperMemory::SynchronizationInfo& synchronizationInfo, uint32_t currentFrame)
    {
        //destroy old
        vkDestroyAccelerationStructureKHR(devicePtr->getDevice(), topStructure, nullptr);

        //geometries
        VkAccelerationStructureGeometryInstancesDataKHR geoInstances = {};
        geoInstances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geoInstances.pNext = NULL;
        geoInstances.arrayOfPointers = VK_FALSE;
        geoInstances.data = VkDeviceOrHostAddressConstKHR{.deviceAddress = TLInstancesBuffers.at(currentFrame)->getBufferDeviceAddress()};

        VkAccelerationStructureGeometryDataKHR geometry = {};
        geometry.instances = geoInstances;

        VkAccelerationStructureGeometryKHR structureGeometry = {};
        structureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        structureGeometry.pNext = NULL;
        structureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR; //TODO TRANSPARENCY
        structureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        structureGeometry.geometry = geometry;

        // Get the size requirements for buffers involved in the acceleration structure build process
        VkAccelerationStructureBuildGeometryInfoKHR buildGeoInfo = {};
        buildGeoInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildGeoInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildGeoInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildGeoInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildGeoInfo.srcAccelerationStructure = VK_NULL_HANDLE;
        buildGeoInfo.geometryCount = 1;
        buildGeoInfo.pGeometries   = &structureGeometry;

        const uint32_t primitiveCount = instancesCount;

        VkAccelerationStructureBuildSizesInfoKHR buildSizes = {};
        buildSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

        vkGetAccelerationStructureBuildSizesKHR(
            devicePtr->getDevice(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildGeoInfo,
            &primitiveCount,
            &buildSizes);

        //rebuild buffers if needed (if needed size is greater than current alocation, or is 70% less than what's needed)
        bool rebuildFlag = false;
        if(buildSizes.buildScratchSize > TLScratchBuffers.at(currentFrame)->getSize() || buildSizes.buildScratchSize < TLScratchBuffers.at(currentFrame)->getSize() * 0.7)
        {
            rebuildFlag = true;
        }
        if(buildSizes.accelerationStructureSize > TLBuffers.at(currentFrame)->getSize() || buildSizes.accelerationStructureSize < TLBuffers.at(currentFrame)->getSize() * 0.7)
        {
            rebuildFlag = true;
        }
        if(rebuildFlag)
        {
            PaperMemory::BufferInfo bufferInfo0 = {};
            bufferInfo0.queueFamilyIndices = { devicePtr->getQueues().at(PaperMemory::QueueType::COMPUTE).queueFamilyIndex };
            bufferInfo0.size = buildSizes.buildScratchSize * 1.2; //allocate 20% more than what's currently needed
            bufferInfo0.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            TLScratchBuffers.at(currentFrame) = std::make_unique<PaperMemory::Buffer>(devicePtr->getDevice(), bufferInfo0);

            PaperMemory::BufferInfo bufferInfo1 = {};
            bufferInfo1.queueFamilyIndices = { devicePtr->getQueues().at(PaperMemory::QueueType::COMPUTE).queueFamilyIndex };
            bufferInfo1.size = buildSizes.accelerationStructureSize * 1.2; //allocate 20% more than what's currently needed
            bufferInfo1.usageFlags = VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
            TLBuffers.at(currentFrame) = std::make_unique<PaperMemory::Buffer>(devicePtr->getDevice(), bufferInfo1);

            rebuildAllocations1(currentFrame);
        }
        buildGeoInfo.scratchData.deviceAddress = TLScratchBuffers.at(currentFrame)->getBufferDeviceAddress();

        VkAccelerationStructureCreateInfoKHR accelStructureInfo = {};
        accelStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        accelStructureInfo.pNext = NULL;
        accelStructureInfo.createFlags = 0;
        accelStructureInfo.buffer = TLBuffers.at(currentFrame)->getBuffer();
        accelStructureInfo.offset = 0;
        accelStructureInfo.size = buildSizes.accelerationStructureSize;
        accelStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

        vkCreateAccelerationStructureKHR(devicePtr->getDevice(), &accelStructureInfo, nullptr, &topStructure);
        buildGeoInfo.dstAccelerationStructure = topStructure;

        VkAccelerationStructureBuildRangeInfoKHR buildRange;
        buildRange.primitiveCount = instancesCount;
        buildRange.primitiveOffset = 0;
        buildRange.firstVertex = 0;
        buildRange.transformOffset = 0;
        std::vector<VkAccelerationStructureBuildRangeInfoKHR*> buildRangesPtrArray = {&buildRange};

        VkCommandBuffer cmdBuffer = PaperMemory::Commands::getCommandBuffer(devicePtr->getDevice(), PaperMemory::QueueType::COMPUTE);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginInfo.pInheritanceInfo = NULL;
        
        vkBeginCommandBuffer(cmdBuffer, &beginInfo);
        vkCmdBuildAccelerationStructuresKHR(cmdBuffer, 1, &buildGeoInfo, buildRangesPtrArray.data());
        vkEndCommandBuffer(cmdBuffer);

        //synchronization
        PaperMemory::SynchronizationInfo tlasSyncInfo = {};
        tlasSyncInfo.queueType = PaperMemory::QueueType::COMPUTE;
        tlasSyncInfo.waitPairs = { { blasSignalSemaphores.at(currentFrame), VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR} };
        tlasSyncInfo.signalPairs = {};
        tlasSyncInfo.fence = VK_NULL_HANDLE;

        //insert "injected" synchronization
        tlasSyncInfo.waitPairs.insert(tlasSyncInfo.waitPairs.end(), synchronizationInfo.waitPairs.begin(), synchronizationInfo.waitPairs.end());
        tlasSyncInfo.signalPairs.insert(tlasSyncInfo.signalPairs.end(), synchronizationInfo.signalPairs.begin(), synchronizationInfo.signalPairs.end());
        tlasSyncInfo.fence = synchronizationInfo.fence;

        PaperMemory::Commands::submitToQueue(devicePtr->getDevice(), tlasSyncInfo, { cmdBuffer });

        //get top address
        VkAccelerationStructureDeviceAddressInfoKHR accelerationAddressInfo{};
        accelerationAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        accelerationAddressInfo.accelerationStructure = topStructure;
        topStructureAddress = vkGetAccelerationStructureDeviceAddressKHR(devicePtr->getDevice(), &accelerationAddressInfo);

        return { cmdBuffer, PaperMemory::COMPUTE };
    }

    VkDeviceAddress AccelerationStructure::getTLASInstancesBufferAddress(uint32_t currentFrame) const
    {
        VkBufferDeviceAddressInfo addressInfo = {};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.pNext = NULL;
        addressInfo.buffer = TLInstancesBuffers.at(currentFrame)->getBuffer();
        return vkGetBufferDeviceAddress(devicePtr->getDevice(), &addressInfo);
    }
}
