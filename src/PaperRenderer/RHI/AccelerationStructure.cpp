#include "AccelerationStructure.h"
#include "../PaperRenderer.h"
#include "../Model.h"

namespace PaperRenderer
{
    AccelerationStructure::AccelerationStructure(RenderEngine* renderer)
        :rendererPtr(renderer),
        topStructure(VK_NULL_HANDLE)
    {
        PaperMemory::BufferInfo bufferInfo = {};
        bufferInfo.size = 256; //arbitrary starting size
        bufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
        
        bufferInfo.usageFlags =    VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        BLBuffer =          std::make_unique<PaperMemory::Buffer>(rendererPtr->getDevice()->getDevice(), bufferInfo);
        bufferInfo.usageFlags =    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        BLScratchBuffer =   std::make_unique<PaperMemory::Buffer>(rendererPtr->getDevice()->getDevice(), bufferInfo);
        bufferInfo.usageFlags =    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        TLInstancesBuffer = std::make_unique<PaperMemory::Buffer>(rendererPtr->getDevice()->getDevice(), bufferInfo);
        bufferInfo.usageFlags =    VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        TLBuffer =          std::make_unique<PaperMemory::Buffer>(rendererPtr->getDevice()->getDevice(), bufferInfo);
        bufferInfo.usageFlags =    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        TLScratchBuffer =   std::make_unique<PaperMemory::Buffer>(rendererPtr->getDevice()->getDevice(), bufferInfo);

        //synchronization things
        blasSignalSemaphore = PaperMemory::Commands::getSemaphore(rendererPtr->getDevice()->getDevice());

        rebuildAllocation();
    }

