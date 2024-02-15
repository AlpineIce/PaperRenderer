#include "Swapchain.h"

namespace Renderer
{
    Swapchain::Swapchain(Device *device, Window* window, bool enableVsync)
        :devicePtr(device),
        vsync(enableVsync),
        windowPtr(window),
        swapchain(VK_NULL_HANDLE)
    {
        //----------COLOR SPACE SELECTION----------//

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device->getGPU(), *(device->getSurfacePtr()), &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device->getGPU(), *(device->getSurfacePtr()), &formatCount, surfaceFormats.data());

        bool hdrFound = false;
        this->swapchainImageFormat = VK_FORMAT_UNDEFINED;
        for(VkSurfaceFormatKHR surfaceFormat : surfaceFormats)
        {
            /*if(surfaceFormat.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) //HDR color space
            {
                hdrFound = true;
                this->swapchainImageFormat = surfaceFormat.format;
                this->imageColorSpace = surfaceFormat.colorSpace;

                break;
            }*/
        }

        if(!hdrFound)
        {
            for(VkSurfaceFormatKHR surfaceFormat : surfaceFormats)
            {
                if(surfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR && !hdrFound) //SRGB color space
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

        //----------PRESENT MODE----------//

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device->getGPU(), *(device->getSurfacePtr()), &presentModeCount, nullptr);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device->getGPU(), *(device->getSurfacePtr()), &presentModeCount, presentModes.data());

        for(VkPresentModeKHR presentMode : presentModes)
        {
            if(presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR && !vsync)
            {
                this->presentationMode = presentMode;
                break;
            }
            if(presentMode == VK_PRESENT_MODE_FIFO_KHR && vsync)
            {
                this->presentationMode = presentMode;
                break;
            }
        }

        buildSwapchain();
    }

    Swapchain::~Swapchain()
    {
        //depth buffer
        for(uint32_t i = 0; i < depthBufferImages.size(); i++)
        {
            vmaDestroyImage(devicePtr->getAllocator(), depthBufferImages.at(i), depthBufferAllocations.at(i));
            vkDestroyImageView(devicePtr->getDevice(), depthBufferViews.at(i), nullptr);
        }
        for(VkImageView image : imageViews)
        {
            vkDestroyImageView(devicePtr->getDevice(), image, nullptr);
        }

        vkDestroySwapchainKHR(devicePtr->getDevice(), swapchain, nullptr);
    }

    void Swapchain::buildSwapchain()
    {
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
        swapchainInfo.imageUsage = VkImageUsageFlagBits::VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t queueFamilies[] = {(uint32_t)devicePtr->getQueueFamilies().graphicsFamilyIndex,
                                    (uint32_t)devicePtr->getQueueFamilies().presentationFamilyIndex};
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
        createDepthBuffer();
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

    void Swapchain::createDepthBuffer()
    {
        //find depth buffer format
        VkFormatProperties properties;

        vkGetPhysicalDeviceFormatProperties(devicePtr->getGPU(), VK_FORMAT_D24_UNORM_S8_UINT, &properties);

        if(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            depthBufferFormat = VK_FORMAT_D24_UNORM_S8_UINT;
            depthBufferLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        }
        else
        {
            throw std::runtime_error("No good depth buffer format");
        }
        VkExtent3D imageExtent = {};
        imageExtent.width = swapchainExtent.width;
        imageExtent.height = swapchainExtent.height;
        imageExtent.depth = 1;

        uint32_t imageCount;
        vkGetSwapchainImagesKHR(devicePtr->getDevice(), swapchain, &imageCount, nullptr);
        depthBufferImages.resize(imageCount);
        depthBufferViews.resize(imageCount);
        depthBufferAllocations.resize(imageCount);
        
        for(int i = 0; i < imageCount; i++)
        {
            VkImageCreateInfo depthImageInfo = {};
            depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            depthImageInfo.pNext = NULL;
            depthImageInfo.flags = 0;
            depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
            depthImageInfo.format = depthBufferFormat;
            depthImageInfo.extent = imageExtent;
            depthImageInfo.mipLevels = 1;
            depthImageInfo.arrayLayers = 1;
            depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            depthImageInfo.queueFamilyIndexCount = 0;
            depthImageInfo.pQueueFamilyIndices = NULL;
            depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            VmaAllocationCreateInfo allocCreateInfo = {};
            allocCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

            VmaAllocationInfo allocInfo;
            VkResult allocResult = vmaCreateImage(devicePtr->getAllocator(), &depthImageInfo, &allocCreateInfo, &depthBufferImages.at(i), &depthBufferAllocations.at(i), &allocInfo);

            //create the image view
            VkImageSubresourceRange subresourceRange;
            subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            subresourceRange.baseMipLevel = 0;
            subresourceRange.levelCount = 1;
            subresourceRange.baseArrayLayer = 0;
            subresourceRange.layerCount = 1;

            VkImageViewCreateInfo viewInfo = {};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.pNext = NULL;
            viewInfo.flags = 0;
            viewInfo.image = depthBufferImages.at(i);
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = depthBufferFormat;
            viewInfo.subresourceRange = subresourceRange;

            if (vkCreateImageView(devicePtr->getDevice(), &viewInfo, nullptr, &depthBufferViews.at(i)) != VK_SUCCESS) 
            {
                throw std::runtime_error("Failed to create depth buffer image view");
            }
        }
    }

    void Swapchain::recreate()
    {
        int width = 0, height = 0;
        glfwGetFramebufferSize(windowPtr->getWindow(), &width, &height);
        while (width == 0 || height == 0) {
            glfwWaitEvents();
            glfwGetFramebufferSize(windowPtr->getWindow(), &width, &height);
        }
        
        vkQueueWaitIdle(devicePtr->getQueues().graphics.at(0));
        
        //destruction
        for(VkImageView image : imageViews)
        {
            vkDestroyImageView(devicePtr->getDevice(), image, nullptr);
        }

        for(uint32_t i = 0; i < depthBufferImages.size(); i++)
        {
            vmaDestroyImage(devicePtr->getAllocator(), depthBufferImages.at(i), depthBufferAllocations.at(i));
            vkDestroyImageView(devicePtr->getDevice(), depthBufferViews.at(i), nullptr);
        }

        //rebuild
        VkSwapchainKHR oldSwapchain = swapchain;
        buildSwapchain();
        vkDestroySwapchainKHR(devicePtr->getDevice(), oldSwapchain, nullptr);
    }
}