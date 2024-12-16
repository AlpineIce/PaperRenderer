#pragma once

#include <functional>
#include <mutex>
#include <chrono>
#include <unordered_map>

namespace PaperRenderer
{
    //----------LOGGING----------//

    enum LogType
    {
        INFO, //potentially useful information for keeping track of resources or state
        WARNING, //essentially non-critical errors that should be dealt with
        ERROR //errors that absolutely need to be dealt with
    };
    
    struct LogEvent
    {
        LogType type = INFO;
        std::string text = {};
    };

    //thread safe log handling class
    class Logger
    {
    private:
        const std::function<void(class RenderEngine&, const LogEvent&)> eventCallbackFunction;
        std::mutex logMutex;

        class RenderEngine& renderer;

    public:
        Logger(class RenderEngine& renderer, const std::function<void(class RenderEngine&, const LogEvent&)>& eventCallbackFunction);
        ~Logger();

        void recordLog(const LogEvent& event);
    };

    //----------PROFILING AND STATE----------//

    class RendererStatistics
    {
    private:
        std::unordered_map<std::string, double> timeStatistics;
        std::unordered_map<std::string, uint64_t> objectCounters;
        std::mutex mutex;
        
    public:
        RendererStatistics();
        ~RendererStatistics();

        void insertTimeStatistic(const std::string& name, double value);
        void modifyObjectCounter(const std::string& name, int increment); //increment can be positive for incrementing or negative for decrementing
    };
}