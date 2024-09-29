#pragma once
#include "volk.h"

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

    enum SemaphoreType
    {
        BINARY = 0,
        TIMELINE = 1
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

    struct BinarySemaphorePair
    {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        VkPipelineStageFlagBits2 stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    };

    struct TimelineSemaphorePair
    {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        VkPipelineStageFlagBits2 stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        uint64_t value = 0;
    };

    //generic parameters for synchronization in queues
    struct SynchronizationInfo
    {
        QueueType queueType;
        std::vector<BinarySemaphorePair> binaryWaitPairs = {};
        std::vector<BinarySemaphorePair> binarySignalPairs = {};
        std::vector<TimelineSemaphorePair> timelineWaitPairs = {};
        std::vector<TimelineSemaphorePair> timelineSignalPairs = {};
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
        class RenderEngine* rendererPtr;

        static std::unordered_map<QueueType, QueuesInFamily>* queuesPtr;
        static std::unordered_map<QueueType, VkCommandPool> commandPools;

        void createCommandPools();

        static bool isInit;
        
    public:
        Commands(class RenderEngine* renderer, std::unordered_map<QueueType, QueuesInFamily>* queuesPtr);
        ~Commands();
        Commands(const Commands&) = delete;

        static void freeCommandBuffers(class RenderEngine* renderer, std::vector<CommandBuffer>& commandBuffers);
        static void submitToQueue(const SynchronizationInfo &synchronizationInfo, const std::vector<VkCommandBuffer> &commandBuffers);

        static VkSemaphore getSemaphore(class RenderEngine* renderer);
        static VkSemaphore getTimelineSemaphore(class RenderEngine* renderer, uint64_t initialValue);
        static VkFence getSignaledFence(class RenderEngine* renderer);
        static VkFence getUnsignaledFence(class RenderEngine* renderer);
        static VkCommandBuffer getCommandBuffer(class RenderEngine* renderer, QueueType type);
    };
}