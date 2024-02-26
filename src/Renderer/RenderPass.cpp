#include "RenderPass.h"
#include "Renderer.h"
#include "glm/gtc/matrix_transform.hpp"

namespace Renderer
{
    RenderPass::RenderPass(Swapchain* swapchain, Device* device, CmdBufferAllocator* commands, DescriptorAllocator* descriptors, PipelineBuilder* pipelineBuilder)
        :swapchainPtr(swapchain),
        devicePtr(device),
        commandsPtr(commands),
        descriptorsPtr(descriptors),
        pipelineBuilderPtr(pipelineBuilder),
        currentImage(0)
    {
        //THE UBER-BUFFER
        renderingData.resize(commandsPtr->getFrameCount());
        lightingInfoBuffers.resize(commandsPtr->getFrameCount());

        //synchronization objects
        imageSemaphores.resize(commandsPtr->getFrameCount());
        bufferCopySemaphores.resize(commandsPtr->getFrameCount());
        cullingSemaphores.resize(commandsPtr->getFrameCount());
        renderSemaphores.resize(commandsPtr->getFrameCount());
        cullingFences.resize(commandsPtr->getFrameCount());
        renderFences.resize(commandsPtr->getFrameCount());
        fenceCmdBuffers.resize(commandsPtr->getFrameCount());

        for(uint32_t i = 0; i < commandsPtr->getFrameCount(); i++)
        {
            descriptorsPtr->refreshPools(i);
            renderingData.at(i) = std::make_shared<IndirectRenderingData>();
            renderingData.at(i)->bufferData = std::make_shared<StorageBuffer>(devicePtr, commandsPtr, 0);

            imageSemaphores.at(i) = commandsPtr->getSemaphore();
            bufferCopySemaphores.at(i) = commandsPtr->getSemaphore();
            cullingSemaphores.at(i) = commandsPtr->getSemaphore();
            renderSemaphores.at(i) = commandsPtr->getSemaphore();
            cullingFences.at(i) = commandsPtr->getSignaledFence();
            renderFences.at(i) = commandsPtr->getSignaledFence();

            preprocessUniformBuffers.push_back(std::make_shared<UniformBuffer>(devicePtr, commandsPtr, sizeof(CullingInputData)));
        }

        for(auto& buffer : lightingInfoBuffers)
        {
            buffer = std::make_shared<UniformBuffer>(devicePtr, commandsPtr, sizeof(ShaderLightingInformation));
        }

        //----------PREPROCESS PIPELINE----------//

        std::vector<ShaderPair> shaderPairs = {{
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .directory = "resources/compute/IndirectDrawBuild.spv"
        }};
        std::unordered_map<uint32_t, DescriptorSet> descriptorSets;

        DescriptorSet set0;
        set0.setNumber = 1;
        VkDescriptorSetLayoutBinding inputDataDescriptor = {};
        inputDataDescriptor.binding = 0;
        inputDataDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        inputDataDescriptor.descriptorCount = 1;
        inputDataDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        set0.descriptorBindings[0] = inputDataDescriptor;

        VkDescriptorSetLayoutBinding inputObjectsDescriptor = {};
        inputObjectsDescriptor.binding = 1;
        inputObjectsDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        inputObjectsDescriptor.descriptorCount = 1;
        inputObjectsDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        set0.descriptorBindings[1] = inputObjectsDescriptor;

        descriptorSets[0] = set0;

        PipelineBuildInfo pipelineInfo;
        pipelineInfo.descriptors = descriptorSets;
        pipelineInfo.shaderInfo = shaderPairs;
        
        meshPreprocessPipeline = pipelineBuilderPtr->buildComputePipeline(pipelineInfo);
    }

    RenderPass::~RenderPass()
    {
        for(uint32_t i = 0; i < commandsPtr->getFrameCount(); i++)
        {
            vkDestroySemaphore(devicePtr->getDevice(), imageSemaphores.at(i), nullptr);
            vkDestroySemaphore(devicePtr->getDevice(), bufferCopySemaphores.at(i), nullptr);
            vkDestroySemaphore(devicePtr->getDevice(), cullingSemaphores.at(i), nullptr);
            vkDestroySemaphore(devicePtr->getDevice(), renderSemaphores.at(i), nullptr);
            vkDestroyFence(devicePtr->getDevice(), cullingFences.at(i), nullptr);
            vkDestroyFence(devicePtr->getDevice(), renderFences.at(i), nullptr);
        }
    }

