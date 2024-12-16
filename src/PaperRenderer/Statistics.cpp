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

    RendererStatistics::RendererStatistics()
    {
    }

    RendererStatistics::~RendererStatistics()
    {
    }

    void RendererStatistics::insertTimeStatistic(const std::string &name, double value)
    {
        std::lock_guard<std::mutex> guard(mutex);
        timeStatistics[name] = value;
    }

    void RendererStatistics::modifyObjectCounter(const std::string &name, int increment)
    {
        std::lock_guard<std::mutex> guard(mutex);
        objectCounters[name] += increment;
    }
}
