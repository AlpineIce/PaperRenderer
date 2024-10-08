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

    //acceleration structure base class
    class AS
    {
    private:
        VkAccelerationStructureKHR accelerationStructure = VK_NULL_HANDLE;
        VkBuildAccelerationStructureFlagsKHR enabledFlags = 0;

        std::unique_ptr<Buffer> asBuffer;

        friend class AccelerationStructureBuilder;

    protected:
        class RenderEngine* rendererPtr;

    public:
        AS(RenderEngine* renderer);
        ~AS();
        AS(const AS&) = delete;

        VkAccelerationStructureKHR getAccelerationStructure() const { return accelerationStructure; }
        VkDeviceAddress getAccelerationStructureAddress() const { return asBuffer ? asBuffer->getBufferDeviceAddress() : 0; }
    };

    //Bottom level acceleration structure
    class BLAS : public AS
    {
    private:
        class Model const* parentModelPtr;
        Buffer const* vboPtr = NULL;

    public:
        //If vbo is null, BLAS will instead use those directly from the model. Model is needed
        //for data describing different geometries
        BLAS(RenderEngine* renderer, Model const* model, Buffer const* vbo);
        ~BLAS();
        BLAS(const BLAS&) = delete;

        VkDeviceAddress getVBOAddress() const { return vboPtr->getBufferDeviceAddress(); }
        Model const* getParentModelPtr() const { return parentModelPtr; }
    };

    //top level acceleration structure
    class TLAS : public AS
    {
    private:
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

        friend RenderEngine;
        friend class AccelerationStructureBuilder;
        friend TLASInstanceBuildPipeline;
    public:
        TLAS(RenderEngine* renderer);
        ~TLAS();
        TLAS(const TLAS&) = delete;

        void addInstance(class ModelInstance* instance);
        void removeInstance(class ModelInstance* instance);

        Buffer const* getInstancesBuffer() const { return instancesBuffer.get(); }
        const VkDeviceSize getInstanceDescriptionsOffset() const { return instanceDescriptionsOffset; }
        const VkDeviceSize getInstanceDescriptionsRange() const { return accelerationStructureInstances.size() * sizeof(InstanceDescription); }
    };

    //AS operation
    struct AccelerationStructureOp
    {
        AS* accelerationStructure;
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
        
        //acceleration structure build data
        struct AsBuildData
        {
            AS* as;
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
        
        void queueAs(const AccelerationStructureOp& op) { asQueue.emplace_back(op); }
        void setBuildData();
        void destroyOldData();

        void submitQueuedOps(const SynchronizationInfo& syncInfo, VkAccelerationStructureTypeKHR type); //may block thread if compaction is used for any threads
    };
}