#pragma once
#include "../src/PaperRenderer/PaperRenderer.h"

//imgui
#include "lib/imgui/backends/imgui_impl_vulkan.h"
#include "lib/imgui/imgui.h"
#include "lib/imgui/backends/imgui_impl_glfw.h"

struct GuiContext
{
    PaperRenderer::Queue* imGuiQueue = NULL;
    ImGuiIO* io = NULL;
};

GuiContext initImGui(PaperRenderer::RenderEngine& renderer);

void renderImGui(PaperRenderer::RenderEngine* renderer, GuiContext guiContext, PaperRenderer::SynchronizationInfo syncInfo);

void destroyImGui();
