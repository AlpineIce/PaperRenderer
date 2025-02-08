#include "Swapchain.h"
#include "PaperRenderer.h"

#include <algorithm>
#include <functional>

namespace PaperRenderer
{
    Swapchain::Swapchain(RenderEngine& renderer, const std::function<void(RenderEngine&, VkExtent2D newExtent)>& swapchainRebuildCallbackFunction, const WindowState& startingWindowState)
        :windowState(startingWindowState),
        swapchainRebuildCallback(swapchainRebuildCallbackFunction),
        renderer(renderer)
    {
        //----------WINDOW CREATION----------//

        if(glfwVulkanSupported() != GLFW_TRUE) throw std::runtime_error("No vulkan support for GLFW");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        if(windowState.monitor == NULL)
        {
            windowState.monitor = glfwGetPrimaryMonitor();
        }
        const GLFWvidmode* mode = glfwGetVideoMode(windowState.monitor);

        switch(windowState.windowMode)
        {
            case WINDOWED:
                window = glfwCreateWindow(windowState.resX, windowState.resY, windowState.windowName.c_str(), NULL, NULL);

                break;
            case BORDERLESS:
                glfwWindowHint(GLFW_RED_BITS, mode->redBits);
                glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
                glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
                glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
                window = glfwCreateWindow(mode->width, mode->height, windowState.windowName.c_str(), windowState.monitor, NULL);

                windowState.resX = mode->width;
                windowState.resY = mode->height;

                break;
            case FULLSCREEN:
                window = glfwCreateWindow(windowState.resX, windowState.resY, windowState.windowName.c_str(), windowState.monitor, NULL);

                break;
        }

        //surface and device creation
        VkResult result = glfwCreateWindowSurface(renderer.getDevice().getInstance(), window, nullptr, (VkSurfaceKHR*)(&renderer.getDevice().getSurface()));
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("VkResult: " + std::to_string(result) + " Window surface creation failed");
        }
        renderer.getDevice().createDevice();

        //set swapchain present modes and format
        setWindowState(startingWindowState);

        //build swapchian
        buildSwapchain();

        //set glfw callback 
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

        //sync
        imageSemaphores.reserve(imageCount);
        for(uint32_t i = 0; i < imageCount; i++)
        {
            imageSemaphores.push_back(renderer.getDevice().getCommands().getSemaphore());
        }

