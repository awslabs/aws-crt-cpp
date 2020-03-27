#pragma once

#include <aws/crt/DateTime.h>
#include <aws/crt/StlAllocator.h>
#include <aws/crt/Types.h>
#include <aws/crt/io/Stream.h>
#include <chrono>
#include <functional>

#include "MeasureTransferRateStream.h"
#include "MultipartTransferState.h"

class S3ObjectTransport;
class MetricsPublisher;
class CanaryApp;
struct aws_event_loop;

class MeasureTransferRate
{
  public:
    static const uint64_t SmallObjectSize;
    static const uint64_t LargeObjectSize;
    static const std::chrono::milliseconds AllocationMetricFrequency;
    static const uint64_t AllocationMetricFrequencyNS;
    static const uint32_t LargeObjectNumParts;

    MeasureTransferRate(CanaryApp &canaryApp);
    ~MeasureTransferRate();

    void MeasureHttpTransfer();
    void MeasureSmallObjectTransfer();
    void MeasureLargeObjectTransfer();

  private:
    enum MeasurementFlags
    {
        NoFileSuffix = 0x00000001,
        DontWarmDNSCache = 0x00000002
    };

    using NotifyTransferFinished = std::function<void(int32_t errorCode)>;
    using TransferFunction = std::function<void(
        uint32_t transferIndex,
        Aws::Crt::String &&key,
        uint64_t objectSize,
        const std::shared_ptr<S3ObjectTransport> &transport,
        NotifyTransferFinished &&notifyTransferFinished)>;

    CanaryApp &m_canaryApp;
    aws_event_loop *m_schedulingLoop;
    aws_task m_pulseMetricsTask;

    void PerformMeasurement(
        const char *filenamePrefix,
        const char *keyPrefix,
        uint32_t numTransfers,
        uint32_t numConcurrentTransfers,
        uint64_t objectSize,
        uint32_t flags,
        const std::shared_ptr<S3ObjectTransport> &transport,
        TransferFunction &&transferFunction);

    void SchedulePulseMetrics();

    static void s_PulseMetricsTask(aws_task *task, void *arg, aws_task_status status);
};