    void RenderPass::checkSwapchain(VkResult imageResult)
    {
        if(imageResult == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateFlag = true;
        }
    }

    void RenderPass::setStagingData(const std::unordered_map<Material*, MaterialNode>& renderTree, const LightingInformation& lightingInfo)
    {
        //----------LIGHTING REQUIREMENETS----------//

        std::vector<char>& stagingData = renderingData.at(currentImage)->stagingData;
        stagingData.clear();

        //fill in point light buffer
        renderingData.at(currentImage)->fragmentInputRegion = VkBufferCopy();
        renderingData.at(currentImage)->fragmentInputRegion.srcOffset = stagingData.size();
        renderingData.at(currentImage)->fragmentInputRegion.dstOffset = stagingData.size();
        std::vector<PointLight> pointLights;
        for(auto light = lightingInfo.pointLights.begin(); light != lightingInfo.pointLights.end(); light++)
        {
            pointLights.push_back(**light);
        }

        renderingData.at(currentImage)->lightsOffset = stagingData.size();
        stagingData.resize(stagingData.size() + sizeof(PointLight) * pointLights.size());
        memcpy(stagingData.data() + renderingData.at(currentImage)->fragmentInputRegion.srcOffset , pointLights.data(), sizeof(PointLight) * pointLights.size());
        
        renderingData.at(currentImage)->fragmentInputRegion.size = stagingData.size() - renderingData.at(currentImage)->fragmentInputRegion.dstOffset; //fragment shader input region
        stagingData.resize((stagingData.size() - stagingData.size() % 128) + 128); //padding

        //put lighting into staging data
        ShaderLightingInformation shaderLightingInfo = {};
        if(lightingInfo.directLight) shaderLightingInfo.directLight = *lightingInfo.directLight;
        if(lightingInfo.ambientLight) shaderLightingInfo.ambientLight = *lightingInfo.ambientLight;
        shaderLightingInfo.pointLightCount = pointLights.size();
        shaderLightingInfo.camPos = cameraPtr->getTranslation().position;
        lightingInfoBuffers.at(currentImage)->updateUniformBuffer(&shaderLightingInfo, sizeof(ShaderLightingInformation));
        renderingData.at(currentImage)->lightCount = pointLights.size();

        //----------MESH REQUIREMENETS----------//

        //draw counts
        renderingData.at(currentImage)->meshDrawCountsRegion = VkBufferCopy();
        renderingData.at(currentImage)->meshDrawCountsRegion.srcOffset = stagingData.size();
        renderingData.at(currentImage)->meshDrawCountsRegion.dstOffset = stagingData.size();
        for(const auto& [material, materialNode] : renderTree)
        {
            for(const auto& [materialInstance, instanceNode] : materialNode.instances) //memcpy because it needs to be cleared //TODO NEEDS TO HAPPEN AFTER VERTEX SHADER STAGE
            {
                uint32_t lastSize = stagingData.size();
                std::vector<uint32_t> data;
                stagingData.resize(stagingData.size() + instanceNode.objectBuffer->getDrawCountsSize(stagingData.size()));
                for(uint32_t i = 0; i < (stagingData.size() - lastSize) / sizeof(uint32_t); i++)
                {
                    data.push_back(0);
                }
                memcpy(stagingData.data() + lastSize, data.data(), stagingData.size() - lastSize);
            }
        }
        renderingData.at(currentImage)->meshDrawCountsRegion.size = stagingData.size() - renderingData.at(currentImage)->meshDrawCountsRegion.dstOffset;
        stagingData.resize((stagingData.size() - stagingData.size() % 128) + 128); //padding

        //draw commands
        renderingData.at(currentImage)->meshDrawCommandsRegion = VkBufferCopy();
        renderingData.at(currentImage)->meshDrawCommandsRegion.srcOffset = stagingData.size();
        renderingData.at(currentImage)->meshDrawCommandsRegion.dstOffset = stagingData.size();
        for(const auto& [material, materialNode] : renderTree)
        {
            for(const auto& [materialInstance, instanceNode] : materialNode.instances)
            {
                stagingData.resize(stagingData.size() + instanceNode.objectBuffer->getDrawCommandsSize(stagingData.size()));
            }
        }
        renderingData.at(currentImage)->meshDrawCommandsRegion.size = stagingData.size() - renderingData.at(currentImage)->meshDrawCommandsRegion.dstOffset;
        stagingData.resize((stagingData.size() - stagingData.size() % 128) + 128); //padding

        //output mesh instance data
        renderingData.at(currentImage)->meshOutputObjectsRegion = VkBufferCopy();
        renderingData.at(currentImage)->meshOutputObjectsRegion.srcOffset = stagingData.size();
        renderingData.at(currentImage)->meshOutputObjectsRegion.dstOffset = stagingData.size();
        for(const auto& [material, materialNode] : renderTree)
        {
            for(const auto& [materialInstance, instanceNode] : materialNode.instances)
            {
                stagingData.resize(stagingData.size() + instanceNode.objectBuffer->getOutputObjectSize(stagingData.size()));
            }
        }
        renderingData.at(currentImage)->meshOutputObjectsRegion.size = stagingData.size() - renderingData.at(currentImage)->meshOutputObjectsRegion.dstOffset;
        stagingData.resize((stagingData.size() - stagingData.size() % 128) + 128); //padding

        //----------PREPROCESS INPUT REQUIREMENETS----------//

        //LOD mesh data
        renderingData.at(currentImage)->meshLODOffsetsRegion = VkBufferCopy();
        renderingData.at(currentImage)->meshLODOffsetsRegion.srcOffset = stagingData.size();
        renderingData.at(currentImage)->meshLODOffsetsRegion.dstOffset = stagingData.size();
        for(auto& [model, instances] : renderingModels)
        {
            uint32_t lastSize = stagingData.size();
            std::vector<LODMesh> lodMeshData = model->getMeshLODData(stagingData.size());
            stagingData.resize(stagingData.size() + lodMeshData.size() * sizeof(LODMesh));
            memcpy(stagingData.data() + lastSize, lodMeshData.data(), lodMeshData.size() * sizeof(LODMesh));
        }
        renderingData.at(currentImage)->meshLODOffsetsRegion.size = stagingData.size() - renderingData.at(currentImage)->meshLODOffsetsRegion.dstOffset;
        stagingData.resize((stagingData.size() - stagingData.size() % 128) + 128); //padding

        //LOD data          THIS **ABSOLUTELY MUST** COME AFTER MESH DATA
        renderingData.at(currentImage)->LODOffsetsRegion = VkBufferCopy();
        renderingData.at(currentImage)->LODOffsetsRegion.srcOffset = stagingData.size();
        renderingData.at(currentImage)->LODOffsetsRegion.dstOffset = stagingData.size();
        for(auto& [model, instances] : renderingModels)
        {
            uint32_t lastSize = stagingData.size();
            std::vector<ShaderLOD> lodData = model->getLODData(stagingData.size());
            stagingData.resize(stagingData.size() + lodData.size() * sizeof(ShaderLOD));
            memcpy(stagingData.data() + lastSize, lodData.data(), lodData.size() * sizeof(ShaderLOD));
        }
        renderingData.at(currentImage)->LODOffsetsRegion.size = stagingData.size() - renderingData.at(currentImage)->LODOffsetsRegion.dstOffset;
        stagingData.resize((stagingData.size() - stagingData.size() % 128) + 128); //padding
        
        //get input objects (binding 1)
        renderingData.at(currentImage)->inputObjectsRegion = VkBufferCopy();
        renderingData.at(currentImage)->inputObjectsRegion.srcOffset = stagingData.size();
        renderingData.at(currentImage)->inputObjectsRegion.dstOffset = stagingData.size();
        std::vector<ShaderInputObject> shaderInputObjects;
        for(auto& [model, instances] : renderingModels) //material
        {
            for(std::list<ModelInstance*>::iterator inputObjectReference = instances.begin(); inputObjectReference != instances.end(); inputObjectReference++)
            {
                ShaderInputObject inputObject;
                inputObject.position = glm::vec4((*inputObjectReference)->getTransformation().position, (*inputObjectReference)->getModelPtr()->getSphericalBounds());
                inputObject.rotation = glm::mat4_cast((*inputObjectReference)->getTransformation().rotation);
                inputObject.scale = glm::vec4((*inputObjectReference)->getTransformation().scale, 0.0f);
                inputObject.lodCount = (*inputObjectReference)->getModelPtr()->getLODs().size();
                inputObject.lodsOffset = model->getLODDataOffset();
                shaderInputObjects.push_back(inputObject);
            }
        }
        renderingData.at(currentImage)->objectCount = shaderInputObjects.size();
        stagingData.resize(stagingData.size() + shaderInputObjects.size() * sizeof(ShaderInputObject));
        memcpy(stagingData.data() + renderingData.at(currentImage)->inputObjectsRegion.srcOffset, shaderInputObjects.data(), sizeof(ShaderInputObject) * shaderInputObjects.size());

        renderingData.at(currentImage)->inputObjectsRegion.size = stagingData.size() - renderingData.at(currentImage)->inputObjectsRegion.dstOffset; //input objects region
        stagingData.resize((stagingData.size() - stagingData.size() % 128) + 128); //padding
    }

