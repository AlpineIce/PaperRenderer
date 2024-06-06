#pragma once
#include "Device.h"
#include "Window.h"

#include <vector>

namespace PaperRenderer
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
        std::unique_ptr<PaperMemory::DeviceAllocation> depthBufferAllocation;
        std::unique_ptr<PaperMemory::Image> depthBufferImage;
        VkImageView depthBufferView;
        VkImageLayout depthBufferLayout;
        VkFormat depthBufferFormat;
        
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
        const VkFormat& getFormat() { return swapchainImageFormat; }
        const VkSwapchainKHR& getSwapchain() { return swapchain; }
        VkExtent2D getExtent() const { return swapchainExtent; }

        //depth buffer
        const VkImageView& getDepthView() const { return depthBufferView; }
        VkImageLayout getDepthLayout() const { return depthBufferLayout; }
        VkFormat getDepthFormat() const { return depthBufferFormat; }
    };
}