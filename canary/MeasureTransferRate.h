#pragma once

#include "MultipartTransferState.h"
#include <aws/crt/DateTime.h>
#include <aws/crt/StlAllocator.h>
#include <aws/crt/Types.h>
#include <aws/crt/io/Stream.h>
#include <chrono>
#include <functional>

class S3ObjectTransport;
class MetricsPublisher;
struct aws_event_loop;
struct CanaryApp;

class MeasureTransferRateStream : public Aws::Crt::Io::InputStream
{
  public:
    MeasureTransferRateStream(
        CanaryApp &canaryApp,
        const std::shared_ptr<MultipartTransferState::PartInfo> &partInfo,
        Aws::Crt::Allocator *allocator);

    virtual bool IsValid() const noexcept override;

  private:
    CanaryApp &m_canaryApp;
    std::shared_ptr<MultipartTransferState::PartInfo> m_partInfo;
    Aws::Crt::Allocator *m_allocator;
    uint64_t m_written;
    Aws::Crt::DateTime m_timestamp;

    const MultipartTransferState::PartInfo &GetPartInfo() const;
    MultipartTransferState::PartInfo &GetPartInfo();

    virtual bool ReadImpl(Aws::Crt::ByteBuf &buffer) noexcept override;
    virtual Aws::Crt::Io::StreamStatus GetStatusImpl() const noexcept override;
    virtual int64_t GetLengthImpl() const noexcept override;
    virtual bool SeekImpl(Aws::Crt::Io::OffsetType offset, Aws::Crt::Io::StreamSeekBasis basis) noexcept override;
};

class MeasureTransferRate
{
  public:
    static size_t BodyTemplateSize;
    static const uint64_t SmallObjectSize;
    static const uint64_t LargeObjectSize;
    static const std::chrono::milliseconds AllocationMetricFrequency;
    static const uint64_t AllocationMetricFrequencyNS;
    static const uint32_t LargeObjectNumParts;

    MeasureTransferRate(CanaryApp &canaryApp);
    ~MeasureTransferRate();

    void MeasureSmallObjectTransfer();
    void MeasureLargeObjectTransfer();

  private:
    struct MeasureAllocationsArgs
    {
        MeasureTransferRate &measureTransferRate;
    };

    using NotifyTransferFinished = std::function<void(int32_t errorCode)>;
    using TransferFunction = std::function<
        void(Aws::Crt::String &&key, uint64_t objectSize, NotifyTransferFinished &&notifyTransferFinished)>;

    friend class MeasureTransferRateStream; // TODO use of friend here shouldn't be necessary

    CanaryApp &m_canaryApp;
    aws_event_loop *m_schedulingLoop;
    aws_task m_pulseMetricsTask;

    uint32_t PerformMeasurement(
        const char *filenamePrefix,
        uint32_t maxConcurrentTransfers,
        uint32_t maxTotalTransfers,
        uint64_t objectSize,
        double cutOffTime,
        TransferFunction &&transferFunction);

    void SchedulePulseMetrics();

    static void s_PulseMetricsTask(aws_task *task, void *arg, aws_task_status status);
};
