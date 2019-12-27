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

#include "CanaryApp.h"
#include "CanaryUtil.h"
#include "MeasureTransferRate.h"
#include "MetricsPublisher.h"
#include "S3ObjectTransport.h"
#include <aws/common/log_channel.h>
#include <aws/common/log_formatter.h>
#include <aws/common/logging.h>
#include <aws/io/stream.h>
#include <time.h>

using namespace Aws::Crt;

int main(int argc, char *argv[])
{
    CanaryApp canaryApp(argc, argv);

    if (canaryApp.measureSmallTransfer)
    {
        canaryApp.publisher->SetMetricTransferSize(MetricTransferSize::Small);
        canaryApp.measureTransferRate->MeasureSmallObjectTransfer();
    }

    if (canaryApp.measureLargeTransfer)
    {
        canaryApp.publisher->SetMetricTransferSize(MetricTransferSize::Large);
        canaryApp.measureTransferRate->MeasureLargeObjectTransfer();
    }

    return 0;
}
