#pragma once
#include "ComputeShader.h"

#include <queue>

namespace PaperRenderer
{
    //----------TLAS INSTANCE BUILD PIPELINE DECLARATIONS----------//

    class TLASInstanceBuildPipeline
    {
    private:
        const ComputeShader computeShader;

        class RenderEngine& renderer;

    public:
        TLASInstanceBuildPipeline(RenderEngine& renderer, const std::vector<uint32_t>& shaderData);
        ~TLASInstanceBuildPipeline();
        TLASInstanceBuildPipeline(const TLASInstanceBuildPipeline&) = delete;

        struct UBOInputData
        {
            uint32_t objectCount;
            float padding[15];
        };

        void submit(VkCommandBuffer cmdBuffer, const TLAS& tlas);
    };

    //----------ACCELERATION STRUCTURE DECLARATIONS----------//

    //acceleration structure base class
    class AS
    {
    private:
        VkAccelerationStructureKHR accelerationStructure = VK_NULL_HANDLE;
        VkBuildAccelerationStructureFlagsKHR enabledFlags = 0;

        std::unique_ptr<Buffer> asBuffer;

        friend class AccelerationStructureBuilder;

    protected:
        class RenderEngine& renderer;

    public:
        AS(RenderEngine& renderer);
        ~AS();
        AS(const AS&) = delete;

        VkAccelerationStructureKHR getAccelerationStructure() const { return accelerationStructure; }
        VkDeviceAddress getAccelerationStructureAddress() const { return asBuffer ? asBuffer->getBufferDeviceAddress() : 0; }
    };

    //Bottom level acceleration structure
    class BLAS : public AS
    {
    private:
        const class Model& parentModel;
        Buffer const* vboPtr = NULL;

    public:
        //If vbo is null, BLAS will instead use those directly from the model. Model is needed
        //for data describing different geometries
        BLAS(RenderEngine& renderer, const Model& model, Buffer const* vbo);
        ~BLAS();
        BLAS(const BLAS&) = delete;

        VkDeviceAddress getVBOAddress() const { return vboPtr->getBufferDeviceAddress(); }
        const Model& getParentModel() const { return parentModel; }
    };

    //top level acceleration structure
    class TLAS : public AS
    {
    private:
        Buffer preprocessUniformBuffer;
        std::unique_ptr<Buffer> instancesBuffer;

        VkDeviceSize instanceDescriptionsOffset = 0;
        VkDeviceSize tlInstancesOffset = 0;
        uint32_t nextUpdateSize = 0;

        const float instancesOverhead = 1.5;

        struct InstanceDescription
        {
            uint32_t modelDataOffset;
        };

        void verifyInstancesBuffer(const uint32_t instanceCount);
        void queueInstanceTransfers(const class RayTraceRender& rtRender);

        friend RenderEngine;
        friend class ModelInstance;
        friend class AccelerationStructureBuilder;
        friend class RayTraceRender;
        friend TLASInstanceBuildPipeline;
    public:
        TLAS(RenderEngine& renderer);
        ~TLAS();
        TLAS(const TLAS&) = delete;

        const Buffer& getInstancesBuffer() const { return *instancesBuffer; }
        const VkDeviceSize getInstanceDescriptionsOffset() const { return instanceDescriptionsOffset; } //MAY POSSIBLY CHANGE AFTER UPDATING TLAS IN RayTraceRender::updateTLAS()
        const VkDeviceSize getInstanceDescriptionsRange() const { return nextUpdateSize * sizeof(InstanceDescription); } //MAY POSSIBLY CHANGE AFTER UPDATING TLAS IN RayTraceRender::updateTLAS()
    };

    //AS operation
    struct AccelerationStructureOp
    {
        AS& accelerationStructure;
        VkAccelerationStructureTypeKHR type = VK_ACCELERATION_STRUCTURE_TYPE_MAX_ENUM_KHR; //MUST BE DEFINED AS TOP OR BOTTOM LEVEL
        VkBuildAccelerationStructureModeKHR mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        VkBuildAccelerationStructureFlagsKHR flags = 0;
    };

    class AccelerationStructureBuilder
    {
    private:
        std::unique_ptr<Buffer> scratchBuffer;

        //acceleration structure operation queue
        std::deque<AccelerationStructureOp> asQueue;

        //build semaphore
        VkSemaphore asBuildSemaphore = VK_NULL_HANDLE;
        uint64_t finalSemaphoreValue = 0;
        
        //acceleration structure build data
        struct AsBuildData
        {
            AS& as;
            std::vector<VkAccelerationStructureGeometryKHR> geometries;
            std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangeInfos;
            VkAccelerationStructureBuildGeometryInfoKHR buildGeoInfo = {};
            VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo = {};
            VkDeviceSize scratchDataOffset = 0;
            VkAccelerationStructureTypeKHR type = VK_ACCELERATION_STRUCTURE_TYPE_MAX_ENUM_KHR;
            bool compact = false;
        };
        AsBuildData getAsData(const AccelerationStructureOp& op) const;

        //all build data
        struct BuildData
        {
            std::vector<AsBuildData> blasDatas;
            uint32_t numBlasCompactions = 0;
            std::vector<AsBuildData> tlasDatas;
            uint32_t numTlasCompactions = 0;
        } buildData;

        //scratch size
        VkDeviceSize getScratchSize(std::vector<AsBuildData>& blasDatas, std::vector<AsBuildData>& tlasDatas) const;

        //set build data (happens on submission)
        void setBuildData();
        
        //for keeping structures to derrive compaction from in scope
        struct OldStructureData
        {
            VkAccelerationStructureKHR structure;
            std::unique_ptr<Buffer> buffer;
        };
        std::queue<OldStructureData> destructionQueue;

        class RenderEngine& renderer;
    public:
        AccelerationStructureBuilder(RenderEngine& renderer);
        ~AccelerationStructureBuilder();
        AccelerationStructureBuilder(const AccelerationStructureBuilder&) = delete;
        
        void queueAs(const AccelerationStructureOp& op) { asQueue.emplace_back(op); }
        void destroyOldData();

        TimelineSemaphorePair getBuildSemaphore() const { return { asBuildSemaphore, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR, finalSemaphoreValue }; }

        void submitQueuedOps(const SynchronizationInfo& syncInfo, VkAccelerationStructureTypeKHR type); //may block thread if compaction is used for any threads
    };
}