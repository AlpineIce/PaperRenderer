#include "AccelerationStructure.h"
#include "../Model.h"

namespace Renderer
{
    AccelerationStructure::AccelerationStructure(Device *device, CmdBufferAllocator *commands)
        :devicePtr(device),
        commandsPtr(commands),
        //bottomStructure(VK_NULL_HANDLE),
        topStructure(VK_NULL_HANDLE)
    {
        BottomSignalSemaphores.resize(commandsPtr->getFrameCount());
        BLBuffers.resize(commandsPtr->getFrameCount());
        BLScratchBuffers.resize(commandsPtr->getFrameCount());
        TLInstancesBuffers.resize(commandsPtr->getFrameCount());
        TLBuffers.resize(commandsPtr->getFrameCount());
        TLScratchBuffers.resize(commandsPtr->getFrameCount());
        for(uint32_t i = 0; i < commandsPtr->getFrameCount(); i++)
        {
            BottomSignalSemaphores.at(i) = commandsPtr->getSemaphore();
            BLBuffers.at(i) = std::make_shared<Buffer>(devicePtr, commandsPtr, 256); //starting size really doesn't matter
            BLBuffers.at(i)->createBuffer(VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
            BLScratchBuffers.at(i) = std::make_shared<Buffer>(devicePtr, commandsPtr, 256); //starting size really doesn't matter
            BLScratchBuffers.at(i)->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
            TLInstancesBuffers.at(i) = std::make_shared<Buffer>(devicePtr, commandsPtr, 256);
            TLInstancesBuffers.at(i)->createBuffer(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
            TLBuffers.at(i) = std::make_shared<Buffer>(devicePtr, commandsPtr, 256); //starting size really doesn't matter
            TLBuffers.at(i)->createBuffer(VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
            TLScratchBuffers.at(i) = std::make_shared<Buffer>(devicePtr, commandsPtr, 256); //starting size really doesn't matter
            TLScratchBuffers.at(i)->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
        }
    }

    AccelerationStructure::~AccelerationStructure()
    {
        for(uint32_t i = 0; i < commandsPtr->getFrameCount(); i++)
        {
            vkDestroySemaphore(devicePtr->getDevice(), BottomSignalSemaphores.at(i), nullptr);
        }

        vkDestroyAccelerationStructureKHR(devicePtr->getDevice(), topStructure, nullptr);
        for(auto& [ptr, structure] : bottomStructures)
        {
            vkDestroyAccelerationStructureKHR(devicePtr->getDevice(), structure.structure, nullptr);
        }
        bottomStructures.clear();
    }

    void AccelerationStructure::verifyBufferSizes(const std::unordered_map<Model*, std::list<ModelInstance*>> &modelInstances, uint32_t currentFrame)
    {
        //BOTTOM LEVEL
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
                    trianglesGeometry.vertexStride = sizeof(Vertex);
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
        if(BLBuildData.totalScratchSize > BLScratchBuffers.at(currentFrame)->getAllocatedSize() || BLBuildData.totalScratchSize < BLScratchBuffers.at(currentFrame)->getAllocatedSize() * 0.7)
        {
            VkDeviceSize newSize = BLBuildData.totalScratchSize * 1.1; //allocate 10% more than what's currently needed
            BLScratchBuffers.at(currentFrame) = std::make_shared<Buffer>(devicePtr, commandsPtr, newSize); //starting size really doesn't matter
            BLScratchBuffers.at(currentFrame)->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
        }
        if(BLBuildData.totalBuildSize > BLBuffers.at(currentFrame)->getAllocatedSize() || BLBuildData.totalBuildSize < BLBuffers.at(currentFrame)->getAllocatedSize() * 0.7)
        {
            VkDeviceSize newSize = BLBuildData.totalBuildSize * 1.1; //allocate 10% more than what's currently needed
            BLBuffers.at(currentFrame) = std::make_shared<Buffer>(devicePtr, commandsPtr, newSize); //starting size really doesn't matter
            BLBuffers.at(currentFrame)->createBuffer(VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
        }
        
        //TOP LEVEL
        instancesBufferSize = vectorModelInstances.size() * sizeof(VkAccelerationStructureInstanceKHR);
        if(instancesBufferSize > TLInstancesBuffers.at(currentFrame)->getAllocatedSize())
        {
            TLInstancesBuffers.at(currentFrame) = std::make_shared<Buffer>(devicePtr, commandsPtr, instancesBufferSize);
            TLInstancesBuffers.at(currentFrame)->createBuffer(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
        }
    }

    CommandBuffer AccelerationStructure::updateBLAS(const std::vector<SemaphorePair> &waitSemaphores, const std::vector<SemaphorePair> &signalSemaphores, const VkFence &fence, uint32_t currentFrame)
    {
        return createBottomLevel(waitSemaphores, signalSemaphores, fence, currentFrame);
    }

    CommandBuffer AccelerationStructure::updateTLAS(const std::vector<SemaphorePair> &waitSemaphores, const std::vector<SemaphorePair> &signalSemaphores, const VkFence &fence, uint32_t currentFrame)
    {
        return createTopLevel(waitSemaphores, signalSemaphores, fence, currentFrame);
    }

    CommandBuffer AccelerationStructure::createBottomLevel(const std::vector<SemaphorePair> &waitSemaphores, const std::vector<SemaphorePair> &signalSemaphores, const VkFence &fence, uint32_t currentFrame)
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

        VkCommandBuffer cmdBuffer = commandsPtr->getCommandBuffer(CmdPoolType::COMPUTE);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginInfo.pInheritanceInfo = NULL;
        
        vkBeginCommandBuffer(cmdBuffer, &beginInfo);
        
        //unordered map -> vector
        std::vector<VkAccelerationStructureBuildGeometryInfoKHR> vectorBuildGeos;
        for(const auto& [model, buildGeo] : BLBuildData.buildGeometries)
        {
            vectorBuildGeos.push_back(buildGeo);
        }
        vkCmdBuildAccelerationStructuresKHR(cmdBuffer, vectorBuildGeos.size(), vectorBuildGeos.data(), buildRangesPtrArray.data());
        
        vkEndCommandBuffer(cmdBuffer);

        std::vector<VkSemaphoreSubmitInfo> semaphoreWaitInfos;

        for(const SemaphorePair& pair : waitSemaphores)
        {
            VkSemaphoreSubmitInfo semaphoreInfo = {};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            semaphoreInfo.pNext = NULL;
            semaphoreInfo.semaphore = pair.semaphore;
            semaphoreInfo.stageMask = pair.stage;
            semaphoreInfo.deviceIndex = 0;

            semaphoreWaitInfos.push_back(semaphoreInfo);
        }

        VkCommandBufferSubmitInfo cmdBufferSubmitInfo = {};
        cmdBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdBufferSubmitInfo.pNext = NULL;
        cmdBufferSubmitInfo.commandBuffer = cmdBuffer;
        cmdBufferSubmitInfo.deviceMask = 0;

        std::vector<VkSemaphoreSubmitInfo> semaphoreSignalInfos;
        VkSemaphoreSubmitInfo semaphoreSignalInfo = {};
        semaphoreSignalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        semaphoreSignalInfo.pNext = NULL;
        semaphoreSignalInfo.semaphore = BottomSignalSemaphores.at(currentFrame);
        semaphoreSignalInfo.stageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        semaphoreSignalInfo.deviceIndex = 0;
        semaphoreSignalInfos.push_back(semaphoreSignalInfo);

        for(const SemaphorePair& pair : signalSemaphores)
        {
            VkSemaphoreSubmitInfo semaphoreInfo = {};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            semaphoreInfo.pNext = NULL;
            semaphoreInfo.semaphore = pair.semaphore;
            semaphoreInfo.stageMask = pair.stage;
            semaphoreInfo.deviceIndex = 0;

            semaphoreSignalInfos.push_back(semaphoreInfo);
        }
        
        VkSubmitInfo2 submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.pNext = NULL;
        submitInfo.flags = 0;
        submitInfo.waitSemaphoreInfoCount = semaphoreWaitInfos.size();
        submitInfo.pWaitSemaphoreInfos = semaphoreWaitInfos.data();
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &cmdBufferSubmitInfo;
        submitInfo.signalSemaphoreInfoCount = semaphoreSignalInfos.size();
        submitInfo.pSignalSemaphoreInfos = semaphoreSignalInfos.data();

        vkQueueSubmit2(devicePtr->getQueues().compute.at(0), 1, &submitInfo, fence);

        for(auto& ptr : buildRangesPtrArray)
        {
            free(ptr);
        }

        return { cmdBuffer, COMPUTE };
    }

    CommandBuffer AccelerationStructure::createTopLevel(const std::vector<SemaphorePair> &waitSemaphores, const std::vector<SemaphorePair>& signalSemaphores, const VkFence& fence, uint32_t currentFrame)
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
        if(buildSizes.buildScratchSize > TLScratchBuffers.at(currentFrame)->getAllocatedSize() || buildSizes.buildScratchSize < TLScratchBuffers.at(currentFrame)->getAllocatedSize() * 0.7)
        {
            VkDeviceSize newSize = buildSizes.buildScratchSize * 1.1; //allocate 10% more than what's currently needed
            TLScratchBuffers.at(currentFrame) = std::make_shared<Buffer>(devicePtr, commandsPtr, newSize); //starting size really doesn't matter
            TLScratchBuffers.at(currentFrame)->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
        }
        if(buildSizes.accelerationStructureSize > TLBuffers.at(currentFrame)->getAllocatedSize() || buildSizes.accelerationStructureSize < TLBuffers.at(currentFrame)->getAllocatedSize() * 0.7)
        {
            VkDeviceSize newSize = buildSizes.accelerationStructureSize * 1.1; //allocate 10% more than what's currently needed
            TLBuffers.at(currentFrame) = std::make_shared<Buffer>(devicePtr, commandsPtr, newSize); //starting size really doesn't matter
            TLBuffers.at(currentFrame)->createBuffer(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
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

        VkCommandBuffer cmdBuffer = commandsPtr->getCommandBuffer(CmdPoolType::COMPUTE);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginInfo.pInheritanceInfo = NULL;
        
        vkBeginCommandBuffer(cmdBuffer, &beginInfo);
        
        vkCmdBuildAccelerationStructuresKHR(cmdBuffer, 1, &buildGeoInfo, buildRangesPtrArray.data());
        
        vkEndCommandBuffer(cmdBuffer);

        std::vector<VkSemaphoreSubmitInfo> semaphoreWaitInfos;
        VkSemaphoreSubmitInfo semaphoreWaitInfo = {};
        semaphoreWaitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        semaphoreWaitInfo.pNext = NULL;
        semaphoreWaitInfo.semaphore = BottomSignalSemaphores.at(currentFrame);
        semaphoreWaitInfo.stageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        semaphoreWaitInfo.deviceIndex = 0;
        semaphoreWaitInfos.push_back(semaphoreWaitInfo);

        for(const SemaphorePair& pair : waitSemaphores)
        {
            VkSemaphoreSubmitInfo semaphoreInfo = {};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            semaphoreInfo.pNext = NULL;
            semaphoreInfo.semaphore = pair.semaphore;
            semaphoreInfo.stageMask = pair.stage;
            semaphoreInfo.deviceIndex = 0;

            semaphoreWaitInfos.push_back(semaphoreInfo);
        }

        VkCommandBufferSubmitInfo cmdBufferSubmitInfo = {};
        cmdBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdBufferSubmitInfo.pNext = NULL;
        cmdBufferSubmitInfo.commandBuffer = cmdBuffer;
        cmdBufferSubmitInfo.deviceMask = 0;

        std::vector<VkSemaphoreSubmitInfo> semaphoreSignalInfos;
        for(const SemaphorePair& pair : signalSemaphores)
        {
            VkSemaphoreSubmitInfo semaphoreInfo = {};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            semaphoreInfo.pNext = NULL;
            semaphoreInfo.semaphore = pair.semaphore;
            semaphoreInfo.stageMask = pair.stage;
            semaphoreInfo.deviceIndex = 0;

            semaphoreSignalInfos.push_back(semaphoreInfo);
        }
        
        VkSubmitInfo2 submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.pNext = NULL;
        submitInfo.flags = 0;
        submitInfo.waitSemaphoreInfoCount = semaphoreWaitInfos.size();
        submitInfo.pWaitSemaphoreInfos = semaphoreWaitInfos.data();
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &cmdBufferSubmitInfo;
        submitInfo.signalSemaphoreInfoCount = semaphoreSignalInfos.size();
        submitInfo.pSignalSemaphoreInfos = semaphoreSignalInfos.data();

        vkQueueSubmit2(devicePtr->getQueues().compute.at(0), 1, &submitInfo, fence);

        //get top address
        VkAccelerationStructureDeviceAddressInfoKHR accelerationAddressInfo{};
        accelerationAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        accelerationAddressInfo.accelerationStructure = topStructure;
        topStructureAddress = vkGetAccelerationStructureDeviceAddressKHR(devicePtr->getDevice(), &accelerationAddressInfo);

        return { cmdBuffer, COMPUTE };
    }
}
