#pragma once
#include "Device.h"

#include <vector>
#include <functional>

namespace PaperRenderer
{
    enum WindowMode
    {
        WINDOWED = 0,
        BORDERLESS = 1,
        FULLSCREEN = 2
    };

    struct WindowState
    {
        std::string windowName = "Set window name in swapchain creation";
        unsigned int resX = 1280;
        unsigned int resY = 720;
        WindowMode windowMode = WINDOWED;
        GLFWmonitor* monitor = NULL;
        bool enableVsync = false;
    };

    class Swapchain
    {
    private:
        VkExtent2D swapchainExtent;
        VkFormat swapchainImageFormat;
        VkColorSpaceKHR imageColorSpace;
        VkPresentModeKHR presentationMode;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        std::vector<VkImage> swapchainImages;
        std::vector<VkImageView> imageViews;
        GLFWwindow* window = NULL;
        WindowState currentWindowState;
        bool usingHDR = false;

        static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
        std::function<void(VkExtent2D newExtent)> swapchainRebuildCallback = NULL;

        Device* devicePtr;
        
        void buildSwapchain();
        void createImageViews();

    public:
        Swapchain(Device* device, WindowState startingWindowState);
        ~Swapchain();

        void recreate();
        void setSwapchainRebuildCallback(std::function<void(VkExtent2D newExtent)> callbackFunction) { this->swapchainRebuildCallback = callbackFunction; }

        GLFWwindow* getGLFWwindow() const { return window; }
        const WindowState& getWindowState() const { return currentWindowState; }
        const std::vector<VkImageView>& getImageViews() const { return imageViews; }
        const std::vector<VkImage>& getImages() const { return swapchainImages; }
        const VkFormat& getFormat() const { return swapchainImageFormat; }
        const VkSwapchainKHR& getSwapchain() const { return swapchain; }
        const VkExtent2D& getExtent() const { return swapchainExtent; } //AKA resolution
        const bool& getIsUsingHDR() const { return usingHDR; }
    };
}