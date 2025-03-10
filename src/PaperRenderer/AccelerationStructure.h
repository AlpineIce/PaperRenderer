#pragma once
#include "ComputeShader.h"

namespace PaperRenderer
{
    //----------TLAS INSTANCE BUILD PIPELINE DECLARATIONS----------//

    class TLASInstanceBuildPipeline
    {
    private:
        DescriptorSetLayout uboSetLayout;
        DescriptorSetLayout ioSetLayout;
        ComputeShader computeShader;

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

        void submit(VkCommandBuffer cmdBuffer, const TLAS& tlas, const uint32_t count) const;

        const VkDescriptorSetLayout& getUboDescriptorLayout() const { return uboSetLayout.getSetLayout(); }
        const VkDescriptorSetLayout& getIODescriptorLayout() const { return ioSetLayout.getSetLayout(); }
    };

    //----------ACCELERATION STRUCTURE DECLARATIONS----------//

    //acceleration structure base class
    class AS
    {
    private:
        std::unique_ptr<Buffer> asBuffer = NULL;
        VkAccelerationStructureKHR accelerationStructure = VK_NULL_HANDLE;

        friend class AccelerationStructureBuilder;

    protected:
        //geometry build data
        struct AsGeometryBuildData
        {
            std::vector<VkAccelerationStructureGeometryKHR> geometries = {};
            std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangeInfos = {};
            std::vector<uint32_t> primitiveCounts = {};
        };
        virtual std::unique_ptr<AsGeometryBuildData> getGeometryData() const = 0;

        //combined build data
        struct AsBuildData
        {
            std::unique_ptr<AsGeometryBuildData> geometryBuildData = NULL;
            VkAccelerationStructureBuildGeometryInfoKHR buildGeoInfo = {};
            VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo = {};
            bool compact = false;
        };
        AS::AsBuildData getAsData(const VkAccelerationStructureTypeKHR type, const VkBuildAccelerationStructureFlagsKHR flags, const VkBuildAccelerationStructureModeKHR mode);

        //build
        struct CompactionQuery
        {
            VkQueryPool pool = VK_NULL_HANDLE;
            VkDeviceSize compactionIndex = 0;
        };
        virtual void buildStructure(VkCommandBuffer cmdBuffer, AsBuildData& data, const CompactionQuery compactionQuery, const VkDeviceAddress scratchAddress);

        //compaction operation; returns old buffer which must stay in scope until completion
        std::unique_ptr<Buffer> compactStructure(VkCommandBuffer cmdBuffer, const VkAccelerationStructureTypeKHR type, const VkDeviceSize newSize);

        //resource ownership
        virtual void assignResourceOwner(const Queue& queue);
        std::array<std::deque<VkAccelerationStructureKHR>, 2> asDestructionQueue;

        class RenderEngine& renderer;

    public:
        AS(RenderEngine& renderer);
        virtual ~AS();
        AS(const AS&) = delete;

        VkAccelerationStructureKHR getAccelerationStructure() const { return accelerationStructure; }
        VkDeviceAddress getASBufferAddress() const { return asBuffer ? asBuffer->getBufferDeviceAddress() : 0; }
        VkDeviceAddress getAsDeviceAddress() const;
    };

    //Bottom level acceleration structure
    class BLAS : public AS
    {
    private:
        const class Model& parentModel;
        Buffer const* vboPtr = NULL;

        std::unique_ptr<AsGeometryBuildData> getGeometryData() const override;
        void buildStructure(VkCommandBuffer cmdBuffer, AsBuildData& data, const CompactionQuery compactionQuery, const VkDeviceAddress scratchAddress) override;

        friend class AccelerationStructureBuilder;

    public:
        //If vbo is null, BLAS will instead use those directly from the model. Model is needed
        //for data describing different geometries
        BLAS(RenderEngine& renderer, const Model& model, Buffer const* vbo);
        ~BLAS() override;
        BLAS(const BLAS&) = delete;

