#pragma once
#include "VulkanMemory.h"

#include <unordered_map>
#include <vector>
#include <mutex>

namespace PaperRenderer
{
    namespace PaperMemory
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

            static std::unordered_map<QueueType, QueuesInFamily>* queuesPtr;
            static std::unordered_map<QueueType, VkCommandPool> commandPools;
            static const uint32_t frameCount = 2;

            void createCommandPools();

            static bool isInit;
            
        public:
            Commands(VkDevice device, std::unordered_map<QueueType, QueuesInFamily>* queuesPtr);
            ~Commands();

            static void freeCommandBuffers(VkDevice device, std::vector<CommandBuffer>& commandBuffers);
            static void submitToQueue(VkDevice device, const SynchronizationInfo &synchronizationInfo, const std::vector<VkCommandBuffer> &commandBuffers);
            static uint32_t getFrameCount() { return frameCount; }

            static VkSemaphore getSemaphore(VkDevice device);
            static VkFence getSignaledFence(VkDevice device);
            static VkFence getUnsignaledFence(VkDevice device);
            static VkCommandBuffer getCommandBuffer(VkDevice device, QueueType type);
        };
    }
}