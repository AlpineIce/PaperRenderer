#include "Window.h"

namespace PaperRenderer
{
    Window::Window(WindowInformation windowInfo, std::string name, Device* device)
        :devicePtr(device)
    {
        if(glfwVulkanSupported() != GLFW_TRUE) throw std::runtime_error("No vulkan support for GLFW");
        //glfwGetPhysicalDevicePresentationSupport(device->getInstance(), device->getGPU(), device->getQueueFamilies().presentationFamilyIndex);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        if(windowInfo.fullscreen)
        {
            monitor = glfwGetPrimaryMonitor();
        }
        else
        {
            monitor = NULL;
        }
        window = glfwCreateWindow(windowInfo.resX, windowInfo.resY, name.c_str(), monitor, NULL);

        //surface and device creation
        VkResult result = glfwCreateWindowSurface(device->getInstance(), window, nullptr, devicePtr->getSurfacePtr());
        if(result != VK_SUCCESS)
        {
            throw std::runtime_error("Window surface creation failed");
        }
        devicePtr->createDevice();

    }

    Window::~Window()
    {
        vkDestroySurfaceKHR(devicePtr->getInstance(), *devicePtr->getSurfacePtr(), nullptr);
        glfwDestroyWindow(window);
    }
}