    AccelerationStructure::~AccelerationStructure()
    {
        vkDestroySemaphore(rendererPtr->getDevice()->getDevice(), blasSignalSemaphore, nullptr);

        vkDestroyAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), topStructure, nullptr);
        for(auto& [ptr, structure] : bottomStructures)
        {
            vkDestroyAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), structure.structure, nullptr);
        }
        bottomStructures.clear();
    }

    AccelerationStructure::BuildData AccelerationStructure::getBuildData()
    {
        BottomBuildData BLBuildData = {};
        TopBuildData TLBuildData = {};

        //setup bottom level geometries
        for(Model const* model : rendererPtr->getModelReferences())
        {
            std::vector<VkAccelerationStructureGeometryKHR> modelGeometries;
            std::vector<uint32_t> modelPrimitiveCounts;
            std::vector<VkAccelerationStructureBuildRangeInfoKHR> modelBuildRangeInfos;

            //get per mesh group geometry data
            for(const std::vector<LODMesh> meshes : model->getLODs().at(0).meshMaterialData) //use LOD 0 for BLAS
            {
                for(const LODMesh& mesh : meshes) //per mesh in mesh group data
                {
                    //buffer information
                    VkAccelerationStructureGeometryTrianglesDataKHR trianglesGeometry = {};
                    trianglesGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                    trianglesGeometry.pNext = NULL;
                    trianglesGeometry.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
                    trianglesGeometry.vertexData = VkDeviceOrHostAddressConstKHR{.deviceAddress = model->getVBOAddress()};
                    trianglesGeometry.maxVertex = mesh.vertexCount;
                    trianglesGeometry.vertexStride = mesh.vertexDescription.stride;
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
                    buildRangeInfo.primitiveCount = mesh.indexCount / 3;
                    buildRangeInfo.primitiveOffset = mesh.iboOffset * sizeof(uint32_t);
                    buildRangeInfo.firstVertex = mesh.vboOffset;
                    buildRangeInfo.transformOffset = 0;

                    modelGeometries.push_back(structureGeometry);
                    modelPrimitiveCounts.push_back(mesh.indexCount / 3);
                    modelBuildRangeInfos.push_back(buildRangeInfo);
                }
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
        instancesCount = rendererPtr->getModelReferences().size();
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

        //----------CHECK IF ALLOCATION REBUILD IS NEEDED----------//

        bool rebuildFlag = false;
        if(BLBuildData.totalBuildSize > BLBuffer->getSize() || BLBuildData.totalBuildSize < BLBuffer->getSize() * 0.7)
        {
            rebuildFlag = true;
        }
        if(BLBuildData.totalScratchSize > BLScratchBuffer->getSize() || BLBuildData.totalScratchSize < BLScratchBuffer->getSize() * 0.7)
        {
            rebuildFlag = true;
        }
        if(instancesBufferSize > TLInstancesBuffer->getSize() || instancesBufferSize < TLInstancesBuffer->getSize() * 0.7)
        {
            rebuildFlag = true;
        }
        if(TLBuildSizes.buildScratchSize > TLScratchBuffer->getSize() || TLBuildSizes.buildScratchSize < TLScratchBuffer->getSize() * 0.7)
        {
            rebuildFlag = true;
        }
        if(TLBuildSizes.accelerationStructureSize > TLBuffer->getSize() || TLBuildSizes.accelerationStructureSize < TLBuffer->getSize() * 0.7)
        {
            rebuildFlag = true;
        }

        //rebuild all buffers with new size and allocation if needed
        if(rebuildFlag)
        {
            //BL buffers
            PaperMemory::BufferInfo bufferInfo0 = {};
            bufferInfo0.size = BLBuildData.totalBuildSize * 1.2; //allocate 20% more than what's currently needed
            bufferInfo0.usageFlags = VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            bufferInfo0.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
            BLBuffer = std::make_unique<PaperMemory::Buffer>(rendererPtr->getDevice()->getDevice(), bufferInfo0);

            //BL scratch
            PaperMemory::BufferInfo bufferInfo1 = {};
            bufferInfo1.size = PaperMemory::DeviceAllocation::padToMultiple(BLBuildData.totalScratchSize * 1.2, rendererPtr->getDevice()->getASproperties().minAccelerationStructureScratchOffsetAlignment);
            bufferInfo1.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            bufferInfo1.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
            BLScratchBuffer = std::make_unique<PaperMemory::Buffer>(rendererPtr->getDevice()->getDevice(), bufferInfo1);

            //TL instances
            PaperMemory::BufferInfo bufferInfo2 = {};
            bufferInfo2.size = instancesBufferSize * 1.2; //allocate 20% more than what's currently needed
            bufferInfo2.usageFlags = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufferInfo2.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
            TLInstancesBuffer = std::make_unique<PaperMemory::Buffer>(rendererPtr->getDevice()->getDevice(), bufferInfo2);

            //TL scratch buffers
            PaperMemory::BufferInfo bufferInfo3 = {};
            bufferInfo3.size = PaperMemory::DeviceAllocation::padToMultiple(TLBuildSizes.buildScratchSize * 1.2, rendererPtr->getDevice()->getASproperties().minAccelerationStructureScratchOffsetAlignment);
            bufferInfo3.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            bufferInfo3.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
            TLScratchBuffer = std::make_unique<PaperMemory::Buffer>(rendererPtr->getDevice()->getDevice(), bufferInfo3);

            //TL buffers
            PaperMemory::BufferInfo bufferInfo4 = {};
            bufferInfo4.size = TLBuildSizes.accelerationStructureSize * 1.2; //allocate 20% more than what's currently needed
            bufferInfo4.usageFlags = VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
            bufferInfo4.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
            TLBuffer = std::make_unique<PaperMemory::Buffer>(rendererPtr->getDevice()->getDevice(), bufferInfo4);

            rebuildAllocation();

            TLBuildData.structureGeometry.geometry.instances.data = VkDeviceOrHostAddressConstKHR{.deviceAddress = TLInstancesBuffer->getBufferDeviceAddress()};
        }

        return { std::move(BLBuildData), std::move(TLBuildData) };
    }

    void AccelerationStructure::rebuildAllocation()
    {
        //find new size
        VkDeviceSize newSize = 0;
        newSize += PaperMemory::DeviceAllocation::padToMultiple(BLBuffer->getMemoryRequirements().size, BLScratchBuffer->getMemoryRequirements().alignment);
        newSize += PaperMemory::DeviceAllocation::padToMultiple(BLScratchBuffer->getMemoryRequirements().size, TLInstancesBuffer->getMemoryRequirements().alignment);
        newSize += PaperMemory::DeviceAllocation::padToMultiple(TLInstancesBuffer->getMemoryRequirements().size, TLBuffer->getMemoryRequirements().alignment);
        newSize += PaperMemory::DeviceAllocation::padToMultiple(TLBuffer->getMemoryRequirements().size, TLScratchBuffer->getMemoryRequirements().alignment);
        newSize += TLScratchBuffer->getMemoryRequirements().size;

        //rebuild allocation (no need for copying since the buffer data changes every frame by the compute shader)
        PaperMemory::DeviceAllocationInfo allocInfo = {};
        allocInfo.allocationSize = newSize;
        allocInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        allocInfo.allocFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        ASAllocation = std::make_unique<PaperMemory::DeviceAllocation>(rendererPtr->getDevice()->getDevice(), rendererPtr->getDevice()->getGPU(), allocInfo);

        //assign allocation to buffers
        int errorCheck = 0;
        errorCheck += BLScratchBuffer->assignAllocation(ASAllocation.get());
        errorCheck += TLScratchBuffer->assignAllocation(ASAllocation.get());
        errorCheck += BLBuffer->assignAllocation(ASAllocation.get());
        errorCheck += TLInstancesBuffer->assignAllocation(ASAllocation.get());
        errorCheck += TLBuffer->assignAllocation(ASAllocation.get());

        if(errorCheck != 0)
        {
            throw std::runtime_error("Acceleration Structure allocation rebuild failed"); //programmer error
        }
    }

    void AccelerationStructure::updateAccelerationStructures(const AccelerationStructureSynchronizatioInfo& syncInfo)
    {
        const BuildData buildData = getBuildData();

        PaperMemory::SynchronizationInfo blSyncInfo = {};
        blSyncInfo.queueType = PaperMemory::QueueType::COMPUTE;
        blSyncInfo.waitPairs = {};
        blSyncInfo.signalPairs = { { blasSignalSemaphore, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR } };
        blSyncInfo.fence = VK_NULL_HANDLE;
        createBottomLevel(buildData.bottomData, blSyncInfo);

        PaperMemory::SynchronizationInfo tlSyncInfo = {};
        tlSyncInfo.queueType = PaperMemory::QueueType::COMPUTE;
        tlSyncInfo.waitPairs = { { blasSignalSemaphore, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR } };
        tlSyncInfo.signalPairs = {};
        tlSyncInfo.fence = VK_NULL_HANDLE;
        createTopLevel(buildData.topData, tlSyncInfo);
    }

    void AccelerationStructure::createBottomLevel(BottomBuildData buildData, const PaperMemory::SynchronizationInfo &synchronizationInfo)
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
            vkDestroyAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), structure.structure, nullptr);
        }
        bottomStructures.clear();

        //create BLASes (funny grammar moment)
        uint32_t modelIndex = 0;
        for(Model const* model : rendererPtr->getModelReferences()) //important to note here that the iteration order is the exact same as the one for collecting the initial data
        {
            bottomStructures[model].structure = VK_NULL_HANDLE;
            bottomStructures[model].bufferAddress = BLBuffer->getBufferDeviceAddress() + buildData.asOffsets.at(modelIndex);
            buildData.buildGeometries.at(modelIndex).scratchData.deviceAddress = BLScratchBuffer->getBufferDeviceAddress() + buildData.scratchOffsets.at(modelIndex);

            VkAccelerationStructureCreateInfoKHR accelStructureInfo = {};
            accelStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
            accelStructureInfo.pNext = NULL;
            accelStructureInfo.createFlags = 0;
            accelStructureInfo.buffer = BLBuffer->getBuffer();
            accelStructureInfo.offset = buildData.asOffsets.at(modelIndex);
            accelStructureInfo.size = buildData.buildSizes.at(modelIndex).accelerationStructureSize;
            accelStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            
            vkCreateAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), &accelStructureInfo, nullptr, &bottomStructures[model].structure);
            buildData.buildGeometries.at(modelIndex).dstAccelerationStructure = bottomStructures[model].structure;

            modelIndex++;
        }
        
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

        //command buffer and BLAS build
        VkCommandBuffer cmdBuffer = PaperMemory::Commands::getCommandBuffer(rendererPtr->getDevice()->getDevice(), synchronizationInfo.queueType);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginInfo.pInheritanceInfo = NULL;
        
        vkBeginCommandBuffer(cmdBuffer, &beginInfo);
        vkCmdBuildAccelerationStructuresKHR(cmdBuffer, vectorBuildGeos.size(), vectorBuildGeos.data(), buildRangesPtrArray.data());
        vkEndCommandBuffer(cmdBuffer);

        PaperMemory::Commands::submitToQueue(rendererPtr->getDevice()->getDevice(), synchronizationInfo, { cmdBuffer });

        for(auto& ptr : buildRangesPtrArray)
        {
            free(ptr);
        }

        rendererPtr->recycleCommandBuffer({ cmdBuffer, synchronizationInfo.queueType });
    }

    void AccelerationStructure::createTopLevel(TopBuildData buildData, const PaperMemory::SynchronizationInfo& synchronizationInfo)
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
        buildData.buildGeoInfo.scratchData.deviceAddress = TLScratchBuffer->getBufferDeviceAddress();
        buildData.buildGeoInfo.dstAccelerationStructure = topStructure;

        VkAccelerationStructureBuildRangeInfoKHR buildRange;
        buildRange.primitiveCount = instancesCount;
        buildRange.primitiveOffset = 0;
        buildRange.firstVertex = 0;
        buildRange.transformOffset = 0;
        std::vector<VkAccelerationStructureBuildRangeInfoKHR*> buildRangesPtrArray = {&buildRange};

        VkCommandBuffer cmdBuffer = PaperMemory::Commands::getCommandBuffer(rendererPtr->getDevice()->getDevice(), synchronizationInfo.queueType);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = NULL;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginInfo.pInheritanceInfo = NULL;
        
        vkBeginCommandBuffer(cmdBuffer, &beginInfo);
        //TODO DISPATCH INSTANCE FILLING COMPUTE SHADER
        vkCmdBuildAccelerationStructuresKHR(cmdBuffer, 1, &buildData.buildGeoInfo, buildRangesPtrArray.data());
        vkEndCommandBuffer(cmdBuffer);

        PaperMemory::Commands::submitToQueue(rendererPtr->getDevice()->getDevice(), synchronizationInfo, { cmdBuffer });

        //get top address
        VkAccelerationStructureDeviceAddressInfoKHR accelerationAddressInfo{};
        accelerationAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        accelerationAddressInfo.accelerationStructure = topStructure;
        topStructureAddress = vkGetAccelerationStructureDeviceAddressKHR(rendererPtr->getDevice()->getDevice(), &accelerationAddressInfo);

        rendererPtr->recycleCommandBuffer({ cmdBuffer, synchronizationInfo.queueType });
    }

    void AccelerationStructure::addInstance(ModelInstance *instance)
    {
    }
    
    void AccelerationStructure::removeInstance(ModelInstance *instance)
    {
    }
}
