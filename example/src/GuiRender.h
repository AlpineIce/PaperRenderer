#pragma once
#include "Common.h"

//imgui
#include "lib/imgui/backends/imgui_impl_vulkan.h"
#include "lib/imgui/imgui.h"
#include "lib/imgui/backends/imgui_impl_glfw.h"

struct GuiContext
{
    struct GuiIrregularTimeStatistic
    {
        PaperRenderer::TimeStatistic statistic;
        std::chrono::high_resolution_clock::time_point from;
    };

    PaperRenderer::Queue* imGuiQueue = NULL;
    ImGuiIO* io = NULL;
    std::deque<GuiIrregularTimeStatistic> irregularTimeEvents;
};

GuiContext initImGui(PaperRenderer::RenderEngine& renderer);

void renderImGui(PaperRenderer::RenderEngine* renderer, PaperRenderer::Statistics const* lastFrameStatistics, GuiContext* guiContext, PaperRenderer::SynchronizationInfo syncInfo);

void destroyImGui();
