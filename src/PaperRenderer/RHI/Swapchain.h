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

        Device* devicePtr;
        Window* windowPtr;
        bool vsync;

        void buildSwapchain();
        void createImageViews();

    public:
        Swapchain(Device* device, Window* window, bool enableVsync);
        ~Swapchain();

        void recreate();

        std::vector<VkImageView> const* getImageViews() { return &imageViews; }
        std::vector<VkImage> const* getImages() { return &swapchainImages; }
        const VkFormat& getFormat() { return swapchainImageFormat; }
        const VkSwapchainKHR& getSwapchain() { return swapchain; }
        const VkExtent2D& getExtent() const { return swapchainExtent; } //AKA resolution
    };
}