#include "Statistics.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
    //----------LOGGING----------//
    
    Logger::Logger(RenderEngine& renderer, const std::function<void(RenderEngine&, const LogEvent&)>& eventCallbackFunction)
        :eventCallbackFunction(eventCallbackFunction),
        renderer(renderer)
    {
        //hello world!
        recordLog({
            .type = INFO,
            .text = "\n\n   ---------- Hello, PaperRenderer! ----------\n"
        });
    }

    Logger::~Logger()
    {
        //goodbye!
        recordLog({
            .type = INFO,
            .text = "\n\n   ---------- Goodbye, PaperRenderer ----------\n"
        });
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

    void StatisticsTracker::insertTimeStatistic(const std::string &name, TimeStatisticInterval interval, std::chrono::duration<double> duration)
    {
        std::lock_guard<std::mutex> guard(statisticsMutex);
        if(name.size()) statistics.timeStatistics.emplace_back(name, interval, duration);
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
    Timer::Timer(RenderEngine& renderer, const std::string& timerName, TimeStatisticInterval interval)
        :timerName(timerName),
        interval(interval),
        startTime(std::chrono::high_resolution_clock::now()),
        renderer(renderer)
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
            renderer.getStatisticsTracker().insertTimeStatistic(timerName, interval, (std::chrono::high_resolution_clock::now() - startTime));
            released = true;
        }
    }

    void Timer::release()
    {
        tryInsertTimeStatistic();
    }
}
