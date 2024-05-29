#pragma once
#include "Device.h"

#include <unordered_map>

namespace PaperRenderer
{
    class Model; //forward declaration
    class ModelInstance; //forward declaration

    struct RTInputData
    {
        
    };
    
    struct ShaderTLASInstance
    {

    };

    struct BottomStructure
    {
        VkAccelerationStructureKHR structure;
        VkDeviceAddress bufferAddress;
    };

    class AccelerationStructure
    {
    private:
        std::vector<std::unique_ptr<PaperMemory::DeviceAllocation>> ASAllocations0; //one shared allocation for the BL and TL instances
        std::vector<std::unique_ptr<PaperMemory::Buffer>> BLBuffers;
        std::vector<std::unique_ptr<PaperMemory::Buffer>> BLScratchBuffers;
        std::vector<std::unique_ptr<PaperMemory::Buffer>> TLInstancesBuffers;
        std::vector<std::unique_ptr<PaperMemory::DeviceAllocation>> ASAllocations1; //one shared allocation for the TL
        std::vector<std::unique_ptr<PaperMemory::Buffer>> TLBuffers;
        std::vector<std::unique_ptr<PaperMemory::Buffer>> TLScratchBuffers;

        VkAccelerationStructureKHR topStructure;
        VkDeviceAddress topStructureAddress;
        std::unordered_map<Model const*, BottomStructure> bottomStructures;
        std::vector<VkSemaphore> blasSignalSemaphores;
        bool isBuilt = false;

        VkDeviceSize instancesBufferSize;
        uint32_t instancesCount;
        struct BottomBuildData
        {
            std::vector<Model const*> buildModels;
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

        void verifyBufferSizes(const std::unordered_map<Model const*, std::vector<ModelInstance*>> &modelInstances, uint32_t currentFrame);
        void rebuildAllocations0(uint32_t currentFrame);
        void rebuildAllocations1(uint32_t currentFrame);
        PaperMemory::CommandBuffer createBottomLevel(const PaperMemory::SynchronizationInfo& synchronizationInfo, uint32_t currentFrame);
        PaperMemory::CommandBuffer createTopLevel(const PaperMemory::SynchronizationInfo& synchronizationInfo, uint32_t currentFrame);

    public:
        AccelerationStructure(Device* device);
        ~AccelerationStructure();

        PaperMemory::CommandBuffer updateBLAS(const std::unordered_map<Model const*, std::vector<ModelInstance*>> &modelInstances, const PaperMemory::SynchronizationInfo& synchronizationInfo, uint32_t currentFrame);
        PaperMemory::CommandBuffer updateTLAS(const PaperMemory::SynchronizationInfo& synchronizationInfo, uint32_t currentFrame);

        const std::unordered_map<Model const*, BottomStructure>& getBottomStructures() const { return bottomStructures; }
        VkDeviceAddress getTLASInstancesBufferAddress(uint32_t currentFrame) const;
    };
}