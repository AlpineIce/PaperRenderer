#pragma once
#include "RHI/Swapchain.h"
#include "RHI/Pipeline.h"
#include "RHI/IndirectDraw.h"
#include "RHI/AccelerationStructure.h"
#include "Camera.h"
#include "ComputeShader.h"
#include "Material.h"
#include "Model.h"

namespace PaperRenderer
{
    //leaf node including the mesh and any uniforms/push constants to be set for rendering. 
    //Ownership should be within actors, not the render tree, which includes a pointer instead

    //node for objects corresponding to one material
    struct MaterialInstanceNode
    {
        std::unique_ptr<CommonMeshGroup> meshGroups;
    };

    //node for materials corresponding to one pipeline
    struct MaterialNode
    {
        std::unordered_map<MaterialInstance*, MaterialInstanceNode> instances;
    };

    struct IndirectRenderingData
    {
        uint32_t objectCount;
        VkBufferCopy inputObjectsRegion;

        std::vector<char> stagingData;
        std::unique_ptr<PaperMemory::DeviceAllocation> bufferAllocation;
        std::unique_ptr<PaperMemory::Buffer> bufferData; //THE UBER-BUFFER
    };

    //----------PREPROCESS COMPUTE PIPELINES----------//

    class RasterPreprocessPipeline : public ComputeShader
    {
    private:
        std::string fileName = "IndirectDrawBuild.spv";
        std::vector<std::unique_ptr<PaperMemory::Buffer>> uniformBuffers;
        std::unique_ptr<PaperMemory::DeviceAllocation> uniformBuffersAllocation;

        struct UBOInputData
        {
            VkDeviceAddress bufferAddress; //used with offsets to make LOD selection possible in a compute shader
            uint64_t padding;
            glm::vec4 camPos;
            glm::mat4 projection;
            glm::mat4 view;
            CameraFrustum frustumData;
            uint32_t objectCount;
        };

    public:
        RasterPreprocessPipeline(std::string fileDir);
        ~RasterPreprocessPipeline() override;

        PaperMemory::CommandBuffer submit(Camera* camera, const IndirectRenderingData& renderingData, uint32_t currentImage, PaperMemory::SynchronizationInfo syncInfo);
    };
    
    class RTPreprocessPipeline : public ComputeShader
    {
    private:
        std::string fileName = "RTObjectBuild.spv";
        std::vector<std::unique_ptr<PaperMemory::Buffer>> uniformBuffers;
        std::unique_ptr<PaperMemory::DeviceAllocation> uniformBuffersAllocation;

        struct UBOInputData
        {
            VkDeviceAddress tlasInstancesAddress;
            uint32_t objectCount;
        };

    public:
        RTPreprocessPipeline(std::string fileDir);
        ~RTPreprocessPipeline() override;

        PaperMemory::CommandBuffer submit();
    };

    //----------RENDER PASS----------//

    class RenderPass
    {
    private:
        //structs
        struct ShaderInputObject
        {
            //transformation
            glm::vec4 position;
            glm::vec4 scale; 
            glm::mat4 rotation; //quat -> mat4... could possibly be a mat3
            AABB bounds;
            uint32_t lodCount;
            uint32_t lodsOffset;
        };

        struct ShaderLOD
        {
            uint32_t meshCount;
            uint32_t meshesLocationOffset;
        };

        //synchronization and commands
        std::vector<VkSemaphore> imageSemaphores;
        std::vector<VkSemaphore> bufferCopySemaphores;
        std::vector<VkFence> bufferCopyFences;
        std::vector<VkSemaphore> BLASBuildSemaphores;
        std::vector<VkSemaphore> TLASBuildSemaphores;
        std::vector<VkSemaphore> rasterPreprocessSemaphores;
        std::vector<VkSemaphore> preprocessTLASSignalSemaphores;
        std::vector<VkSemaphore> renderSemaphores;
        std::vector<VkFence> RTFences;
        std::vector<VkFence> renderFences;
        std::vector<std::vector<PaperMemory::CommandBuffer>> usedCmdBuffers;
        
        //buffers and allocations
        std::vector<std::unique_ptr<PaperMemory::DeviceAllocation>> stagingAllocations;
        std::vector<std::unique_ptr<PaperMemory::Buffer>> newDataStagingBuffers;

        //compute shaders
        std::unique_ptr<RasterPreprocessPipeline> rasterPreprocessPipeline;
        std::unique_ptr<RTPreprocessPipeline> RTPreprocessPipeline;

        //device local rendering buffer and misc data
        std::vector<IndirectRenderingData> renderingData; //includes its own device local allocation, but needs staging allocation for access
        std::unordered_map<Model const*, std::vector<ModelInstance*>> renderingModels;
        
        AccelerationStructure rtAccelStructure;
        uint32_t currentImage;
        bool recreateFlag = false;

        Swapchain* swapchainPtr;
        Device* devicePtr;
        DescriptorAllocator* descriptorsPtr;
        PipelineBuilder* pipelineBuilderPtr;
        Camera* cameraPtr = NULL;
        
        //helper functions
        void rebuildRenderDataAllocation(uint32_t currentFrame);
        
        //frame rendering functions
        void raster(std::unordered_map<Material*, MaterialNode>& renderTree);
        void setRasterStagingData(const std::unordered_map<Material*, MaterialNode>& renderTree);
        void setRTStagingData(const std::unordered_map<Material*, MaterialNode>& renderTree);
        void copyStagingData();
        void rasterPreProcess(const std::unordered_map<Material*, MaterialNode>& renderTree);
        void rayTracePreProcess(const std::unordered_map<Material*, MaterialNode>& renderTree);
        void frameEnd(VkSemaphore waitSemaphore);

    public:
        RenderPass(Swapchain* swapchain, Device* device, DescriptorAllocator* descriptors, PipelineBuilder* pipelineBuilder);
        ~RenderPass();

        //set camera
        void setCamera(Camera* camera) { this->cameraPtr = camera; }

        //draw new frame
        void rasterOrTrace(bool shouldRaster, std::unordered_map<Material *, MaterialNode> &renderTree);

        void addModelInstance(ModelInstance* instance, uint64_t& selfIndex);
        void removeModelInstance(ModelInstance* instance, uint64_t& reference);
    };
}