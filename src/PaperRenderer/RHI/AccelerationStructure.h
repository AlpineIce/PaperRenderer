#pragma once
#include "../ComputeShader.h"

#include <unordered_map>
#include <list>

namespace PaperRenderer
{
    //----------TLAS INSTANCE BUILD PIPELINE DECLARATIONS----------//

    class TLASInstanceBuildPipeline : public ComputeShader
    {
    private:
        std::string fileName = "TLASInstBuild.spv";
        std::vector<std::unique_ptr<PaperMemory::Buffer>> uniformBuffers;
        std::unique_ptr<PaperMemory::DeviceAllocation> uniformBuffersAllocation;

        struct UBOInputData
        {
            uint32_t objectCount;
        };

        class RenderEngine* rendererPtr;

    public:
        TLASInstanceBuildPipeline(RenderEngine* renderer, std::string fileDir);
        ~TLASInstanceBuildPipeline() override;

        void submit(const PaperMemory::SynchronizationInfo& syncInfo, const AccelerationStructure& accelerationStructure);
    };

    //----------ACCELERATION STRUCTURE DECLARATIONS----------//

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

        //instances buffers and allocations
        static std::unique_ptr<PaperMemory::DeviceAllocation> hostInstancesAllocation;
        static std::unique_ptr<PaperMemory::DeviceAllocation> deviceInstancesAllocation;
        std::unique_ptr<PaperMemory::Buffer> hostInstancesBuffer;
        std::unique_ptr<PaperMemory::Buffer> deviceInstancesBuffer;

        VkAccelerationStructureKHR topStructure = VK_NULL_HANDLE;
        std::unordered_map<class Model const*, BottomStructure> bottomStructures;
        std::vector<class ModelInstance*> accelerationStructureInstances;
        static std::list<AccelerationStructure*> accelerationStructures;
        VkSemaphore instancesCopySemaphore;
        VkSemaphore tlasInstanceBuildSignalSemaphore;
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

        const float instancesOverhead = 1.5;
        static void rebuildInstancesAllocationsAndBuffers(RenderEngine* renderer);
        void rebuildInstancesBuffers();
        BuildData getBuildData();
        void rebuildAllocation();
        void createBottomLevel(BottomBuildData buildData, const PaperMemory::SynchronizationInfo& synchronizationInfo);
        void createTopLevel(TopBuildData buildData, const PaperMemory::SynchronizationInfo& synchronizationInfo);

        friend TLASInstanceBuildPipeline;

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