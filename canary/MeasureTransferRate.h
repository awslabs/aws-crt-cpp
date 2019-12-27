#pragma once

#include <aws/crt/StlAllocator.h>
#include <aws/crt/Types.h>
#include <aws/io/stream.h>
#include <chrono>
#include <functional>

class S3ObjectTransport;
class MetricsPublisher;
struct aws_event_loop;
struct CanaryApp;

class MeasureTransferRate
{
  public:
    MeasureTransferRate(CanaryApp &canaryApp);

    void MeasureSmallObjectTransfer();
    void MeasureLargeObjectTransfer();

  private:
    struct MeasureAllocationsArgs
    {
        MeasureTransferRate &measureTransferRate;
    };

    struct TemplateStream
    {
        aws_input_stream inputStream;
        MetricsPublisher *publisher;
        size_t length;
        size_t written;
    };

    using NotifyUploadFinished = std::function<void(int32_t errorCode)>;
    using NotifyDownloadProgress = std::function<void(uint64_t dataLength)>;
    using NotifyDownloadFinished = std::function<void(int32_t errorCode)>;
    using PerformTransfer = std::function<void(
        Aws::Crt::Allocator *allocator,
        S3ObjectTransport &transport,
        MetricsPublisher &publisher,
        const Aws::Crt::String &key,
        uint64_t objectSize,
        const NotifyUploadFinished &notifyUploadFinished,
        const NotifyDownloadFinished &notifyDownloadFinished)>;

    static const char BodyTemplate[];
    static const size_t SmallObjectSize;
    static const size_t LargeObjectSize;
    static const std::chrono::milliseconds AllocationMetricFrequency;
    static const uint64_t AllocationMetricFrequencyNS;

    static aws_input_stream_vtable s_templateStreamVTable;

    CanaryApp &m_canaryApp;
    aws_event_loop *m_schedulingLoop;
    aws_task m_measureAllocationsTask;

    static int s_templateStreamRead(struct aws_input_stream *stream, struct aws_byte_buf *dest);
    static int s_templateStreamGetStatus(struct aws_input_stream *stream, struct aws_stream_status *status);
    static int s_templateStreamSeek(
        struct aws_input_stream *stream,
        aws_off_t offset,
        enum aws_stream_seek_basis basis);
    static int s_templateStreamGetLength(struct aws_input_stream *stream, int64_t *length);
    static void s_templateStreamDestroy(struct aws_input_stream *stream);
    static aws_input_stream *s_createTemplateStream(
        Aws::Crt::Allocator *allocator,
        MetricsPublisher *publisher,
        size_t length);

    template <typename TPeformTransferType>
    void PerformMeasurement(
        uint32_t maxConcurrentTransfers,
        uint64_t objectSize,
        double cutOffTime,
        const TPeformTransferType &&performTransfer);

    static void s_TransferSmallObject(
        MeasureTransferRate &measureTransferRate,
        const Aws::Crt::String &key,
        uint64_t objectSize,
        const NotifyUploadFinished &notifyUploadFinished,
        const NotifyDownloadProgress &notifyDownloadProgress,
        const NotifyDownloadFinished &notifyDownloadFinished);

    static void s_TransferLargeObject(
        MeasureTransferRate &measureTransferRate,
        const Aws::Crt::String &key,
        uint64_t objectSize,
        const NotifyUploadFinished &notifyUploadFinished,
        const NotifyDownloadProgress &notifyDownloadProgress,
        const NotifyDownloadFinished &notifyDownloadFinished);

    void ScheduleMeasureAllocationsTask();

    static void s_MeasureAllocations(aws_task *task, void *arg, aws_task_status status);
};
