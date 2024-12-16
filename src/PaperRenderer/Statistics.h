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

    struct Statistics
    {
        std::unordered_map<std::string, std::chrono::duration<double>> timeStatistics = {};
        std::unordered_map<std::string, uint64_t> objectCounters = {};
    };

    class StatisticsTracker
    {
    private:
        Statistics statistics = {};
        std::mutex statisticsMutex;
        
    public:
        StatisticsTracker();
        ~StatisticsTracker();

        void insertTimeStatistic(const std::string& name, std::chrono::duration<double> duration); //insert time statistic (e.g. time for render pass or AS build)
        void modifyObjectCounter(const std::string& name, int increment); //increment can be positive for incrementing or negative for decrementing
        void clearStatistics(); //clears all statistical values (times, object counters, etc)

        const Statistics& getStatistics() const { return statistics; }
    };

    //RAII style timer that inserts time statistic automatically. Can be released early. Ownership limited to one thread
    class Timer
    {
    private:
        const std::string timerName;
        const std::chrono::time_point<std::chrono::high_resolution_clock> startTime;
        bool released = false;

        void tryInsertTimeStatistic();

        StatisticsTracker& tracker;
        
    public:
        Timer(const std::string& timerName, StatisticsTracker& tracker);
        ~Timer();

        void release(); //Release is typically done when this goes out of scope, but early release can be done to send time statistic now
    };
}