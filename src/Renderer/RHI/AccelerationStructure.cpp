#include "AccelerationStructure.h"

namespace Renderer
{
    AccelerationStructure::AccelerationStructure(Device *device, CmdBufferAllocator *commands)
        :devicePtr(device),
        commandsPtr(commands),
        //bottomStructure(VK_NULL_HANDLE),
        topStructure(VK_NULL_HANDLE)
    {
    }

    AccelerationStructure::~AccelerationStructure()
    {
        vkDestroyAccelerationStructureKHR(devicePtr->getDevice(), topStructure, nullptr);
        for(auto& [ptr, structure] : bottomStructures)
        {
            vkDestroyAccelerationStructureKHR(devicePtr->getDevice(), structure.structure, nullptr);
        }
        bottomStructures.clear();
    }

    void AccelerationStructure::createBottomLevel(const BottomAccelerationStructureData& meshesData)
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
        QueueReturn queueReturnData = transformMatrixBuffer.copyFromBuffer(transformationMatrixStaging, true);*/

        //clear previous structure
        for(auto& [ptr, structure] : bottomStructures)
        {
            vkDestroyAccelerationStructureKHR(devicePtr->getDevice(), structure.structure, nullptr);
        }
        bottomStructures.clear();

        //setup bottom level geometries
        std::vector<std::shared_ptr<std::vector<VkAccelerationStructureGeometryKHR>>> modelsGeometries;
        std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildGeometries;
        std::vector<std::vector<VkAccelerationStructureBuildRangeInfoKHR>> buildRangeInfos;
        std::vector<std::shared_ptr<Buffer>> buffers;

