#include "Swapchain.h"
#include "PaperRenderer.h"

#include <algorithm>
#include <functional>

namespace PaperRenderer
{
    Swapchain::Swapchain(RenderEngine* renderer, WindowState startingWindowState)
        :rendererPtr(renderer),
        currentWindowState(startingWindowState)
    {
        //----------WINDOW CREATION----------//

        if(glfwVulkanSupported() != GLFW_TRUE) throw std::runtime_error("No vulkan support for GLFW");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        if(currentWindowState.monitor == NULL)
        {
            currentWindowState.monitor = glfwGetPrimaryMonitor();
        }
        const GLFWvidmode* mode = glfwGetVideoMode(currentWindowState.monitor);

        switch(currentWindowState.windowMode)
        {
            case WINDOWED:
                window = glfwCreateWindow(currentWindowState.resX, currentWindowState.resY, currentWindowState.windowName.c_str(), NULL, NULL);

                break;
            case BORDERLESS:
                glfwWindowHint(GLFW_RED_BITS, mode->redBits);
                glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
                glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
                glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
                window = glfwCreateWindow(mode->width, mode->height, currentWindowState.windowName.c_str(), currentWindowState.monitor, NULL);

                currentWindowState.resX = mode->width;
                currentWindowState.resY = mode->height;

                break;
            case FULLSCREEN:
                window = glfwCreateWindow(currentWindowState.resX, currentWindowState.resY, currentWindowState.windowName.c_str(), currentWindowState.monitor, NULL);

                break;
        }

        //surface and device creation
        VkResult result = glfwCreateWindowSurface(rendererPtr->getDevice()->getInstance(), window, nullptr, (VkSurfaceKHR*)(&rendererPtr->getDevice()->getSurface()));
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Window surface creation failed");
        }
        rendererPtr->getDevice()->createDevice();

        //----------PRESENT MODE----------//

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(rendererPtr->getDevice()->getGPU(), rendererPtr->getDevice()->getSurface(), &presentModeCount, nullptr);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(rendererPtr->getDevice()->getGPU(), rendererPtr->getDevice()->getSurface(), &presentModeCount, presentModes.data());

        for(VkPresentModeKHR presentMode : presentModes)
        {
            if(presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR && !startingWindowState.enableVsync)
            {
                this->presentationMode = presentMode;
                break;
            }
            if(presentMode == VK_PRESENT_MODE_FIFO_KHR && startingWindowState.enableVsync)
            {
                this->presentationMode = presentMode;
                break;
            }
        }

        //build swapchian
        buildSwapchain();

        //set glfw callback 
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

        //sync
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(rendererPtr->getDevice()->getGPU(), rendererPtr->getDevice()->getSurface(), &capabilities);

