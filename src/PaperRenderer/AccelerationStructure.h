#pragma once
#include "ComputeShader.h"

#include <unordered_map>
#include <list>

namespace PaperRenderer
{
    //----------TLAS INSTANCE BUILD PIPELINE DECLARATIONS----------//

    class TLASInstanceBuildPipeline : public ComputeShader
    {
    private:
        std::string fileName = "TLASInstBuild.spv";
        std::unique_ptr<Buffer> uniformBuffer;
        std::unique_ptr<DeviceAllocation> uniformBufferAllocation;

        struct UBOInputData
        {
            uint32_t objectCount;
        };

        class RenderEngine* rendererPtr;

    public:
        TLASInstanceBuildPipeline(RenderEngine* renderer, std::string fileDir);
        ~TLASInstanceBuildPipeline() override;

        void submit(VkCommandBuffer cmdBuffer, const AccelerationStructure& accelerationStructure);
    };

    //----------ACCELERATION STRUCTURE DECLARATIONS----------//

    struct BottomStructure
    {
        uint32_t referenceCount = 0;
        VkAccelerationStructureKHR structure = VK_NULL_HANDLE;
        VkDeviceAddress bufferAddress = 0;
    };

    class AccelerationStructure
    {
    private:
        std::unique_ptr<DeviceAllocation> scratchAllocation;
        std::unique_ptr<DeviceAllocation> BLASAllocation;
        std::unique_ptr<DeviceAllocation> TLASAllocation; //one shared allocation for the BLs and TL
        std::unique_ptr<Buffer> BLBuffer;
        std::unique_ptr<Buffer> TLInstancesBuffer;
        std::unique_ptr<Buffer> TLBuffer;
        std::unique_ptr<Buffer> scratchBuffer;

        //instances buffers and allocations
        struct InstanceDescription
        {
            uint64_t vertexAddress;
            uint64_t indexAddress;
            uint32_t modelDataOffset;
            uint32_t vertexStride;
            uint32_t indexStride;
        };

        static std::unique_ptr<DeviceAllocation> hostInstancesAllocation;
        static std::unique_ptr<DeviceAllocation> deviceInstancesAllocation;
        std::unique_ptr<Buffer> hostInstancesBuffer;
        std::unique_ptr<Buffer> hostInstanceDescriptionsBuffer;
        std::unique_ptr<Buffer> deviceInstancesBuffer;
        std::unique_ptr<Buffer> deviceInstanceDescriptionsBuffer;

        VkAccelerationStructureKHR topStructure = VK_NULL_HANDLE;
        std::unordered_map<class Model const*, BottomStructure> bottomStructures;
        std::list<class Model const*> blasBuildModels;
        std::vector<class ModelInstance*> accelerationStructureInstances;
        static std::list<AccelerationStructure*> accelerationStructures;
        std::mutex instanceAddRemoveMutex;

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
        BuildData getBuildData(VkCommandBuffer cmdBuffer);
        void rebuildBLASAllocation();
        void rebuildTLASAllocation();
        void rebuildScratchAllocation();
        void rebuildInstancesBuffers();
        static void rebuildInstancesAllocationsAndBuffers(RenderEngine* renderer);
        void createBottomLevel(VkCommandBuffer cmdBuffer, BottomBuildData buildData);
        void createTopLevel(VkCommandBuffer cmdBuffer, TopBuildData buildData);

        friend TLASInstanceBuildPipeline;

    public:
        AccelerationStructure(RenderEngine* renderer);
        ~AccelerationStructure();

        void updateAccelerationStructures(const SynchronizationInfo& syncInfo);

        void addInstance(class ModelInstance* instance);
        void removeInstance(class ModelInstance* instance);

        const VkAccelerationStructureKHR& getTLAS() const { return topStructure; }
        Buffer const* getInstanceDescriptionsBuffer() const { return deviceInstanceDescriptionsBuffer.get(); }
        const std::unordered_map<class Model const*, BottomStructure>& getBottomStructures() const { return bottomStructures; }
    };
}