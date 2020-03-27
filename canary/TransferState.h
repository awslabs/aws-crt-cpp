/*
 * Copyright 2010-2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include "MetricsPublisher.h"
#include <aws/crt/Types.h>

class TransferState
{
  public:
    TransferState();
    TransferState(
        std::shared_ptr<MetricsPublisher> publisher,
        uint32_t partIndex,
        uint32_t partNumber,
        uint64_t sizeInBytes);

    uint32_t GetPartIndex() const { return m_partIndex; }

    uint32_t GetPartNumber() const { return m_partNumber; }

    uint64_t GetSizeInBytes() const { return m_sizeInBytes; }

    void SetTransferSuccess(bool success) { m_transferSuccess = success; }

    void AddDataUpMetric(uint64_t dataUp);

    void AddDataDownMetric(uint64_t dataDown);

    void FlushDataUpMetrics();

    void FlushDataDownMetrics();

  private:
    uint32_t m_partIndex;
    uint32_t m_partNumber;
    uint64_t m_sizeInBytes;
    uint32_t m_transferSuccess : 1;

    Aws::Crt::Vector<Metric> m_uploadMetrics;
    Aws::Crt::Vector<Metric> m_downloadMetrics;
    std::shared_ptr<MetricsPublisher> m_publisher;

    void DistributeDataUsedOverTime(
        Aws::Crt::Vector<Metric> &metrics,
        MetricName metricName,
        uint64_t beginTime,
        double dataUsed);

    void PushMetric(Aws::Crt::Vector<Metric> &metrics, MetricName metricName, double dataUsed);

    void PushAndTryToMerge(
        Aws::Crt::Vector<Metric> &metrics,
        MetricName metricName,
        uint64_t timestamp,
        double dataUsed);

    void FlushMetricsVector(Aws::Crt::Vector<Metric> &metrics);
};