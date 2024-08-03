#pragma once
#include "VulkanMemory.h"

#include <unordered_map>
#include <vector>
#include <mutex>

namespace PaperRenderer
{
    enum QueueType
    {
        GRAPHICS = 0,
        COMPUTE = 1,
        TRANSFER = 2,
        PRESENT = 3
    };
    
    struct Queue
    {
        VkQueue queue;
        std::mutex threadLock;
    };

    struct QueuesInFamily
    {
        uint32_t queueFamilyIndex;
        std::vector<Queue*> queues;
    };

    struct QueueFamiliesIndices
    {
        int graphicsFamilyIndex = -1;
        int computeFamilyIndex = -1;
        int transferFamilyIndex = -1;
        int presentationFamilyIndex = -1;
    };

    struct SemaphorePair
    {
        VkSemaphore semaphore;
        VkPipelineStageFlagBits2 stage;
    };

    //generic parameters for synchronization in queues
    struct SynchronizationInfo
    {
        QueueType queueType;
        std::vector<SemaphorePair> waitPairs = {};
        std::vector<SemaphorePair> signalPairs = {};
        VkFence fence = VK_NULL_HANDLE;
    };

    struct CommandBuffer
    {
        VkCommandBuffer buffer;
        QueueType type;
    };

    class Commands
    {
    private:
        VkDevice device;
        VkPhysicalDevice gpu;
        VkSurfaceKHR surface;

        static std::unordered_map<QueueType, QueuesInFamily>* queuesPtr;
        static std::unordered_map<QueueType, VkCommandPool> commandPools;
        static uint32_t frameCount;

        void createCommandPools();

        static bool isInit;
        
    public:
        Commands(VkDevice device, VkPhysicalDevice gpu, VkSurfaceKHR surface, std::unordered_map<QueueType, QueuesInFamily>* queuesPtr);
        ~Commands();

        static void freeCommandBuffers(VkDevice device, std::vector<CommandBuffer>& commandBuffers);
        static void submitToQueue(VkDevice device, const SynchronizationInfo &synchronizationInfo, const std::vector<VkCommandBuffer> &commandBuffers);
        static uint32_t getFrameCount();

        static VkSemaphore getSemaphore(VkDevice device);
        static VkFence getSignaledFence(VkDevice device);
        static VkFence getUnsignaledFence(VkDevice device);
        static VkCommandBuffer getCommandBuffer(VkDevice device, QueueType type);
    };
}