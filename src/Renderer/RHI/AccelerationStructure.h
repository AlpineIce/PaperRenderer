#pragma once
#include "Buffer.h"
#include "Command.h"

#include <unordered_map>

namespace Renderer
{
    struct AccelerationStructureMeshData
    {
        VkDeviceOrHostAddressConstKHR vertexBufferAddress = {};
        VkDeviceOrHostAddressConstKHR indexBufferAddress = {};
        uint32_t vertexCount = 0;
        uint32_t primitiveCount = 0;
    };

    struct AccelerationStructureModelReference
    {
        std::vector<AccelerationStructureMeshData> meshes;
        void const* modelPointer = NULL;
    };

    struct BottomStructure
    {
        VkAccelerationStructureKHR structure;
        std::shared_ptr<Buffer> structureBuffer;
    };

    struct BottomAccelerationStructureData
    {
        std::vector<AccelerationStructureModelReference> models;
    };

    struct TopAccelerationInstance
    {
        void const* modelPointer = NULL;
        VkTransformMatrixKHR transform;
    };

    struct TopAccelerationData
    {
        std::vector<TopAccelerationInstance> instances;
    };

    class AccelerationStructure
    {
    private:

        VkAccelerationStructureKHR topStructure;
        VkDeviceAddress topStructureAddress;
        std::unordered_map<void const*, BottomStructure> bottomStructures;
        bool isBuilt = false;

        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;

        

    public:
        AccelerationStructure(Device* device, CmdBufferAllocator* commands);
        ~AccelerationStructure();

        void createBottomLevel(const BottomAccelerationStructureData& meshesData);
        VkSemaphore createTopLevel(const TopAccelerationData& instancesData, const std::vector<SemaphorePair>& waitSemaphores);
    };
}