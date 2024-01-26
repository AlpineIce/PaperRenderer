#pragma once
#include "Buffer.h"
#include "Command.h"

#include <unordered_map>

namespace Renderer
{
    struct AccelerationStructureModelReference
    {
        std::vector<ModelMesh const*> meshes;
        void* modelPointer = NULL;
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

    struct AccelerationData
    {
        int a = 0;
        //std::vector<BufferPair> bufferPairs;
    };

    class AccelerationStructure
    {
    private:

        VkAccelerationStructureKHR topStructure;
        VkDeviceAddress topStructureAddress;
        std::unordered_map<void const*, BottomStructure> bottomStructures;
        VkDeviceAddress bottomStructureAddress;
        bool isBuilt = false;

        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;

        void createTopLevel(const AccelerationData& instancesData);

    public:
        AccelerationStructure(Device* device, CmdBufferAllocator* commands);
        ~AccelerationStructure();

        void createBottomLevel(const BottomAccelerationStructureData& meshesData);
        void buildStructure(const AccelerationData& accelData);
    };
}