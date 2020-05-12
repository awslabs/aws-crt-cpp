#pragma once

#include <cinttypes>
#include <mutex>
#include <aws/crt/Types.h>

struct aws_task;
struct aws_event_loop;

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            class EventLoop;
            class DefaultHostResolver;

            struct EndPointMonitorOptions
            {
                EndPointMonitorOptions();

                uint64_t m_expectedPerSampleThroughput;
                uint64_t m_allowedFailureInterval;
                aws_event_loop* m_schedulingLoop;
                DefaultHostResolver* m_hostResolver;
                Aws::Crt::String m_endPoint;
            };
            
            class EndPointMonitor
            {
            public:
                struct SampleSum
                {
                    uint64_t m_sampleSum : 48;
                    uint64_t m_numSamples : 16;

                    SampleSum();
                    SampleSum(uint64_t sample);
                    SampleSum(uint64_t sampleSum, uint64_t numSamples);
                    uint64_t asUint64() const;
                };

                EndPointMonitor(const Aws::Crt::String & address, const EndPointMonitorOptions & options);
                ~EndPointMonitor();

                void AddSample(uint64_t bytesPerSecond);

            private:
                Aws::Crt::String m_address;
                EndPointMonitorOptions m_options;
                aws_task* m_processSamplesTask;
                std::atomic<uint64_t> m_sampleSum;
                uint64_t m_timeLastProcessed;
                uint64_t m_failureTime;

                static void s_ProcessSamplesTask(struct aws_task *task, void *arg, aws_task_status taskStatus);

                void ProcessSamples();

                void ScheduleNextProcessSamplesTask();
            };

            class EndPointMonitorManager
            {
            public:
                
                EndPointMonitorManager(const EndPointMonitorOptions & options)
                    : m_options(options)
                {
                    
                }

                EndPointMonitor* CreateMonitor(const Aws::Crt::String & address);
            
            private:
                EndPointMonitorOptions m_options;
                std::mutex m_endPointMonitorsMutex;
                Aws::Crt::Map<Aws::Crt::String, std::unique_ptr<EndPointMonitor>> m_endPointMonitors;
            };
        }
    }
}