        //log constructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "Swapchain constructor finished"
        });
    }

    Swapchain::~Swapchain()
    {
        //images
        for(VkImageView image : imageViews)
        {
            vkDestroyImageView(renderer.getDevice().getDevice(), image, nullptr);
        }

        //semaphores
        vkDestroySwapchainKHR(renderer.getDevice().getDevice(), swapchain, nullptr);
        for(VkSemaphore semaphore : imageSemaphores)
        {
            vkDestroySemaphore(renderer.getDevice().getDevice(), semaphore, nullptr);
        }

        //glfw window and surface
        vkDestroySurfaceKHR(renderer.getDevice().getInstance(), renderer.getDevice().getSurface(), nullptr);
        glfwDestroyWindow(window);

        //log destructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "Swapchain destructor finished"
        });
    }

    const VkSemaphore& Swapchain::acquireNextImage()
    {
        //increment semaphore index
        if(semaphoreIndex >= imageSemaphores.size() - 1)
        {
            semaphoreIndex = 0;
        }
        else
        {
            semaphoreIndex++;
        }
        
        //get available image
        VkResult imageAcquireResult = vkAcquireNextImageKHR(
            renderer.getDevice().getDevice(),
            swapchain,
            UINT64_MAX,
            imageSemaphores.at(semaphoreIndex),
            VK_NULL_HANDLE,
            &frameIndex);

        if(imageAcquireResult == VK_ERROR_OUT_OF_DATE_KHR || imageAcquireResult == VK_SUBOPTIMAL_KHR)
        {
            recreate();
            acquireNextImage();
        }

        return imageSemaphores.at(semaphoreIndex);
    }

    void Swapchain::presentImage(const std::vector<VkSemaphore>& waitSemaphores)
    {
        const VkPresentInfoKHR presentSubmitInfo = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = NULL,
            .waitSemaphoreCount = (uint32_t)waitSemaphores.size(),
            .pWaitSemaphores = waitSemaphores.data(),
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &frameIndex,
            .pResults = NULL
        };

        //lock queue and present
        std::lock_guard guard(renderer.getDevice().getQueues().at(PRESENT).queues.at(0)->threadLock);
        VkResult presentResult = vkQueuePresentKHR(renderer.getDevice().getQueues().at(QueueType::PRESENT).queues.at(0)->queue, &presentSubmitInfo);

        if(presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) 
        {
            recreate();
        }
    }

    void Swapchain::setWindowState(const WindowState& newState)
    {
        //set window state to new state
        windowState = newState;

        //----------PRESENT MODE----------//

        //get valid present modes
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(renderer.getDevice().getGPU(), renderer.getDevice().getSurface(), &presentModeCount, nullptr);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(renderer.getDevice().getGPU(), renderer.getDevice().getSurface(), &presentModeCount, presentModes.data());

        //check if any match selected present mode
        bool presentModeFound = false;
        for(const VkPresentModeKHR presentMode : presentModes)
        {
            if(presentMode == windowState.presentMode)
            {
                presentModeFound = true;
                break;
            }
        }

        //verify present mode selected
        if(!presentModeFound)
        {
            if(!presentModes.size())
            {
                throw std::runtime_error("No valid GPU surface present modes");
            }
            else
            {
                //use first
                renderer.getLogger().recordLog({
                    .type = WARNING,
                    .text=  "Selected VkPresentModeKHR for swapchain was not found. Using first found mode"
                });
                windowState.presentMode = presentModes[0];
            }
        }

        //----------COLOR SPACE SELECTION----------//

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(renderer.getDevice().getGPU(), renderer.getDevice().getSurface(), &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(renderer.getDevice().getGPU(), renderer.getDevice().getSurface(), &formatCount, surfaceFormats.data());
        
        //helper lambda function
        const auto formatEqual = [](const VkSurfaceFormatKHR& a, const VkSurfaceFormatKHR& b)
        {
            bool equal = true;
            equal = equal && a.format == b.format;
            equal = equal && a.colorSpace == b.colorSpace;

            return equal;
        };

        //see if already selected format exists
        bool formatFound = false;
        for(const VkSurfaceFormatKHR surfaceFormat : surfaceFormats)
        {
            if(formatEqual(surfaceFormat, windowState.surfaceFormat))
            {
                formatFound = true;
                break;
            }
        }

        //handle specified format not being available
        if(!formatFound)
        {
            //log warning
            renderer.getLogger().recordLog({
                .type = WARNING,
                .text = "Selected surface format was not found. Auto selecting format instead"
            });
            
            //use sRGB if no HDR format is available; use UNORM if sRGB isnt avaliable
            for(const VkSurfaceFormatKHR surfaceFormat : surfaceFormats)
            {
                if(surfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) //SRGB color space
                {
                    if(surfaceFormat.format == VK_FORMAT_R8G8B8A8_SRGB)
                    {
                        windowState.surfaceFormat = surfaceFormat;
                        formatFound = true;
                        break;
                    }
                    else if(surfaceFormat.format == VK_FORMAT_R8G8B8A8_UNORM)
                    {
                        windowState.surfaceFormat = surfaceFormat;
                        formatFound = true;
                    }
                }
            }

            //throw error if format not found
            if(!formatFound) throw std::runtime_error("No good surface format found");
        }
    }

    void Swapchain::framebufferResizeCallback(GLFWwindow *window, int width, int height)
    {
        Swapchain* thisPtr = (Swapchain*)glfwGetWindowUserPointer(window);
        thisPtr->windowState.resX = width;
        thisPtr->windowState.resY = height;
        thisPtr->recreate();
    }

    void Swapchain::buildSwapchain()
    {
        //Timer
        Timer timer(renderer, "Build Swapchain", IRREGULAR);

        //set new window state
        setWindowState(windowState);

        //get surface capabilities
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(renderer.getDevice().getGPU(), renderer.getDevice().getSurface(), &capabilities);
        swapchainExtent = {
            .width = std::min(windowState.resX, capabilities.maxImageExtent.width),
            .height = std::min(windowState.resY, capabilities.maxImageExtent.height)
        };

        //set image count variables
        minImageCount = capabilities.minImageCount;
        imageCount = std::min(capabilities.minImageCount + 1, capabilities.maxImageCount); //use recommended 1 extra image

        //get queue family indices
        const QueueFamiliesIndices& deviceQueueFamilies = renderer.getDevice().getQueueFamiliesIndices();
        std::vector<uint32_t> queueFamilyIndices = {};
        if(deviceQueueFamilies.graphicsFamilyIndex != -1) queueFamilyIndices.push_back(deviceQueueFamilies.graphicsFamilyIndex);
        if(deviceQueueFamilies.computeFamilyIndex != -1) queueFamilyIndices.push_back(deviceQueueFamilies.computeFamilyIndex);
        if(deviceQueueFamilies.transferFamilyIndex != -1) queueFamilyIndices.push_back(deviceQueueFamilies.transferFamilyIndex);
        if(deviceQueueFamilies.presentationFamilyIndex != -1) queueFamilyIndices.push_back(deviceQueueFamilies.presentationFamilyIndex);

        std::sort(queueFamilyIndices.begin(), queueFamilyIndices.end());
        auto uniqueIndices = std::unique(queueFamilyIndices.begin(), queueFamilyIndices.end());
        queueFamilyIndices.erase(uniqueIndices, queueFamilyIndices.end());

        //create swapchain
        const VkSwapchainCreateInfoKHR swapchainInfo = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext = NULL,
            .flags =  0,
            .surface = renderer.getDevice().getSurface(),
            .minImageCount = imageCount,
            .imageFormat = windowState.surfaceFormat.format,
            .imageColorSpace = windowState.surfaceFormat.colorSpace,
            .imageExtent = swapchainExtent,
            .imageArrayLayers = 1,
            .imageUsage = windowState.imageUsageFlags,
            .imageSharingMode = VK_SHARING_MODE_CONCURRENT,
            .queueFamilyIndexCount = (uint32_t)queueFamilyIndices.size(),
            .pQueueFamilyIndices = queueFamilyIndices.data(),
            .preTransform = capabilities.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = windowState.presentMode,
            .clipped = VK_TRUE,
            .oldSwapchain = swapchain
        };
        
        VkResult result = vkCreateSwapchainKHR(renderer.getDevice().getDevice(), &swapchainInfo, nullptr, &swapchain);
        if(result != VK_SUCCESS) throw std::runtime_error("VkResult: " + std::to_string(result) + "Swapchain creation/recreation failed");
        
        createImageViews();

        //log build
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "Swapchain built using VkFormat " + std::to_string(windowState.surfaceFormat.format)
        });
    }

    void Swapchain::createImageViews()
    {
        vkGetSwapchainImagesKHR(renderer.getDevice().getDevice(), swapchain, &imageCount, nullptr);
        swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(renderer.getDevice().getDevice(), swapchain, &imageCount, swapchainImages.data());

        imageViews.resize(imageCount);

        for(uint32_t i = 0; i < imageCount; i++)
        {
            const VkImageViewCreateInfo creationInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = NULL,
                .flags = 0,
                .image = swapchainImages[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = windowState.surfaceFormat.format,
                .components = { VK_COMPONENT_SWIZZLE_IDENTITY },
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };

            VkResult result = vkCreateImageView(renderer.getDevice().getDevice(), &creationInfo, nullptr, &imageViews.at(i));
            if(result != VK_SUCCESS) throw std::runtime_error("VkResult: " + std::to_string(result) + "Failed to create image views");
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

        vkDeviceWaitIdle(renderer.getDevice().getDevice());
        
        //destruction
        for(VkImageView image : imageViews)
        {
            vkDestroyImageView(renderer.getDevice().getDevice(), image, nullptr);
        }
        imageViews.clear();

        //rebuild
        VkSwapchainKHR oldSwapchain = swapchain;
        buildSwapchain();
        vkDestroySwapchainKHR(renderer.getDevice().getDevice(), oldSwapchain, nullptr);

        if(swapchainRebuildCallback) swapchainRebuildCallback(renderer, swapchainExtent);
    }
}