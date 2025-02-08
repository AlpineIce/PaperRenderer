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
        VkSurfaceFormatKHR surfaceFormat = { VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }; //if not available, will one will be chosen instead
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR; //if not available, will one will be chosen instead
        VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    };

    class Swapchain
    {
    private:
        VkExtent2D swapchainExtent = { 0, 0 };
        WindowState windowState;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        uint32_t minImageCount = 0;
        uint32_t imageCount = 0;
        std::vector<VkImage> swapchainImages = {};
        std::vector<VkImageView> imageViews = {};
        std::vector<VkSemaphore> imageSemaphores = {};
        uint32_t frameIndex = 0;
        uint32_t semaphoreIndex = 0;
        GLFWwindow* window = NULL;

        static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
        const std::function<void(RenderEngine&, VkExtent2D newExtent)> swapchainRebuildCallback = NULL;

        class RenderEngine& renderer;
        
        void buildSwapchain();
        void createImageViews();

    public:
        Swapchain(class RenderEngine& renderer, const std::function<void(RenderEngine&, VkExtent2D newExtent)>& swapchainRebuildCallbackFunction, const WindowState& startingWindowState);
        ~Swapchain();
        Swapchain(const Swapchain&) = delete;

        //returns the image acquire semaphore
        const VkSemaphore& acquireNextImage();
        void presentImage(const std::vector<VkSemaphore>& waitSemaphores);
        void setWindowState(const WindowState& newState);
        void recreate();

        GLFWwindow* getGLFWwindow() const { return window; }
        const WindowState& getWindowState() const { return windowState; }
        const VkImageView& getCurrentImageView() const { return imageViews[frameIndex]; }
        const VkImage& getCurrentImage() const { return swapchainImages[frameIndex]; }
        const VkSwapchainKHR& getSwapchain() const { return swapchain; }
        const uint32_t& getMinImageCount() const { return minImageCount; }
        const uint32_t& getImageCount() const { return imageCount; }
        const VkExtent2D& getExtent() const { return swapchainExtent; } //AKA resolution
        const uint32_t& getSwapchainImageIndex() const { return frameIndex; }
    };
}