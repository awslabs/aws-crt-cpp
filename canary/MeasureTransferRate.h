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

#include <aws/crt/DateTime.h>
#include <aws/crt/StlAllocator.h>
#include <aws/crt/Types.h>
#include <aws/crt/io/Stream.h>
#include <chrono>
#include <functional>

#include "MeasureTransferRateStream.h"

class S3ObjectTransport;
class MetricsPublisher;
class CanaryApp;
struct aws_event_loop;

/**
 * Performs measurements of transfer rates in several scenarios.
 */
class MeasureTransferRate
{
  public:
    MeasureTransferRate(CanaryApp &canaryApp);
    ~MeasureTransferRate();

    /*
     * Measure download speed from an HTTP server.
     */
    void MeasureHttpTransfer();

    /*
     * Measure upload/download speed of single part objects from S3.
     */
    void MeasureSinglePartObjectTransfer();

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
        const std::shared_ptr<S3ObjectTransport> &transport,
        NotifyTransferFinished &&notifyTransferFinished)>;

    CanaryApp &m_canaryApp;
    aws_event_loop *m_schedulingLoop;

    /*
     * Performs a caller defined transfer.
     *
     *    filenamePrefix: Filename to be used during the transfer operation.  Will have an id appended to it unless
     * NoFileSuffix is a specified flag.
     *    numTransfers: Number of total times to perform the transferFunction.
     *    numConcurentTransfers: Maximum number of transfers that are active at any given time.
     *    flags: Optional flags defined by MeasurementFlags enum that can alter the exeuction of the method.
     *    transport: S3ObjectTransport that will be used during the transfer.  This is passed in so that the relevant
     * DNS cache can be warmed in relation to the number of concurrent transfers happening. transferFunction: Defines
     * the actual transfer happening.
     */
    void PerformMeasurement(
        const char *filenamePrefix,
        uint32_t numTransfers,
        uint32_t numConcurrentTransfers,
        uint32_t flags,
        const std::shared_ptr<S3ObjectTransport> &transport,
        TransferFunction &&transferFunction);
};
