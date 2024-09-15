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
        VkAccelerationStructureKHR structure = VK_NULL_HANDLE;
        VkDeviceAddress bufferAddress = 0;
        std::list<class ModelInstance*> referencedInstances;
    };

    class AccelerationStructure
    {
    private:
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

        std::unique_ptr<Buffer> instancesBuffer;
        std::unique_ptr<Buffer> instanceDescriptionsBuffer;

        VkAccelerationStructureKHR topStructure = VK_NULL_HANDLE;
        std::unordered_map<class Model const*, BottomStructure> bottomStructures;
        std::list<class Model const*> blasBuildModels;
        std::vector<class ModelInstance*> accelerationStructureInstances;
        std::deque<class ModelInstance*> toUpdateInstances;
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
        } buildData;

        class RenderEngine* rendererPtr;

        const float instancesOverhead = 1.5;
        void setBuildData();
        void rebuildInstancesBuffers();
        void createBottomLevel(VkCommandBuffer cmdBuffer, BottomBuildData buildData);
        void createTopLevel(VkCommandBuffer cmdBuffer, TopBuildData buildData);
        

        friend TLASInstanceBuildPipeline;

    public:
        AccelerationStructure(RenderEngine* renderer);
        ~AccelerationStructure();

        void queueInstanceTransfers();
        void updateAccelerationStructures(SynchronizationInfo syncInfo);

        void addInstance(class ModelInstance* instance);
        void removeInstance(class ModelInstance* instance);

        const VkAccelerationStructureKHR& getTLAS() const { return topStructure; }
        Buffer const* getInstanceDescriptionsBuffer() const { return instanceDescriptionsBuffer.get(); }
        const std::unordered_map<class Model const*, BottomStructure>& getBottomStructures() const { return bottomStructures; }
    };
}