        //build per-model bottom level acceleration structures
        for(const AccelerationStructureModelReference& model : meshesData.models)
        {
            std::shared_ptr<std::vector<VkAccelerationStructureGeometryKHR>> modelGeometries = 
                std::make_shared<std::vector<VkAccelerationStructureGeometryKHR>>();
            std::vector<uint32_t> modelPrimitiveCounts;
            std::vector<VkAccelerationStructureBuildRangeInfoKHR> modelBuildRangeInfos;

            //get per-mesh geometry data
            for(ModelMesh const* mesh : model.meshes)
            {
                //get buffer addresses
                VkDeviceOrHostAddressConstKHR vertexBufferAddress = {};
                VkDeviceOrHostAddressConstKHR indexBufferAddress = {};
                //VkDeviceOrHostAddressConstKHR transformMatrixBufferAddress = {};

                vertexBufferAddress.deviceAddress = mesh->mesh->getVertexBuffer().getBufferDeviceAddress();
                indexBufferAddress.deviceAddress = mesh->mesh->getIndexBuffer().getBufferDeviceAddress();
                //transformMatrixBufferAddress.deviceAddress = transformMatrixBuffer.getBufferDeviceAddress();

                //buffer information
                VkAccelerationStructureGeometryTrianglesDataKHR trianglesGeometry = {};
                trianglesGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                trianglesGeometry.pNext = NULL;
                trianglesGeometry.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
                trianglesGeometry.vertexData = vertexBufferAddress;
                trianglesGeometry.maxVertex = mesh->mesh->getVertexBuffer().getLength();
                trianglesGeometry.vertexStride = sizeof(Vertex);
                trianglesGeometry.indexType = VK_INDEX_TYPE_UINT32;
                trianglesGeometry.indexData = indexBufferAddress;
                //trianglesGeometry.transformData = transformMatrixBufferAddress;

                //geometries
                VkAccelerationStructureGeometryKHR structureGeometry = {};
                structureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
                structureGeometry.pNext = NULL;
                structureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
                structureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                structureGeometry.geometry.triangles = trianglesGeometry;
                
                VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo;
                buildRangeInfo.primitiveCount = mesh->mesh->getIndexBuffer().getLength() / 3;
                buildRangeInfo.primitiveOffset = 0;
                buildRangeInfo.firstVertex = 0;
                buildRangeInfo.transformOffset = 0;

                modelGeometries->push_back(structureGeometry);
                modelPrimitiveCounts.push_back(mesh->mesh->getIndexBuffer().getLength() / 3);
                modelBuildRangeInfos.push_back(buildRangeInfo);
            }
            modelsGeometries.push_back(modelGeometries);
            buildRangeInfos.push_back(modelBuildRangeInfos);
            
            //per model build information
            VkAccelerationStructureBuildGeometryInfoKHR buildGeoInfo = {};
            buildGeoInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            buildGeoInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            buildGeoInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            buildGeoInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            buildGeoInfo.srcAccelerationStructure = VK_NULL_HANDLE;
            buildGeoInfo.geometryCount = modelGeometries->size();
            buildGeoInfo.pGeometries = modelGeometries->data();
            buildGeoInfo.ppGeometries = NULL;

            VkAccelerationStructureBuildSizesInfoKHR buildSizes = {};
            buildSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

            vkGetAccelerationStructureBuildSizesKHR(
                devicePtr->getDevice(),
                VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                &buildGeoInfo,
                modelPrimitiveCounts.data(),
                &buildSizes);

            //scratch buffer
            std::shared_ptr<Buffer> scratchBuffer = std::make_shared<Buffer>(
                devicePtr, commandsPtr, buildSizes.buildScratchSize
            );
            scratchBuffer->createBuffer(
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VMA_MEMORY_USAGE_AUTO,
                VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
            buffers.push_back(scratchBuffer);

            buildGeoInfo.scratchData.deviceAddress = scratchBuffer->getBufferDeviceAddress();

            //acceleration structure
            BottomStructure structure;
            structure.structure = VK_NULL_HANDLE;
            structure.structureBuffer = std::make_shared<Buffer>(
                devicePtr, commandsPtr, buildSizes.accelerationStructureSize
            );
            structure.structureBuffer->createBuffer(
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VMA_MEMORY_USAGE_AUTO,
                VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);

            VkAccelerationStructureCreateInfoKHR accelStructureInfo = {};
            accelStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
            accelStructureInfo.pNext = NULL;
            accelStructureInfo.createFlags = 0;
            accelStructureInfo.buffer = structure.structureBuffer->getBuffer();
            accelStructureInfo.offset = 0;
            accelStructureInfo.size = buildSizes.accelerationStructureSize;
            accelStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

            vkCreateAccelerationStructureKHR(devicePtr->getDevice(), &accelStructureInfo, nullptr, &structure.structure);
            buildGeoInfo.dstAccelerationStructure = structure.structure;
            
            buildGeometries.push_back(buildGeoInfo);
            bottomStructures[model.modelPointer] = structure;
        }

        //build process
        std::vector<VkAccelerationStructureBuildRangeInfoKHR*> buildRangesPtrArray;
        for(auto buildRanges : buildRangeInfos)
        {
            std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangesArray;
            for(auto buildRange : buildRanges)
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
        
        vkCmdBuildAccelerationStructuresKHR(cmdBuffer, buildGeometries.size(), buildGeometries.data(), buildRangesPtrArray.data());
        
        vkEndCommandBuffer(cmdBuffer);

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = NULL;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuffer;

        commandsPtr->submitQueue(submitInfo, CmdPoolType::COMPUTE, true);

        for(auto& ptr : buildRangesPtrArray)
        {
            free(ptr);
        }
    }

    QueueReturn AccelerationStructure::createTopLevel(const TopAccelerationData& instancesData)
    {
        //destroy old
        vkDestroyAccelerationStructureKHR(devicePtr->getDevice(), topStructure, nullptr);

        //setup top level geometries
        std::vector<VkAccelerationStructureInstanceKHR> structureInstances;
        for(const TopAccelerationInstance& instance : instancesData.instances)
        {
            VkAccelerationStructureInstanceKHR structureInstance = {};
            structureInstance.transform = instance.transform;
            structureInstance.instanceCustomIndex = 0;
            structureInstance.mask = 0xFF;
            structureInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            structureInstance.accelerationStructureReference = bottomStructures.at(instance.modelPointer).structureBuffer->getBufferDeviceAddress();
        }

        //create buffers
        const VkBufferUsageFlags bufferUsageFlags = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        StagingBuffer instancesStaging(devicePtr, commandsPtr, sizeof(VkAccelerationStructureInstanceKHR) * structureInstances.size());
        instancesStaging.mapData(structureInstances.data(), 0, sizeof(VkAccelerationStructureInstanceKHR) * structureInstances.size());

        Buffer instancesBuffer(devicePtr, commandsPtr, sizeof(VkAccelerationStructureInstanceKHR) * structureInstances.size());
        instancesBuffer.createBuffer(bufferUsageFlags, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
        QueueReturn queueReturnData = instancesBuffer.copyFromBuffer(instancesStaging, false);

        //get buffer address
        VkDeviceOrHostAddressConstKHR instancesAddress;
        instancesAddress.deviceAddress = instancesBuffer.getBufferDeviceAddress();

        //geometries
        VkAccelerationStructureGeometryInstancesDataKHR geoInstances = {};
        geoInstances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geoInstances.pNext = NULL;
        geoInstances.arrayOfPointers = VK_FALSE;
        geoInstances.data = instancesAddress;

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

        const uint32_t primitiveCount = 1;

        VkAccelerationStructureBuildSizesInfoKHR buildSizes = {};
        buildSizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

        vkGetAccelerationStructureBuildSizesKHR(
            devicePtr->getDevice(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildGeoInfo,
            &primitiveCount,
            &buildSizes);

        // Create a buffer to hold the acceleration structure
        Buffer accelStructureBuffer(devicePtr, commandsPtr, buildSizes.accelerationStructureSize);
        accelStructureBuffer.createBuffer(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);

        VkAccelerationStructureCreateInfoKHR accelStructureInfo = {};
        accelStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        accelStructureInfo.pNext = NULL;
        accelStructureInfo.createFlags = 0;
        accelStructureInfo.buffer = accelStructureBuffer.getBuffer();
        accelStructureInfo.offset = 0;
        accelStructureInfo.size = buildSizes.accelerationStructureSize;
        accelStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

        vkCreateAccelerationStructureKHR(devicePtr->getDevice(), &accelStructureInfo, nullptr, &topStructure);
        buildGeoInfo.dstAccelerationStructure = topStructure;

        //build process
        //scratch buffer
        Buffer scratchBuffer(devicePtr, commandsPtr, buildSizes.buildScratchSize);
        scratchBuffer.createBuffer(
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);

        buildGeoInfo.scratchData.deviceAddress = scratchBuffer.getBufferDeviceAddress();

        VkAccelerationStructureBuildRangeInfoKHR buildRange;
        buildRange.primitiveCount = 1;
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

        
        VkSemaphore signalSemaphore = commandsPtr->getSemaphore();
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = NULL;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuffer;
        submitInfo.waitSemaphoreCount = queueReturnData.getSemaphores().size();
        submitInfo.pWaitSemaphores = queueReturnData.getSemaphores().data();
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &signalSemaphore;

        QueueReturn returnInfo = commandsPtr->submitQueue(submitInfo, CmdPoolType::COMPUTE, false);

        //get top address
        VkAccelerationStructureDeviceAddressInfoKHR accelerationAddressInfo{};
        accelerationAddressInfo.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        accelerationAddressInfo.accelerationStructure = topStructure;
        topStructureAddress = vkGetAccelerationStructureDeviceAddressKHR(devicePtr->getDevice(), &accelerationAddressInfo);

        return returnInfo;
    }
}
