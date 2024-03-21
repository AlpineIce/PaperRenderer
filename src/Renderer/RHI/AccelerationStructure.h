#pragma once
#include "Buffer.h"
#include "Command.h"

#include <unordered_map>

namespace Renderer
{
    class Model; //forward declaration
    class ModelInstance; //forward declaration

    struct BottomStructure
    {
        VkAccelerationStructureKHR structure;
        VkDeviceAddress bufferAddress;
    };

    class AccelerationStructure
    {
    private:
        std::vector<std::shared_ptr<Buffer>> BLBuffers;
        std::vector<std::shared_ptr<Buffer>> BLScratchBuffers;
        std::vector<std::shared_ptr<Buffer>> TLInstancesBuffers;
        std::vector<std::shared_ptr<Buffer>> TLBuffers;
        std::vector<std::shared_ptr<Buffer>> TLScratchBuffers;

        VkAccelerationStructureKHR topStructure;
        VkDeviceAddress topStructureAddress;
        std::unordered_map<Model const*, BottomStructure> bottomStructures;
        std::vector<VkSemaphore> BottomSignalSemaphores;
        bool isBuilt = false;

        VkDeviceSize instancesBufferSize;
        uint32_t instancesCount;
        struct BottomBuildData
        {
            std::vector<Model*> buildModels;
            std::vector<std::vector<VkAccelerationStructureGeometryKHR>> modelsGeometries;
            std::vector<std::vector<VkAccelerationStructureBuildRangeInfoKHR>> buildRangeInfos;
            std::unordered_map<Model const*, VkAccelerationStructureBuildGeometryInfoKHR> buildGeometries;
            std::vector<VkAccelerationStructureBuildSizesInfoKHR> buildSizes;
            VkDeviceSize totalScratchSize = 0;
            std::vector<VkDeviceSize> scratchOffsets;
            VkDeviceSize totalBuildSize = 0;
            std::vector<VkDeviceSize> asOffsets;
        } BLBuildData;

        

        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;

        CommandBuffer createBottomLevel(const std::vector<SemaphorePair> &waitSemaphores, const std::vector<SemaphorePair> &signalSemaphores, const VkFence &fence, uint32_t currentFrame);
        CommandBuffer createTopLevel(const std::vector<SemaphorePair> &waitSemaphores, const std::vector<SemaphorePair>& signalSemaphores, const VkFence& fence, uint32_t currentFrame);

    public:
        AccelerationStructure(Device* device, CmdBufferAllocator* commands);
        ~AccelerationStructure();

        void verifyBufferSizes(const std::unordered_map<Model*, std::list<ModelInstance*>> &modelInstances, uint32_t currentFrame);
        CommandBuffer updateBLAS(const std::vector<SemaphorePair>& waitSemaphores, 
            const std::vector<SemaphorePair>& signalSemaphores, 
            const VkFence& fence,
            uint32_t currentFrame);
        CommandBuffer updateTLAS(const std::vector<SemaphorePair>& waitSemaphores, 
            const std::vector<SemaphorePair>& signalSemaphores, 
            const VkFence& fence,
            uint32_t currentFrame);

        const std::vector<std::shared_ptr<Buffer>>& getInstancesBuffers() const { return TLInstancesBuffers; }
        const std::unordered_map<Model const*, BottomStructure>& getBottomStructures() const { return bottomStructures; }
    };
}