    glm::vec4 RenderPass::normalizePlane(glm::vec4 plane)
    {
        return plane / glm::length(glm::vec3(plane));
    }

    ImageAttachment RenderPass::createImageAttachment(VkFormat imageFormat)
    {
        VkImage returnImage;
        VkImageView returnView;
        VmaAllocation returnAllocation;

        VkExtent3D swapchainExtent = {
            .width = swapchainPtr->getExtent().width,
            .height = swapchainPtr->getExtent().height,
            .depth = 1
        };
        
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.pNext = NULL;
        imageInfo.flags = 0;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = imageFormat;
        imageInfo.extent = swapchainExtent;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; //no MSAA for now
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; //likely wont be transfered or anything
        imageInfo.queueFamilyIndexCount = 0;
        imageInfo.pQueueFamilyIndices = NULL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VmaAllocationInfo allocInfo;
        VkResult allocResult = vmaCreateImage(devicePtr->getAllocator(), &imageInfo, &allocCreateInfo, &returnImage, &returnAllocation, &allocInfo);

        //create the image view
        VkImageSubresourceRange subresourceRange;
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = 1;

        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.pNext = NULL;
        viewInfo.flags = 0;
        viewInfo.image = returnImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = imageFormat;
        viewInfo.subresourceRange = subresourceRange;

        if (vkCreateImageView(devicePtr->getDevice(), &viewInfo, nullptr, &returnView) != VK_SUCCESS) 
        {
            throw std::runtime_error("Failed to create a render target image view");
        }

        return { returnImage, returnView, returnAllocation };
    }

