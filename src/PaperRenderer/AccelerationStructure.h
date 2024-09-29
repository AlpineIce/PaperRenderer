#pragma once
#include "ComputeShader.h"

#include <queue>

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

        void submit(VkCommandBuffer cmdBuffer, const TLAS& tlas);
    };

    //----------ACCELERATION STRUCTURE DECLARATIONS----------//

    enum BlasUpdateMode
    {
        UPDATE, //faster, but less optimal ray tracing performance
        REBUILD //slower, but more optimal ray tracing performance
    };

    class BLAS
    {
    private:
        VkAccelerationStructureKHR accelerationStructure = VK_NULL_HANDLE;
        VkBuildAccelerationStructureFlagsKHR enabledFlags = 0;

        std::unique_ptr<Buffer> blasBuffer;

        class Model const* parentModelPtr; //TODO ADD VBO AND IBO REFERENCES AND DONT DERIVE SAID DATA FROM MODEL
        class RenderEngine* rendererPtr;

        friend class AccelerationStructureBuilder;
    public:
        BLAS(RenderEngine* renderer);
        ~BLAS();

        VkAccelerationStructureKHR getAccelerationStructure() const { return accelerationStructure; }
        class Model const* getParentModelPtr() const { return parentModelPtr; }
    };

    class TLAS
    {
    private:
        VkAccelerationStructureKHR accelerationStructure = VK_NULL_HANDLE;

        std::unique_ptr<Buffer> tlasBuffer;
        std::unique_ptr<Buffer> instancesBuffer;

        VkDeviceSize instanceDescriptionsOffset = 0;
        VkDeviceSize tlInstancesOffset = 0;

        const float instancesOverhead = 1.5;

        std::vector<class ModelInstance*> accelerationStructureInstances;
        std::deque<class ModelInstance*> toUpdateInstances;

        struct InstanceDescription
        {
            uint32_t modelDataOffset;
        };

        void rebuildInstancesBuffer();
        void queueInstanceTransfers();

        class RenderEngine* rendererPtr;

        friend RenderEngine;
        friend class AccelerationStructureBuilder;
        friend TLASInstanceBuildPipeline;
    public:
        TLAS(RenderEngine* renderer);
        ~TLAS();

        void addInstance(class ModelInstance* instance);
        void removeInstance(class ModelInstance* instance);

        VkAccelerationStructureKHR getAccelerationStructure() const { return accelerationStructure; }
        Buffer const* getInstancesBuffer() const { return instancesBuffer.get(); }
        const VkDeviceSize getInstanceDescriptionsOffset() const { return instanceDescriptionsOffset; }
        const VkDeviceSize getInstanceDescriptionsRange() const { return accelerationStructureInstances.size() * sizeof(InstanceDescription); }
    };

    class AccelerationStructureBuilder
    {
    private:
        std::unique_ptr<Buffer> scratchBuffer;

        //acceleration structure update queues
        struct BlasBuildOp
        {
            BLAS* blas;
            VkBuildAccelerationStructureFlagsKHR flags;
        };
        std::deque<BlasBuildOp> blasBuildQueue;
        std::deque<BLAS*> blasUpdateQueue;
        std::deque<TLAS*> tlasBuildQueue;
        VkSemaphore asBuildSemaphore;
        
        //blas data
        struct BlasBuildData
        {
            BLAS& blas;
            std::vector<VkAccelerationStructureGeometryKHR> geometries;
            std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangeInfos;
            VkAccelerationStructureBuildGeometryInfoKHR buildGeoInfo = {};
            VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo = {};
            VkDeviceSize scratchDataOffset = 0;
            bool compact = false;
        };
        BlasBuildData getBlasData(const BlasBuildOp& blasOp, VkBuildAccelerationStructureModeKHR mode) const;

        //tlas datas
        struct TlasBuildData
        {
            TLAS& tlas;
            std::unique_ptr<VkAccelerationStructureGeometryKHR> geometry; //unique ptr because bruh dangling pointer moment with vulkan
            VkAccelerationStructureBuildGeometryInfoKHR buildGeoInfo = {};
            VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo = {};
            VkDeviceSize scratchDataOffset = 0;
        };
        TlasBuildData getTlasData(TLAS& tlas) const;

        //all build data
        struct BuildData
        {
            std::vector<BlasBuildData> blasUpdateDatas;
            std::vector<BlasBuildData> blasBuildDatas;
            std::vector<TlasBuildData> tlasBuildDatas;
            uint32_t numCompactions = 0;
        } buildData;

        //scratch size
        VkDeviceSize getScratchSize(
            std::vector<BlasBuildData>& updateDatas,
            std::vector<BlasBuildData>& buildDatas,
            std::vector<TlasBuildData>& tlasDatas) const;
        
        //for keeping structures to derrive compaction from in scope
        struct OldStructureData
        {
            VkAccelerationStructureKHR structure;
            std::unique_ptr<Buffer> buffer;
        };
        std::queue<OldStructureData> destructionQueue;

        class RenderEngine* rendererPtr;
    public:
        AccelerationStructureBuilder(RenderEngine* renderer);
        ~AccelerationStructureBuilder();
        
        void queueBlasBuild(BLAS* blas, VkBuildAccelerationStructureFlagsKHR flags) { blasBuildQueue.emplace_back(blas, flags); }
        void queueBlasUpdate(BLAS* blas) { blasUpdateQueue.emplace_back(blas); }
        void queueTlasUpdate(TLAS* tlas) { tlasBuildQueue.emplace_back(tlas); }
        void setBuildData();
        void destroyOldData();

        void submitQueuedStructureOps(const SynchronizationInfo& syncInfo);
    };
}