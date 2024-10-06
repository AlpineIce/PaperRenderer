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

    class BLAS
    {
    private:
        VkAccelerationStructureKHR accelerationStructure = VK_NULL_HANDLE;
        VkBuildAccelerationStructureFlagsKHR enabledFlags = 0;

        std::unique_ptr<Buffer> blasBuffer;

        class Model const* parentModelPtr;
        Buffer const* vboPtr = NULL;

        class RenderEngine* rendererPtr;

        friend class AccelerationStructureBuilder;
    public:
        //If vbo is null, BLAS will instead use those directly from the model. Model is needed
        //for data describing different geometries
        BLAS(RenderEngine* renderer, Model const* model, Buffer const* vbo);
        ~BLAS();
        BLAS(const BLAS&) = delete;

        VkAccelerationStructureKHR getAccelerationStructure() const { return accelerationStructure; }
        VkDeviceAddress getAccelerationStructureAddress() const { return blasBuffer ? blasBuffer->getBufferDeviceAddress() : 0; }
        VkDeviceAddress getVBOAddress() const { return vboPtr->getBufferDeviceAddress(); }
        Model const* getParentModelPtr() const { return parentModelPtr; }
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

        void verifyInstancesBuffer();
        void queueInstanceTransfers();

        class RenderEngine* rendererPtr;

        friend RenderEngine;
        friend class AccelerationStructureBuilder;
        friend TLASInstanceBuildPipeline;
    public:
        TLAS(RenderEngine* renderer);
        ~TLAS();
        TLAS(const TLAS&) = delete;

        void addInstance(class ModelInstance* instance);
        void removeInstance(class ModelInstance* instance);

        VkAccelerationStructureKHR getAccelerationStructure() const { return accelerationStructure; }
        VkDeviceAddress getAccelerationStructureAddress() const { return tlasBuffer->getBufferDeviceAddress(); }
        Buffer const* getInstancesBuffer() const { return instancesBuffer.get(); }
        const VkDeviceSize getInstanceDescriptionsOffset() const { return instanceDescriptionsOffset; }
        const VkDeviceSize getInstanceDescriptionsRange() const { return accelerationStructureInstances.size() * sizeof(InstanceDescription); }
    };

    //BLAS operation
    struct BlasOp
    {
        BLAS* blas = NULL;
        VkBuildAccelerationStructureModeKHR mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        VkBuildAccelerationStructureFlagsKHR flags = 0;
    };

    //TLAS operation
    struct TlasOp
    {
        TLAS* tlas = NULL;
        VkBuildAccelerationStructureModeKHR mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        VkBuildAccelerationStructureFlagsKHR flags = 0;
    };

    class AccelerationStructureBuilder
    {
    private:
        std::unique_ptr<Buffer> scratchBuffer;

        //acceleration structure queues
        std::deque<BlasOp> blasQueue;
        std::deque<TlasOp> tlasQueue;

        //build semaphore
        VkSemaphore asBuildSemaphore = VK_NULL_HANDLE;
        
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
        BlasBuildData getBlasData(const BlasOp& blasOp) const;

        //tlas datas
        struct TlasBuildData
        {
            TLAS& tlas;
            std::unique_ptr<VkAccelerationStructureGeometryKHR> geometry; //unique ptr because bruh dangling pointer moment with vulkan
            VkAccelerationStructureBuildGeometryInfoKHR buildGeoInfo = {};
            VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo = {};
            VkDeviceSize scratchDataOffset = 0;
            bool compact = false;
        };
        TlasBuildData getTlasData(const TlasOp& tlasOp) const;

        //all build data
        struct BuildData
        {
            std::vector<BlasBuildData> blasDatas;
            std::vector<TlasBuildData> tlasDatas;
            uint32_t numCompactions = 0;
        } buildData;

        //scratch size
        VkDeviceSize getScratchSize(std::vector<BlasBuildData>& blasDatas, std::vector<TlasBuildData>& tlasDatas) const;
        
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
        AccelerationStructureBuilder(const AccelerationStructureBuilder&) = delete;
        
        void queueBlas(const BlasOp& blasOp) { blasQueue.emplace_back(blasOp); }
        void queueTlas(const TlasOp& tlasOp) { tlasQueue.emplace_back(tlasOp); }
        void setBuildData();
        void destroyOldData();

        void submitQueuedBlasOps(const SynchronizationInfo& syncInfo); //may block thread if compaction is used for any threads
        void submitQueuedTlasOps(const SynchronizationInfo& syncInfo);
    };
}