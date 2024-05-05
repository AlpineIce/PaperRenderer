#pragma once
#include "Device.h"

#include <unordered_map>

namespace PaperRenderer
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
        std::vector<std::unique_ptr<PaperMemory::DeviceAllocation>> ASBuffersAllocation; //one shared allocation for all buffers in the AS
        std::vector<std::unique_ptr<PaperMemory::Buffer>> BLBuffers;
        std::vector<std::unique_ptr<PaperMemory::Buffer>> BLScratchBuffers;
        std::vector<std::unique_ptr<PaperMemory::Buffer>> TLInstancesBuffers;
        std::vector<std::unique_ptr<PaperMemory::Buffer>> TLBuffers;
        std::vector<std::unique_ptr<PaperMemory::Buffer>> TLScratchBuffers;

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

        PaperMemory::CommandBuffer createBottomLevel(const PaperMemory::SynchronizationInfo& synchronizationInfo, uint32_t currentFrame);
        PaperMemory::CommandBuffer createTopLevel(const PaperMemory::SynchronizationInfo& synchronizationInfo, uint32_t currentFrame);

    public:
        AccelerationStructure(Device* device);
        ~AccelerationStructure();

        void verifyBufferSizes(const std::unordered_map<Model*, std::list<ModelInstance*>> &modelInstances, uint32_t currentFrame);
        PaperMemory::CommandBuffer updateBLAS(const PaperMemory::SynchronizationInfo& synchronizationInfo, uint32_t currentFrame);
        PaperMemory::CommandBuffer updateTLAS(const PaperMemory::SynchronizationInfo& synchronizationInfo, uint32_t currentFrame);

        const std::unordered_map<Model const*, BottomStructure>& getBottomStructures() const { return bottomStructures; }
    };
}