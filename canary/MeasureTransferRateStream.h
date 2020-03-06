#pragma once

#include "TransferState.h"
#include <aws/crt/StlAllocator.h>
#include <aws/crt/Types.h>
#include <aws/crt/io/Stream.h>
#include <chrono>
#include <functional>

class MeasureTransferRateStream : public Aws::Crt::Io::InputStream
{
  public:
    MeasureTransferRateStream(
        CanaryApp &canaryApp,
        const std::shared_ptr<TransferState> &transferState,
        Aws::Crt::Allocator *allocator);

    virtual bool IsValid() const noexcept override;

  private:
    CanaryApp &m_canaryApp;
    std::shared_ptr<TransferState> m_transferState;
    Aws::Crt::Allocator *m_allocator;
    uint64_t m_written;

    const TransferState &GetTransferState() const;
    TransferState &GetTransferState();

    virtual bool ReadImpl(Aws::Crt::ByteBuf &buffer) noexcept override;
    virtual Aws::Crt::Io::StreamStatus GetStatusImpl() const noexcept override;
    virtual int64_t GetLengthImpl() const noexcept override;
    virtual bool SeekImpl(Aws::Crt::Io::OffsetType offset, Aws::Crt::Io::StreamSeekBasis basis) noexcept override;
};