        imageSemaphores.resize(capabilities.minImageCount);
        for(uint32_t i = 0; i < capabilities.minImageCount; i++)
        {
            imageSemaphores.at(i) = PaperRenderer::Commands::getSemaphore(rendererPtr);
        }
    }

    Swapchain::~Swapchain()
    {
        //images
        for(VkImageView image : imageViews)
        {
            vkDestroyImageView(rendererPtr->getDevice()->getDevice(), image, nullptr);
        }

        vkDestroySwapchainKHR(rendererPtr->getDevice()->getDevice(), swapchain, nullptr);
        for(VkSemaphore semaphore : imageSemaphores)
        {
            vkDestroySemaphore(rendererPtr->getDevice()->getDevice(), semaphore, nullptr);
        }

        //glfw window and surface
        vkDestroySurfaceKHR(rendererPtr->getDevice()->getInstance(), rendererPtr->getDevice()->getSurface(), nullptr);
        glfwDestroyWindow(window);
    }

    const VkSemaphore& Swapchain::acquireNextImage(uint32_t currentImage)
    {
        //get available image
        VkResult imageAcquireResult = vkAcquireNextImageKHR(
            rendererPtr->getDevice()->getDevice(),
            swapchain,
            UINT64_MAX,
            imageSemaphores.at(currentImage),
            VK_NULL_HANDLE,
            &frameIndex);

        if(imageAcquireResult == VK_ERROR_OUT_OF_DATE_KHR || imageAcquireResult == VK_SUBOPTIMAL_KHR)
        {
            recreate();
            acquireNextImage(frameIndex);
        }

        return imageSemaphores.at(currentImage);
    }

    void Swapchain::presentImage(const std::vector<VkSemaphore>& waitSemaphores)
    {
        VkResult returnResult;
        VkPresentInfoKHR presentSubmitInfo = {};
        presentSubmitInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentSubmitInfo.pNext = NULL;
        presentSubmitInfo.waitSemaphoreCount = waitSemaphores.size();
        presentSubmitInfo.pWaitSemaphores = waitSemaphores.data();
        presentSubmitInfo.swapchainCount = 1;
        presentSubmitInfo.pSwapchains = &swapchain;
        presentSubmitInfo.pImageIndices = &frameIndex;
        presentSubmitInfo.pResults = &returnResult;//&returnResult;

        //too lazy to properly fix this, it probably barely affects performance anyways
        rendererPtr->getDevice()->getQueues().at(QueueType::PRESENT).queues.at(0)->threadLock.lock();
        VkResult presentResult = vkQueuePresentKHR(rendererPtr->getDevice()->getQueues().at(QueueType::PRESENT).queues.at(0)->queue, &presentSubmitInfo);
        rendererPtr->getDevice()->getQueues().at(QueueType::PRESENT).queues.at(0)->threadLock.unlock();

        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) 
        {
            recreate();
        }
    }

    void Swapchain::framebufferResizeCallback(GLFWwindow *window, int width, int height)
    {
        Swapchain* thisPtr = (Swapchain*)glfwGetWindowUserPointer(window);
        thisPtr->currentWindowState.resX = width;
        thisPtr->currentWindowState.resY = height;
        thisPtr->recreate();
    }

    void Swapchain::buildSwapchain()
    {
        //----------COLOR SPACE SELECTION----------//

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(rendererPtr->getDevice()->getGPU(), rendererPtr->getDevice()->getSurface(), &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(rendererPtr->getDevice()->getGPU(), rendererPtr->getDevice()->getSurface(), &formatCount, surfaceFormats.data());
        
        this->swapchainImageFormat = VK_FORMAT_UNDEFINED;

        //look for an HDR format first
        usingHDR = false;
        for(VkSurfaceFormatKHR surfaceFormat : surfaceFormats)
        {
            if(surfaceFormat.colorSpace == VK_COLOR_SPACE_DOLBYVISION_EXT) //i dont even know if this is supported by any mainstream desktop OS
            {
                usingHDR = true;
                this->swapchainImageFormat = surfaceFormat.format;
                this->imageColorSpace = surfaceFormat.colorSpace;

                break;
            }
            else if(surfaceFormat.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) 
            {
                usingHDR = true;
                this->swapchainImageFormat = surfaceFormat.format;
                this->imageColorSpace = surfaceFormat.colorSpace;

                break;
            }
        }

        //use SRGB if no HDR format is available
        if(!usingHDR)
        {
            for(VkSurfaceFormatKHR surfaceFormat : surfaceFormats)
            {
                if(surfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR && !usingHDR) //SRGB color space
                {
                    if(surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB)
                    {
                        this->swapchainImageFormat = surfaceFormat.format;
                        this->imageColorSpace = surfaceFormat.colorSpace;
                        break;
                    }
                    else if(surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM)
                    {
                        this->swapchainImageFormat = surfaceFormat.format;
                        this->imageColorSpace = surfaceFormat.colorSpace;
                    }
                }
            }
        }

        //make sure a format is selected and the available formats is greater than 0
        if(this->swapchainImageFormat == VK_FORMAT_UNDEFINED && formatCount > 0)
        {
            this->swapchainImageFormat = surfaceFormats.at(0).format;
            this->imageColorSpace = surfaceFormats.at(0).colorSpace;
        }
        else if(formatCount == 0)
        {
            throw std::runtime_error("Swapchain image format unavailable");
        }

        //----------BUILD----------//

        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(rendererPtr->getDevice()->getGPU(), rendererPtr->getDevice()->getSurface(), &capabilities);
        this->swapchainExtent.width = std::min(currentWindowState.resX, capabilities.maxImageExtent.width);
        this->swapchainExtent.height = std::min(currentWindowState.resY, capabilities.maxImageExtent.height);

        VkSwapchainCreateInfoKHR swapchainInfo = {};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.pNext = NULL;
        swapchainInfo.flags =  0;
        swapchainInfo.surface = rendererPtr->getDevice()->getSurface();
        swapchainInfo.minImageCount = std::max(capabilities.minImageCount, (uint32_t)2);
        swapchainInfo.imageFormat = this->swapchainImageFormat;
        swapchainInfo.imageColorSpace = this->imageColorSpace;
        swapchainInfo.imageExtent = this->swapchainExtent;
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        uint32_t queueFamilies[] = {rendererPtr->getDevice()->getQueues().at(QueueType::GRAPHICS).queueFamilyIndex,
                                    rendererPtr->getDevice()->getQueues().at(QueueType::PRESENT).queueFamilyIndex};
        if(queueFamilies[0] == queueFamilies[1])
        {
            swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            swapchainInfo.queueFamilyIndexCount = 0;
            swapchainInfo.pQueueFamilyIndices = nullptr;
        }
        else
        {
            swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            swapchainInfo.queueFamilyIndexCount = 2;
            swapchainInfo.pQueueFamilyIndices = queueFamilies;
        }
        swapchainInfo.preTransform = capabilities.currentTransform;
        swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainInfo.presentMode = this->presentationMode;
        swapchainInfo.clipped = VK_TRUE;
        swapchainInfo.oldSwapchain = swapchain;
        
        VkResult result = vkCreateSwapchainKHR(rendererPtr->getDevice()->getDevice(), &swapchainInfo, nullptr, &swapchain);
        if(result != VK_SUCCESS) throw std::runtime_error("Swapchain creation/recreation failed");
        
        createImageViews();
    }

    void Swapchain::createImageViews()
    {
        uint32_t imageCount;
        vkGetSwapchainImagesKHR(rendererPtr->getDevice()->getDevice(), swapchain, &imageCount, nullptr);
        swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(rendererPtr->getDevice()->getDevice(), swapchain, &imageCount, swapchainImages.data());

        imageViews.resize(imageCount);

        for(int i = 0; i < imageCount; i++)
        {
            VkImageViewCreateInfo creationInfo;
            creationInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            creationInfo.pNext = NULL;
            creationInfo.flags = 0;
            creationInfo.image = swapchainImages.at(i);
            creationInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            creationInfo.format = swapchainImageFormat;
            creationInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY };
            creationInfo.subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            };

            VkResult result = vkCreateImageView(rendererPtr->getDevice()->getDevice(), &creationInfo, nullptr, &imageViews.at(i));
            if(result != VK_SUCCESS) throw std::runtime_error("Failed to create image views");
        }
    }

    void Swapchain::recreate()
    {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        while (width == 0 || height == 0) {
            glfwWaitEvents();
            glfwGetFramebufferSize(window, &width, &height);
        }

        vkDeviceWaitIdle(rendererPtr->getDevice()->getDevice());
        
        //destruction
        for(VkImageView image : imageViews)
        {
            vkDestroyImageView(rendererPtr->getDevice()->getDevice(), image, nullptr);
        }
        imageViews.clear();

        //rebuild
        VkSwapchainKHR oldSwapchain = swapchain;
        buildSwapchain();
        vkDestroySwapchainKHR(rendererPtr->getDevice()->getDevice(), oldSwapchain, nullptr);

        if(swapchainRebuildCallback) swapchainRebuildCallback(swapchainExtent);
    }
}