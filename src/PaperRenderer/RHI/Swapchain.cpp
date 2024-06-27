#include "Swapchain.h"

namespace PaperRenderer
{
    Swapchain::Swapchain(Device *device, WindowState startingWindowState)
        :devicePtr(device),
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
        VkResult result = glfwCreateWindowSurface(device->getInstance(), window, nullptr, devicePtr->getSurfacePtr());
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Window surface creation failed");
        }
        devicePtr->createDevice();

        //----------PRESENT MODE----------//

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device->getGPU(), *(device->getSurfacePtr()), &presentModeCount, nullptr);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device->getGPU(), *(device->getSurfacePtr()), &presentModeCount, presentModes.data());

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
    }

    Swapchain::~Swapchain()
    {
        //images
        for(VkImageView image : imageViews)
        {
            vkDestroyImageView(devicePtr->getDevice(), image, nullptr);
        }

        vkDestroySwapchainKHR(devicePtr->getDevice(), swapchain, nullptr);

        //glfw window and surface
        vkDestroySurfaceKHR(devicePtr->getInstance(), *devicePtr->getSurfacePtr(), nullptr);
        glfwDestroyWindow(window);
    }

    void Swapchain::buildSwapchain()
    {
        //----------COLOR SPACE SELECTION----------//

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(devicePtr->getGPU(), *(devicePtr->getSurfacePtr()), &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(devicePtr->getGPU(), *(devicePtr->getSurfacePtr()), &formatCount, surfaceFormats.data());

        usingHDR = false;
        this->swapchainImageFormat = VK_FORMAT_UNDEFINED;
        for(VkSurfaceFormatKHR surfaceFormat : surfaceFormats)
        {
            if(surfaceFormat.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) //HDR color space
            {
                //set format and color space
                usingHDR = true;
                this->swapchainImageFormat = surfaceFormat.format;
                this->imageColorSpace = surfaceFormat.colorSpace;

                break;
            }
        }

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
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devicePtr->getGPU(), *(devicePtr->getSurfacePtr()), &capabilities);
        this->swapchainExtent = capabilities.currentExtent;

        VkSwapchainCreateInfoKHR swapchainInfo = {};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.pNext = NULL;
        swapchainInfo.flags =  0;
        swapchainInfo.surface = *(devicePtr->getSurfacePtr());
        swapchainInfo.minImageCount = capabilities.minImageCount;
        swapchainInfo.imageFormat = this->swapchainImageFormat;
        swapchainInfo.imageColorSpace = this->imageColorSpace;
        swapchainInfo.imageExtent = this->swapchainExtent;
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        uint32_t queueFamilies[] = {devicePtr->getQueues().at(PaperMemory::QueueType::GRAPHICS).queueFamilyIndex,
                                    devicePtr->getQueues().at(PaperMemory::QueueType::PRESENT).queueFamilyIndex};
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
        
        VkResult result = vkCreateSwapchainKHR(devicePtr->getDevice(), &swapchainInfo, nullptr, &swapchain);
        if(result != VK_SUCCESS) throw std::runtime_error("Swapchain creation/recreation failed");
        
        createImageViews();
    }

    void Swapchain::createImageViews()
    {
        uint32_t imageCount;
        vkGetSwapchainImagesKHR(devicePtr->getDevice(), swapchain, &imageCount, nullptr);
        swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(devicePtr->getDevice(), swapchain, &imageCount, swapchainImages.data());

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

            VkResult result = vkCreateImageView(devicePtr->getDevice(), &creationInfo, nullptr, &imageViews.at(i));
            if(result != VK_SUCCESS) throw std::runtime_error("Failed to create image views");
        }
    }

    void Swapchain::recreate()
    {
        vkDeviceWaitIdle(devicePtr->getDevice());
        
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        while (width == 0 || height == 0) {
            glfwWaitEvents();
            glfwGetFramebufferSize(window, &width, &height);
        }
        
        //destruction
        for(VkImageView image : imageViews)
        {
            vkDestroyImageView(devicePtr->getDevice(), image, nullptr);
        }
        imageViews.clear();

        //rebuild
        VkSwapchainKHR oldSwapchain = swapchain;
        buildSwapchain();
        vkDestroySwapchainKHR(devicePtr->getDevice(), oldSwapchain, nullptr);
    }
}