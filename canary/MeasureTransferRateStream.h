/*
 * Copyright 2010-2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#pragma once

#include "TransferState.h"
#include <aws/crt/StlAllocator.h>
#include <aws/crt/Types.h>
#include <aws/crt/io/Stream.h>
#include <chrono>
#include <functional>

/*
 * An input stream that measures up-transfer-rate by recording metrics when it is read.
 */
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
    uint64_t m_written;

    const TransferState &GetTransferState() const;
    TransferState &GetTransferState();

    virtual bool ReadImpl(Aws::Crt::ByteBuf &buffer) noexcept override;
    virtual Aws::Crt::Io::StreamStatus GetStatusImpl() const noexcept override;
    virtual int64_t GetLengthImpl() const noexcept override;
    virtual bool SeekImpl(Aws::Crt::Io::OffsetType offset, Aws::Crt::Io::StreamSeekBasis basis) noexcept override;
};
