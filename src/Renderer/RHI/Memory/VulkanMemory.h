#pragma once
#include "../Device.h"

namespace Renderer
{
    struct DeviceAllocationInfo
    {
        VkDeviceSize allocationSize = 0;
        VkMemoryPropertyFlagBits memoryProperties;
    };

    class DeviceAllocation
    {
    private:
        Device* devicePtr;
        DeviceAllocationInfo allocationInfo;
        
        static uint32_t allocationCount;
        VkDeviceMemory allocation;
        VkMemoryType memoryType;

        VkPhysicalDeviceMemoryProperties2 getDeviceMemoryProperties() const;
    public:
        DeviceAllocation(Device* device, DeviceAllocationInfo allocationInfo);
        ~DeviceAllocation();

        const VkDeviceMemory& getAllocation() const { return allocation; }
        const VkMemoryType& getMemoryType() const { return memoryType; }
    };
}