#include "GuiRender.h"

#include <format>

GuiContext initImGui(PaperRenderer::RenderEngine& renderer, DefaultMaterialInstance& adjustableMaterial)
{
    //get imgui queue (workaround for PaperRenderer)
    PaperRenderer::Queue* imGuiQueue = *renderer.getDevice().getQueues().at(PaperRenderer::QueueType::GRAPHICS).queues.rbegin();

    // Setup Dear ImGui context
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForVulkan(renderer.getSwapchain().getGLFWwindow(), true);
    ImGui_ImplVulkan_InitInfo init_info = {
        .Instance = renderer.getDevice().getInstance(),
        .PhysicalDevice = renderer.getDevice().getGPU(),
        .Device = renderer.getDevice().getDevice(),
        .QueueFamily = (uint32_t)renderer.getDevice().getQueueFamiliesIndices().graphicsFamilyIndex,
        .Queue = imGuiQueue->queue,
        .MinImageCount = renderer.getSwapchain().getMinImageCount(),
        .ImageCount = renderer.getSwapchain().getImageCount(),
        .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
        .PipelineCache = renderer.getPipelineBuilder().getPipelineCache(),
        .DescriptorPoolSize = 1000,
        .UseDynamicRendering = true,
        .PipelineRenderingCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .pNext = NULL,
            .viewMask = 0,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &renderer.getSwapchain().getFormat(),
            .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
            .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
        }
    };

    ImGui_ImplVulkan_Init(&init_info);

    ImGui_ImplVulkan_CreateFontsTexture();

    return { imGuiQueue, &io, {}, &adjustableMaterial, true };
}

void renderImGui(PaperRenderer::RenderEngine* renderer, PaperRenderer::Statistics const* lastFrameStatistics, GuiContext* guiContext, PaperRenderer::SynchronizationInfo syncInfo)
{
    // Start the Dear ImGui frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    //----------BEGIN WINDOW----------//

    static float f = 0.0f;
    static int counter = 0;

    //begin main window
    ImGui::Begin("PaperRenderer Example GUI");

    //material adjustment
    ImGui::SeparatorText("Adjust test material");

    MaterialParameters materialParameters = guiContext->adjustableMaterial->getParameters();

    ImGui::ColorEdit4("Base color", (float*)&materialParameters.baseColor); // Edit 3 floats representing a color
    ImGui::ColorEdit4("Emission", (float*)&materialParameters.emission); // Edit 4 floats representing a color
    ImGui::SliderFloat("Metallic", &materialParameters.metallic, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
    ImGui::SliderFloat("Roughness", &materialParameters.roughness, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f

    guiContext->adjustableMaterial->setParameters(materialParameters);

    //render mode
    if(renderer->getDevice().getRTSupport())
    {
        ImGui::SeparatorText("Adjust Render Mode");

        const std::string renderModeSwitchText = std::string("Switch to ") + std::string(!guiContext->raster ? "Raster" : "Ray Tracing");
        if(ImGui::Button(renderModeSwitchText.c_str()))
        {
            vkDeviceWaitIdle(renderer->getDevice().getDevice());
            guiContext->raster = !guiContext->raster;
        }
    }
    
    //list last frame statistics
    ImGui::SeparatorText("Last Frame CPU Statistics");
    for(const PaperRenderer::TimeStatistic& time : lastFrameStatistics->timeStatistics)
    {
        if(time.interval == PaperRenderer::TimeStatisticInterval::REGULAR)
        {
            std::string ms = std::format(": {:.3f}ms", time.getTime() * 1000.0);
            ImGui::Text((time.name + ms).c_str());
        }
        else
        {
            //add irregular event
            guiContext->irregularTimeEvents.push_back({ time, std::chrono::high_resolution_clock::now() });
        }
    }

    //list regular statistics
    ImGui::SeparatorText("Irregular Event CPU Statistics");
    for(const GuiContext::GuiIrregularTimeStatistic& time : guiContext->irregularTimeEvents)
    {
        //remove event if its old
        if((std::chrono::high_resolution_clock::now() - time.from).count() > 7000000000.0) //7 seconds
        {
            guiContext->irregularTimeEvents.pop_front();
        }
        else
        {
            std::string ms = std::format(": {:.3f}ms", time.statistic.getTime() * 1000.0);
            ImGui::Text((time.statistic.name + ms).c_str());
        }
    }

    //list total frame time
    ImGui::SeparatorText("Total Frame Time (GPU/CPU)");
    ImGui::Text("%.3fms    (%.1f FPS)", 1000.0f / guiContext->io->Framerate, guiContext->io->Framerate);

    //end
    ImGui::End();

    //----------END WINDOW, BEGIN RENDERING----------//
 
    //color attachment
    std::vector<VkRenderingAttachmentInfo> colorAttachments;
    colorAttachments.push_back({    //output
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = NULL,
        .imageView = renderer->getSwapchain().getCurrentImageView(),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { 0.0f, 0.0f, 0.0f, 0.0f } 
    });


    //----------VIEWPORT SCISSORS AND RENDER AREA----------//

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)(renderer->getSwapchain().getExtent().width);
    viewport.height = (float)(renderer->getSwapchain().getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissors = {};
    scissors.extent = renderer->getSwapchain().getExtent();
    scissors.offset = { 0, 0 };

    VkRect2D renderArea = {};
    renderArea.offset = { 0, 0 };
    renderArea.extent = renderer->getSwapchain().getExtent();

    //----------RENDER----------//

    VkCommandBuffer cmdBuffer = renderer->getDevice().getCommands().getCommandBuffer(PaperRenderer::QueueType::GRAPHICS);

    VkCommandBufferBeginInfo cmdBufferBeginInfo = {};
    cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufferBeginInfo.pNext = NULL;
    cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    cmdBufferBeginInfo.pInheritanceInfo = NULL;
    
    vkBeginCommandBuffer(cmdBuffer, &cmdBufferBeginInfo);

    //rendering
    VkRenderingInfo renderInfo = {};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    renderInfo.pNext = NULL;
    renderInfo.flags = 0;
    renderInfo.renderArea = renderArea;
    renderInfo.layerCount = 1;
    renderInfo.viewMask = 0;
    renderInfo.colorAttachmentCount = colorAttachments.size();
    renderInfo.pColorAttachments = colorAttachments.data();
    renderInfo.pDepthAttachment = NULL;
    renderInfo.pStencilAttachment = NULL;

    vkCmdBeginRendering(cmdBuffer, &renderInfo);

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuffer);

    vkCmdEndRendering(cmdBuffer);

    //end command buffer and submit
    vkEndCommandBuffer(cmdBuffer);

    renderer->getDevice().getCommands().unlockCommandBuffer(cmdBuffer);

    renderer->getDevice().getCommands().submitToQueue(syncInfo, { cmdBuffer });
}

void destroyImGui()
{
    //end ImGui
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