    void RenderPass::preProcessing(const std::unordered_map<Material*, MaterialNode>& renderTree, const LightingInformation& lightingInfo)
    {
        //----------FILL IN THE UBER-BUFFER, PRE-PROCESS THE BUFFER WITH ASYNC COMPUTE----------//

        uint32_t oldSize = renderingData.at(currentImage)->stagingData.size();
        setStagingData(renderTree, lightingInfo);

        //setup staging buffer (no more inputs after this)
        StagingBuffer newDataStaging(devicePtr, commandsPtr, renderingData.at(currentImage)->stagingData.size());
        newDataStaging.mapData(renderingData.at(currentImage)->stagingData.data(), 0, renderingData.at(currentImage)->stagingData.size()); 

        //allocate a new buffer before the wait fence if needed
        bool rebuildDataBuffer = false;
        if(renderingData.at(currentImage)->stagingData.size() > oldSize) rebuildDataBuffer = true;
        else if(renderingData.at(currentImage)->stagingData.size() < renderingData.at(currentImage)->stagingData.size() * 0.5) rebuildDataBuffer = true; //trimming operation at 50% of capped size

        //wait for fences
        std::vector<VkFence> waitFences = {
            cullingFences.at(currentImage),
            renderFences.at(currentImage)
        };
        vkWaitForFences(devicePtr->getDevice(), waitFences.size(), waitFences.data(), VK_TRUE, UINT64_MAX);
        vkResetFences(devicePtr->getDevice(), waitFences.size(), waitFences.data());

        //get available image
        checkSwapchain(vkAcquireNextImageKHR(devicePtr->getDevice(),
            *(swapchainPtr->getSwapchainPtr()),
            UINT64_MAX,
            imageSemaphores.at(currentImage),
            VK_NULL_HANDLE, &currentImage));

        //free command buffers and reset descriptor pool
        for(CommandBuffer& buffer : fenceCmdBuffers.at(currentImage))
        {
            commandsPtr->freeCommandBuffer(buffer);
        }
        fenceCmdBuffers.at(currentImage).clear();
        descriptorsPtr->refreshPools(currentImage);

        //copy buffers
        std::vector<VkBufferCopy> copyRegions = {
            renderingData.at(currentImage)->fragmentInputRegion,
            renderingData.at(currentImage)->inputObjectsRegion,
            renderingData.at(currentImage)->LODOffsetsRegion,
            renderingData.at(currentImage)->meshLODOffsetsRegion,
            renderingData.at(currentImage)->meshDrawCountsRegion
        };

        std::vector<SemaphorePair> waitPairs = {}; //culling should be done with culling fence
        std::vector<SemaphorePair> signalPairs = {
            { bufferCopySemaphores.at(currentImage), VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT }
        };

        //rebuild if needed, copy, submit
        if(rebuildDataBuffer)
        {
            //find 20% size and rebuild with that
            uint32_t newSize = renderingData.at(currentImage)->stagingData.size() * 1.2;
            newSize = ((newSize - newSize % 128) + 128); //padding
            renderingData.at(currentImage)->bufferData = std::make_shared<StorageBuffer>(devicePtr, commandsPtr, newSize);
        }
        fenceCmdBuffers.at(currentImage).push_back(renderingData.at(currentImage)->bufferData->copyFromBufferRanges(newDataStaging, waitPairs, signalPairs, VK_NULL_HANDLE, copyRegions));
        fenceCmdBuffers.at(currentImage).push_back(submitPreprocess());
    }

