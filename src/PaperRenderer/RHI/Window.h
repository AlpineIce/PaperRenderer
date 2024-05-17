#pragma once
#include "Device.h"

#include <string>

namespace PaperRenderer
{
    struct WindowInformation
    {
        unsigned int resX = 1280;
        unsigned int resY = 720;
        bool fullscreen = false;
    };

    class Window
    {
    private:
        GLFWwindow* window;
        GLFWmonitor* monitor;

        Device* devicePtr;

    public:
        Window(WindowInformation windowInfo, std::string name, Device* device);
        ~Window();

        GLFWwindow* getWindow() const { return window; }
    };
}