#include "Statistics.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
    //----------LOGGING----------//
    
    Logger::Logger(RenderEngine& renderer, const std::function<void(RenderEngine&, const LogEvent&)>& eventCallbackFunction)
        :eventCallbackFunction(eventCallbackFunction),
        renderer(renderer)
    {
    }

    Logger::~Logger()
    {
    }

    void Logger::recordLog(const LogEvent &event)
    {
        if(eventCallbackFunction)
        {
            std::lock_guard<std::mutex> guard(logMutex);
            eventCallbackFunction(renderer, event);
        }
    }

    //----------PROFILING AND STATE----------//
    
    //statistics tracker definitions
    StatisticsTracker::StatisticsTracker()
    {
    }

    StatisticsTracker::~StatisticsTracker()
    {
    }

    void StatisticsTracker::insertTimeStatistic(const std::string &name, std::chrono::duration<double> duration)
    {
        std::lock_guard<std::mutex> guard(statisticsMutex);
        if(name.size()) statistics.timeStatistics[name] = duration;
    }

    void StatisticsTracker::modifyObjectCounter(const std::string &name, int increment)
    {
        std::lock_guard<std::mutex> guard(statisticsMutex);
        if(name.size()) statistics.objectCounters[name] += increment;
    }

    void StatisticsTracker::clearStatistics()
    {
        statistics = {};
    }

    // timer definitions
    Timer::Timer(const std::string& timerName, StatisticsTracker &tracker)
        :timerName(timerName),
        startTime(std::chrono::high_resolution_clock::now()),
        tracker(tracker)
    {
    }

    Timer::~Timer()
    {
        tryInsertTimeStatistic();
    }

    void Timer::tryInsertTimeStatistic()
    {
        if(!released)
        {
            tracker.insertTimeStatistic(timerName, (std::chrono::high_resolution_clock::now() - startTime));
            released = true;
        }
    }

    void Timer::release()
    {
        tryInsertTimeStatistic();
    }
}
