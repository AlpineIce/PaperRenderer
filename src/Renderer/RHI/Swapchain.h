#pragma once
#include "Device.h"
#include "Window.h"

#include <vector>

namespace Renderer
{
    class Swapchain
    {
    private:
        VkExtent2D swapchainExtent;
        VkFormat swapchainImageFormat;
        VkColorSpaceKHR imageColorSpace;
        VkPresentModeKHR presentationMode;
        VkSwapchainKHR swapchain;
        std::vector<VkImage> swapchainImages;
        std::vector<VkImageView> imageViews;

        //depth buffer
        std::vector<VkImage> depthBufferImages;
        std::vector<VkImageView> depthBufferViews;
        VkImageLayout depthBufferLayout;
        VkFormat depthBufferFormat;
        std::vector<VmaAllocation> depthBufferAllocations;
        
        Device* devicePtr;
        Window* windowPtr;
        bool vsync;

        void buildSwapchain();
        void createImageViews();
        void createDepthBuffer();

    public:
        Swapchain(Device* device, Window* window, bool enableVsync);
        ~Swapchain();

        void recreate();

        std::vector<VkImageView>* getImageViews() { return &imageViews; }
        std::vector<VkImage>* getImages() { return &swapchainImages; }
        VkFormat* getFormatPtr() { return &swapchainImageFormat; }
        VkSwapchainKHR* getSwapchainPtr() { return &swapchain; }
        VkExtent2D getExtent() const { return swapchainExtent; }

        //depth buffer
        const std::vector<VkImageView>& getDepthViews() const { return depthBufferViews; }
        VkImageLayout getDepthLayout() const { return depthBufferLayout; }
        VkFormat getDepthFormat() const { return depthBufferFormat; }
    };
}