    void RenderPass::raster(const std::unordered_map<Material*, MaterialNode>& renderTree)
    {
        //command buffer
        VkCommandBufferBeginInfo commandInfo;
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandInfo.pNext = NULL;
        commandInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        commandInfo.pInheritanceInfo = NULL;

        //begin recording
        VkCommandBuffer graphicsCmdBuffer = commandsPtr->getCommandBuffer(CmdPoolType::GRAPHICS);
        vkBeginCommandBuffer(graphicsCmdBuffer, &commandInfo);

        //----------RENDER TARGETS----------//

        VkClearValue clearValue = {};
        clearValue.color = {0.0f, 0.0f, 0.0, 0.0f};

        VkClearValue depthClear = {};
        depthClear.depthStencil = {1.0f, 0};

        VkRect2D renderArea = {};
        renderArea.offset = {0, 0};
        renderArea.extent = swapchainPtr->getExtent();

        std::vector<VkRenderingAttachmentInfo> renderingAttachments;

        //color attacment
        VkRenderingAttachmentInfo colorAttachment = {};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.pNext = NULL;
        colorAttachment.imageView = swapchainPtr->getImageViews()->at(currentImage);
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue = clearValue;
        renderingAttachments.push_back(colorAttachment);

        //depth buffer attachment
        VkRenderingAttachmentInfo depthAttachment = {};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.pNext = NULL;
        depthAttachment.imageView = swapchainPtr->getDepthViews().at(currentImage);
        depthAttachment.imageLayout = swapchainPtr->getDepthLayout();
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.clearValue = depthClear;
        
        VkRenderingAttachmentInfo stencilAttachment = {};
        stencilAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        stencilAttachment.pNext = NULL;

        VkRenderingInfoKHR renderInfo = {};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
        renderInfo.pNext = NULL;
        renderInfo.flags = 0;
        renderInfo.renderArea = renderArea;
        renderInfo.layerCount = 1;
        renderInfo.viewMask = 0;
        renderInfo.colorAttachmentCount = renderingAttachments.size();
        renderInfo.pColorAttachments = renderingAttachments.data();
        renderInfo.pDepthAttachment = &depthAttachment;
        renderInfo.pStencilAttachment = &stencilAttachment;

        VkImageMemoryBarrier2 colorImageBarrier = {};
        colorImageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        colorImageBarrier.pNext = NULL;
        colorImageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        colorImageBarrier.srcAccessMask = VK_ACCESS_NONE;
        colorImageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        colorImageBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        colorImageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorImageBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        colorImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        colorImageBarrier.image = swapchainPtr->getImages()->at(currentImage);
        colorImageBarrier.subresourceRange =  {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        VkDependencyInfo dependencyInfo = {};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.pNext = NULL;
        dependencyInfo.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dependencyInfo.memoryBarrierCount = 0;
        dependencyInfo.pMemoryBarriers = NULL;
        dependencyInfo.bufferMemoryBarrierCount = 0;
        dependencyInfo.pBufferMemoryBarriers = NULL;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &colorImageBarrier;
        
        vkCmdPipelineBarrier2(graphicsCmdBuffer, &dependencyInfo);

        vkCmdBeginRendering(graphicsCmdBuffer, &renderInfo);

        //dynamic viewport and scissor specified in pipelines
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)(swapchainPtr->getExtent().width);
        viewport.height = (float)(swapchainPtr->getExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vkCmdSetViewportWithCount(graphicsCmdBuffer, 1, &viewport);
        vkCmdSetScissorWithCount(graphicsCmdBuffer, 1, &renderArea);

        //record draw commands
        for(const auto& [material, materialNode] : renderTree) //material
        {
            bindMaterial(material, graphicsCmdBuffer);

            for(const auto& [materialInstance, instanceNode] : materialNode.instances) //material instances
            {
                bindMaterialInstance(materialInstance, graphicsCmdBuffer);
                drawIndexedIndirect(graphicsCmdBuffer, instanceNode.objectBuffer.get());
            }
        }

        composeAttachments(graphicsCmdBuffer);
        incrementFrameCounter(graphicsCmdBuffer);
    }

    CullingFrustum RenderPass::createCullingFrustum()
    {
        glm::mat4 projectionT = transpose(cameraPtr->getProjection());
        
		glm::vec4 frustumX = normalizePlane(projectionT[3] + projectionT[0]);
		glm::vec4 frustumY = normalizePlane(projectionT[3] + projectionT[1]);

        CullingFrustum frustum;
        frustum.frustum[0] = frustumX.x;
		frustum.frustum[1] = frustumX.z;
		frustum.frustum[2] = frustumY.y;
		frustum.frustum[3] = frustumY.z;
        frustum.zPlanes = glm::vec2(cameraPtr->getClipNear(), cameraPtr->getClipFar());
        
        return frustum;
    }

    CommandBuffer RenderPass::submitPreprocess()
    {
        //fill in uniform buffer
        CullingInputData preprocessInputData;
        preprocessInputData.bufferAddress = renderingData.at(currentImage)->bufferData->getBufferDeviceAddress();
        preprocessInputData.camPos = glm::vec4(cameraPtr->getTranslation().position, 1.0f);
        preprocessInputData.projection = cameraPtr->getProjection();
        preprocessInputData.view = cameraPtr->getViewMatrix();
        preprocessInputData.objectCount = renderingData.at(currentImage)->objectCount;
        preprocessInputData.frustumData = createCullingFrustum();
        preprocessUniformBuffers.at(currentImage)->updateUniformBuffer(&preprocessInputData, sizeof(CullingInputData));
        uint32_t bbc = sizeof(ShaderInputObject);
        
        VkCommandBufferBeginInfo commandInfo;
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandInfo.pNext = NULL;
        commandInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        commandInfo.pInheritanceInfo = NULL;

        VkCommandBuffer cullingCmdBuffer = commandsPtr->getCommandBuffer(CmdPoolType::COMPUTE);
        vkBeginCommandBuffer(cullingCmdBuffer, &commandInfo);

        vkCmdBindPipeline(cullingCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, meshPreprocessPipeline->getPipeline());

        //bind descriptor set (only 1)
        VkDescriptorSet set0Descriptor = descriptorsPtr->allocateDescriptorSet(meshPreprocessPipeline->getDescriptorSetLayouts().at(0), currentImage);

        //set0 - binding 0: input data
        descriptorsPtr->writeUniform(
            preprocessUniformBuffers.at(currentImage)->getBuffer(),
            sizeof(CullingInputData),
            0,
            0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            set0Descriptor);

        //set0 - binding 1: input objects
        descriptorsPtr->writeUniform(
            renderingData.at(currentImage)->bufferData->getBuffer(),
            renderingData.at(currentImage)->inputObjectsRegion.size,
            renderingData.at(currentImage)->inputObjectsRegion.dstOffset,
            1,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            set0Descriptor);

        vkCmdBindDescriptorSets(cullingCmdBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            meshPreprocessPipeline->getLayout(),
            0, //bind point
            1,
            &set0Descriptor,
            0,
            0);

        int groupcount = ((renderingData.at(currentImage)->objectCount) / 128) + 1;
        vkCmdDispatch(cullingCmdBuffer, groupcount, 1, 1);

        vkEndCommandBuffer(cullingCmdBuffer);

        VkSemaphoreSubmitInfo semaphoreWaitInfo = {};
        semaphoreWaitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        semaphoreWaitInfo.pNext = NULL;
        semaphoreWaitInfo.semaphore = bufferCopySemaphores.at(currentImage);
        semaphoreWaitInfo.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        semaphoreWaitInfo.deviceIndex = 0;

        VkCommandBufferSubmitInfo cmdBufferSubmitInfo = {};
        cmdBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdBufferSubmitInfo.pNext = NULL;
        cmdBufferSubmitInfo.commandBuffer = cullingCmdBuffer;
        cmdBufferSubmitInfo.deviceMask = 0;

        VkSemaphoreSubmitInfo semaphoreSignalInfo = {};
        semaphoreSignalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        semaphoreSignalInfo.pNext = NULL;
        semaphoreSignalInfo.semaphore = cullingSemaphores.at(currentImage);
        semaphoreSignalInfo.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        semaphoreSignalInfo.deviceIndex = 0;

        VkSubmitInfo2 submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.pNext = NULL;
        submitInfo.flags = 0;
        submitInfo.waitSemaphoreInfoCount = 1;
        submitInfo.pWaitSemaphoreInfos = &semaphoreWaitInfo;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &cmdBufferSubmitInfo;
        submitInfo.signalSemaphoreInfoCount = 1;
        submitInfo.pSignalSemaphoreInfos = &semaphoreSignalInfo;

        vkQueueSubmit2(devicePtr->getQueues().compute.at(0), 1, &submitInfo, cullingFences.at(currentImage));

        return { cullingCmdBuffer, COMPUTE };
    }

    void RenderPass::composeAttachments(const VkCommandBuffer &cmdBuffer)
    {

    }

    void RenderPass::drawIndexedIndirect(const VkCommandBuffer& cmdBuffer, IndirectDrawContainer* drawBuffer)
    {
        drawBuffer->draw(cmdBuffer, renderingData.at(currentImage).get(), currentImage);
    }

    void RenderPass::bindMaterial(Material const* material, const VkCommandBuffer &cmdBuffer)
    {
        StorageBuffer const* lightsBuffer = renderingData.at(currentImage)->bufferData.get();
        uint32_t lightsOffset = renderingData.at(currentImage)->lightsOffset;
        material->bindPipeline(cmdBuffer, *lightsBuffer, lightsOffset, renderingData.at(currentImage)->lightCount, *lightingInfoBuffers.at(currentImage).get(), currentImage);
    }

    void RenderPass::bindMaterialInstance(MaterialInstance const* materialInstance, const VkCommandBuffer& cmdBuffer)
    {
        materialInstance->bind(cmdBuffer, currentImage);
    }

    void RenderPass::incrementFrameCounter(const VkCommandBuffer& cmdBuffer)
    {
        //end render "pass"
        vkCmdEndRendering(cmdBuffer);

        VkImageMemoryBarrier2 imageBarrier = {};
        imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imageBarrier.pNext = NULL;
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_NONE;
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.image = swapchainPtr->getImages()->at(currentImage);
        imageBarrier.subresourceRange =  {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        VkDependencyInfo dependencyInfo = {};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.pNext = NULL;
        dependencyInfo.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dependencyInfo.memoryBarrierCount = 0;
        dependencyInfo.pMemoryBarriers = NULL;
        dependencyInfo.bufferMemoryBarrierCount = 0;
        dependencyInfo.pBufferMemoryBarriers = NULL;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &imageBarrier;
        
        vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);

        vkEndCommandBuffer(cmdBuffer);

        //submit rendering to GPU
        std::vector<VkSemaphoreSubmitInfo> graphicsWaitSemaphores;
        VkSemaphoreSubmitInfo graphicsSemaphoreWaitInfo0 = {};
        graphicsSemaphoreWaitInfo0.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        graphicsSemaphoreWaitInfo0.pNext = NULL;
        graphicsSemaphoreWaitInfo0.semaphore = imageSemaphores.at(currentImage);
        graphicsSemaphoreWaitInfo0.stageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        graphicsSemaphoreWaitInfo0.deviceIndex = 0;
        graphicsWaitSemaphores.push_back(graphicsSemaphoreWaitInfo0);

        VkSemaphoreSubmitInfo graphicsSemaphoreWaitInfo1 = {};
        graphicsSemaphoreWaitInfo1.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        graphicsSemaphoreWaitInfo1.pNext = NULL;
        graphicsSemaphoreWaitInfo1.semaphore = cullingSemaphores.at(currentImage);
        graphicsSemaphoreWaitInfo1.stageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        graphicsSemaphoreWaitInfo1.deviceIndex = 0;
        graphicsWaitSemaphores.push_back(graphicsSemaphoreWaitInfo1);

        VkCommandBufferSubmitInfo graphicsCmdBufferSubmitInfo = {};
        graphicsCmdBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        graphicsCmdBufferSubmitInfo.pNext = NULL;
        graphicsCmdBufferSubmitInfo.commandBuffer = cmdBuffer;
        graphicsCmdBufferSubmitInfo.deviceMask = 0;

        std::vector<VkSemaphoreSubmitInfo> graphicsSignalSemaphores;
        VkSemaphoreSubmitInfo graphicsSemaphoreSignalInfo = {};
        graphicsSemaphoreSignalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        graphicsSemaphoreSignalInfo.pNext = NULL;
        graphicsSemaphoreSignalInfo.semaphore = renderSemaphores.at(currentImage);
        graphicsSemaphoreSignalInfo.stageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        graphicsSemaphoreSignalInfo.deviceIndex = 0;
        graphicsSignalSemaphores.push_back(graphicsSemaphoreSignalInfo);
        
        VkSubmitInfo2 graphicsSubmitInfo = {};
        graphicsSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        graphicsSubmitInfo.pNext = NULL;
        graphicsSubmitInfo.flags = 0;
        graphicsSubmitInfo.waitSemaphoreInfoCount = graphicsWaitSemaphores.size();
        graphicsSubmitInfo.pWaitSemaphoreInfos = graphicsWaitSemaphores.data();
        graphicsSubmitInfo.commandBufferInfoCount = 1;
        graphicsSubmitInfo.pCommandBufferInfos = &graphicsCmdBufferSubmitInfo;
        graphicsSubmitInfo.signalSemaphoreInfoCount = 1;
        graphicsSubmitInfo.pSignalSemaphoreInfos = &graphicsSemaphoreSignalInfo;

        vkQueueSubmit2(devicePtr->getQueues().graphics.at(0), 1, &graphicsSubmitInfo, renderFences.at(currentImage));

        fenceCmdBuffers.at(currentImage).push_back({cmdBuffer, GRAPHICS});

        VkResult returnResult;
        VkPresentInfoKHR presentSubmitInfo = {};
        presentSubmitInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentSubmitInfo.pNext = NULL;
        presentSubmitInfo.waitSemaphoreCount = 1;
        presentSubmitInfo.pWaitSemaphores = &renderSemaphores.at(currentImage);
        presentSubmitInfo.swapchainCount = 1;
        presentSubmitInfo.pSwapchains = swapchainPtr->getSwapchainPtr();
        presentSubmitInfo.pImageIndices = &currentImage;
        presentSubmitInfo.pResults = &returnResult;

        VkResult presentResult = vkQueuePresentKHR(devicePtr->getQueues().present.at(0), &presentSubmitInfo);

        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || recreateFlag) 
        {
            recreateFlag = false;
            swapchainPtr->recreate();
            vkDestroyFence(devicePtr->getDevice(), renderFences.at(currentImage), nullptr);
            renderFences.at(currentImage) = commandsPtr->getSignaledFence();
            vkDestroyFence(devicePtr->getDevice(), cullingFences.at(currentImage), nullptr);
            cullingFences.at(currentImage) = commandsPtr->getSignaledFence();
            vkDestroySemaphore(devicePtr->getDevice(), imageSemaphores.at(currentImage), nullptr);
            imageSemaphores.at(currentImage) = commandsPtr->getSemaphore();
            cameraPtr->updateCameraProjection();

            return;
        }

        if(currentImage == 0)
        {
            currentImage = 1;
        }
        else
        {
            currentImage = 0;
        }
    }

    //----------OBJECT ADD/REMOVE FUNCTIONS----------//

    std::list<ModelInstance*>::iterator RenderPass::addModelInstance(ModelInstance* instance)
    {
        renderingModels[(Model*)instance->getModelPtr()].push_back(instance);
        std::list<ModelInstance*>::iterator reference = renderingModels[instance->getModelPtr()].end();
        reference--;

        return reference;
    }

    void RenderPass::removeModelInstance(std::list<ModelInstance*>::iterator reference)
    {
        renderingModels[(*reference)->getModelPtr()].erase(reference);
    }
}