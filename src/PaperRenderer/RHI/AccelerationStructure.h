#pragma once
#include "Device.h"

#include <unordered_map>

namespace PaperRenderer
{
    struct BottomStructure
    {
        VkAccelerationStructureKHR structure;
        VkDeviceAddress bufferAddress;
    };

    struct AccelerationStructureSynchronizatioInfo
    {
        std::vector<PaperMemory::SemaphorePair> waitSemaphores;
        std::vector<PaperMemory::SemaphorePair> TLSignalSemaphores;
    };

    class AccelerationStructure
    {
    private:

        std::unique_ptr<PaperMemory::DeviceAllocation> ASAllocation; //one shared allocation for the BLs and TL
        std::unique_ptr<PaperMemory::Buffer> BLBuffer;
        std::unique_ptr<PaperMemory::Buffer> BLScratchBuffer;
        std::unique_ptr<PaperMemory::Buffer> TLInstancesBuffer;
        std::unique_ptr<PaperMemory::Buffer> TLBuffer;
        std::unique_ptr<PaperMemory::Buffer> TLScratchBuffer;

        VkAccelerationStructureKHR topStructure;
        std::unordered_map<class Model const*, BottomStructure> bottomStructures;
        std::vector<class ModelInstance*> accelerationStructureInstances;
        VkSemaphore blasSignalSemaphore;
        bool isBuilt = false;

        VkDeviceSize instancesBufferSize;
        uint32_t instancesCount;
        struct BottomBuildData
        {
            std::vector<std::vector<VkAccelerationStructureGeometryKHR>> modelsGeometries;
            std::vector<std::vector<VkAccelerationStructureBuildRangeInfoKHR>> buildRangeInfos;
            std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildGeometries;
            std::vector<VkAccelerationStructureBuildSizesInfoKHR> buildSizes;
            VkDeviceSize totalScratchSize = 0;
            std::vector<VkDeviceSize> scratchOffsets;
            VkDeviceSize totalBuildSize = 0;
            std::vector<VkDeviceSize> asOffsets;
        };

        struct TopBuildData
        {
            VkAccelerationStructureGeometryKHR structureGeometry = {};
            VkAccelerationStructureBuildGeometryInfoKHR buildGeoInfo = {};
            VkAccelerationStructureBuildSizesInfoKHR buildSizes = {};
        };

        struct BuildData
        {
            BottomBuildData bottomData;
            TopBuildData topData;
        };

        class RenderEngine* rendererPtr;

        BuildData getBuildData();
        void rebuildAllocation();
        void createBottomLevel(BottomBuildData buildData, const PaperMemory::SynchronizationInfo& synchronizationInfo);
        void createTopLevel(TopBuildData buildData, const PaperMemory::SynchronizationInfo& synchronizationInfo);

    public:
        AccelerationStructure(RenderEngine* renderer);
        ~AccelerationStructure();

        void updateAccelerationStructures(const AccelerationStructureSynchronizatioInfo& syncInfo);

        void addInstance(class ModelInstance* instance);
        void removeInstance(class ModelInstance* instance);

        const VkAccelerationStructureKHR& getTLAS() const { return topStructure; }
        const std::unordered_map<class Model const*, BottomStructure>& getBottomStructures() const { return bottomStructures; }
    };
}