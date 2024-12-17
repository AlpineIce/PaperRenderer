#include "GuiRender.h"

GuiContext initImGui(PaperRenderer::RenderEngine& renderer)
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
        .DescriptorPoolSize = 1,
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

    return { imGuiQueue, &io };
}

void renderImGui(PaperRenderer::RenderEngine* renderer, GuiContext guiContext, PaperRenderer::SynchronizationInfo syncInfo)
{
    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 0.5f);

    // Start the Dear ImGui frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
    if(show_demo_window)
        ImGui::ShowDemoWindow(&show_demo_window);

    // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
    {
        static float f = 0.0f;
        static int counter = 0;

        ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

        ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
        ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
        ImGui::Checkbox("Another Window", &show_another_window);

        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
        ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

        if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
            counter++;
        ImGui::SameLine();
        ImGui::Text("counter = %d", counter);

        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / guiContext.io->Framerate, guiContext.io->Framerate);
        ImGui::End();
    }

    // 3. Show another simple window.
    if (show_another_window)
    {
        ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
        ImGui::Text("Hello from another window!");
        if (ImGui::Button("Close Me"))
            show_another_window = false;
        ImGui::End();
    }

    //----------ATTACHMENTS----------//
        
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

