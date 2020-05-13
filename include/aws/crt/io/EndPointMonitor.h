#pragma once

#include <atomic>
#include <aws/crt/Types.h>
#include <cinttypes>
#include <mutex>

struct aws_task;
struct aws_event_loop;
struct aws_http_connection;
struct aws_host_address;
struct aws_host_resolver;

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
                aws_event_loop *m_schedulingLoop;
                aws_host_resolver *m_hostResolver;
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

                struct HistoryEntry
                {
                    uint64_t m_timeStamp;
                    uint64_t m_bytesPerSecond;
                    uint32_t m_putInFailTable : 1;

                    HistoryEntry(uint64_t timeStamp, uint64_t bytesPerSecond, bool putInFailTable)
                        : m_timeStamp(timeStamp), m_bytesPerSecond(bytesPerSecond), m_putInFailTable(putInFailTable)
                    {
                    }

                    HistoryEntry() : HistoryEntry(0ULL, 0ULL, false) {}
                };

                struct History
                {
                    Vector<HistoryEntry> m_entries;
                };

                EndPointMonitor(const Aws::Crt::String &address, const EndPointMonitorOptions &options);
                ~EndPointMonitor();

                void AddSample(uint64_t bytesPerSecond);

                void SetIsInFailTable(bool status);

                bool IsInFailTable() const;

                const Aws::Crt::String &GetAddress() const { return m_address; }

                // TODO should maybe have some additional thread safety added to it
                const History &GetHistory() const { return m_history; }

              private:
                Aws::Crt::String m_address;
                History m_history;
                EndPointMonitorOptions m_options;
                aws_task *m_processSamplesTask;
                std::atomic<bool> m_isInFailTable;
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
                EndPointMonitorManager(const EndPointMonitorOptions &options);
                ~EndPointMonitorManager();

                void SetupCallbacks();

                void AttachMonitor(aws_http_connection *connection);

                std::shared_ptr<StringStream> GenerateEndPointCSV();

              private:
                EndPointMonitorOptions m_options;
                std::mutex m_endPointMonitorsMutex;
                Aws::Crt::Map<Aws::Crt::String, std::unique_ptr<EndPointMonitor>> m_endPointMonitors;

                static void OnPutFailTable(aws_host_address *host_address, void *user_data);

                static void OnRemoveFailTable(aws_host_address *host_address, void *user_data);
            };
        } // namespace Io
    }     // namespace Crt
} // namespace Aws