        VkDeviceAddress getVBOAddress() const { return vboPtr->getBufferDeviceAddress(); }
        const Model& getParentModel() const { return parentModel; }
    };

    //top level acceleration structure
    class TLAS : public AS
    {
    private:
        //buffers
        Buffer preprocessUniformBuffer;
        std::unique_ptr<Buffer> scratchBuffer; //TLAS gets its own scratch buffer
        std::unique_ptr<Buffer> instancesBuffer;

        //sync
        uint64_t transferSemaphoreValue = 0;
        VkSemaphore transferSemaphore;

        //descriptors
        enum TLASDescriptorIndices
        {
            UBO = 0,
            INSTANCES = 1,
            IO = 2,
        };
        ResourceDescriptor uboDescriptor;
        ResourceDescriptor ioDescriptor;
        ResourceDescriptor instanceDescriptionsDescriptor;

        //instances data offsets/sizes
        struct InstancesBufferSizes
        {
            VkDeviceSize instancesOffset = 0;
            VkDeviceSize instancesRange = 0;
            VkDeviceSize instanceDescriptionsOffset = 0;
            VkDeviceSize instanceDescriptionsRange = 0;
            VkDeviceSize tlInstancesOffset = 0;
            VkDeviceSize tlInstancesRange = 0;

            VkDeviceSize totalSize()
            {
                return instancesRange + instanceDescriptionsRange + tlInstancesRange;
            }
        } instancesBufferSizes = {};

        const float instancesOverhead = 1.5;

        struct InstanceDescription
        {
            uint32_t modelDataOffset;
        };

        std::unique_ptr<AsGeometryBuildData> getGeometryData() const override;
        void verifyInstancesBuffer(const uint32_t instanceCount);
        void buildStructure(VkCommandBuffer cmdBuffer, AsBuildData& data, const CompactionQuery compactionQuery, const VkDeviceAddress scratchAddress) override;

        //ownership
        void assignResourceOwner(const Queue& queue) override;

        class RayTraceRender& rtRender;

        friend class ModelInstance;
        friend TLASInstanceBuildPipeline;

    public:
        TLAS(RenderEngine& renderer, class RayTraceRender& rtRender);
        ~TLAS() override;
        TLAS(const TLAS&) = delete;

        //Updates the TLAS to the RayTraceRender instances according to the mode (either rebuild or update). Note that compaction is ignored for a TLAS
        const Queue& updateTLAS(const VkBuildAccelerationStructureModeKHR mode, const VkBuildAccelerationStructureFlagsKHR flags, SynchronizationInfo syncInfo);

        const Buffer& getInstancesBuffer() const { return *instancesBuffer; }
        const InstancesBufferSizes& getInstancesBufferSizes() const { return instancesBufferSizes; }
        const ResourceDescriptor& getInstanceDescriptionsDescriptor() const { return instanceDescriptionsDescriptor; }
    };

    //BLAS operation
    struct BLASBuildOp
    {
        BLAS& accelerationStructure;
        VkBuildAccelerationStructureModeKHR mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        VkBuildAccelerationStructureFlagsKHR flags = 0;
    };

    class AccelerationStructureBuilder
    {
    private:
        //acceleration structure operation queue
        std::mutex builderMutex;
        std::deque<BLASBuildOp> blasQueue;

        //scratch buffer shared amongst BLAS builds
        const VkDeviceSize scratchBufferSize = 268435456; // 2^26 bytes; ~256MiB
        std::unique_ptr<Buffer> scratchBuffer;

        //BLAS' that request compaction
        std::unordered_map<BLAS*, VkDeviceSize> getCompactions() const;
        
        class RenderEngine& renderer;

    public:
        AccelerationStructureBuilder(RenderEngine& renderer);
        ~AccelerationStructureBuilder();
        AccelerationStructureBuilder(const AccelerationStructureBuilder&) = delete;
        
        void queueBLAS(const BLASBuildOp& op);

        const Queue& submitQueuedOps(const SynchronizationInfo& syncInfo); //may block thread if compaction is used
    };
}