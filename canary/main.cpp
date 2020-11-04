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

#ifdef WIN32
#    undef min
#else
#    include <sys/resource.h>
#    include <sys/types.h>
#    include <sys/wait.h>
#    include <unistd.h>
#endif

#include <aws/common/command_line_parser.h>

using namespace Aws::Crt;

void ParseTransferPair(const char *str, uint32_t &outUpValue, uint32_t &outDownValue)
{
    std::string numTransfersStr = str;
    size_t index = numTransfersStr.find(":");

    if (index == std::string::npos)
    {
        int32_t numTransfers = atoi(numTransfersStr.c_str());
        outUpValue = numTransfers;
        outDownValue = numTransfers;
    }
    else
    {
        numTransfersStr[index] = '\0';
        outUpValue = atoi(numTransfersStr.c_str());
        outDownValue = atoi(numTransfersStr.c_str() + index + 1);
    }
}

void ClampConcurrentTransfers(uint32_t numTransfers, uint32_t &inOutNumConcurrentTransfers)
{
    if (inOutNumConcurrentTransfers == 0)
    {
        inOutNumConcurrentTransfers = numTransfers;
    }

    inOutNumConcurrentTransfers = std::min(inOutNumConcurrentTransfers, numTransfers);
}

int main(int argc, char *argv[])
{
    Aws::Crt::ApiHandle apiHandle(g_allocator);

    enum class CLIOption
    {
        ToolName,
        InstanceType,
        MeasureSinglePartTransfer,
        MeasureMultiPartTransfer,
        MeasureHttpTransfer,
        Logging,
        SendEncrypted,
        NumTransfers,
        NumConcurrentTransfers,
        RehydrateBackup,
        BucketName,
        DownloadObjectName,
        Region,
        Config,
        MaxNumThreads,
        MetricsPublishingEnabled,

        MAX
    };

    const aws_cli_option options[] = {{"toolName", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 't'},
                                      {"instanceType", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'i'},
                                      {"measureSinglePartTransfer", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 's'},
                                      {"measureMultiPartTransfer", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'm'},
                                      {"measureHttpTransfer", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'h'},
                                      {"logging", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'l'},
                                      {"sendEncrypted", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'e'},
                                      {"numTransfers", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'n'},
                                      {"numConcurrentTransfers", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'c'},
                                      {"rehydrateBackup", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'u'},
                                      {"bucketName", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'b'},
                                      {"downloadObjectName", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'o'},
                                      {"region", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'r'},
                                      {"config", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'g'},
                                      {"maxNumThreads", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'z'},
                                      {"metricsPublishingEnabled", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'p'}};

    const char *optstring = "t:i:smh:len:c:u:b:o:r:g:z:p:";

    int cliOptionIndex = 0;
    int cliGetOptResult = aws_cli_getopt_long(argc, argv, optstring, options, &cliOptionIndex);
    String canaryAppOptionsConfig = "";

    while (cliGetOptResult != -1)
    {
        if (cliGetOptResult == '?')
        {
            continue;
        }

        switch ((CLIOption)cliOptionIndex)
        {
            case CLIOption::Config:
                canaryAppOptionsConfig = aws_cli_optarg;
                break;
            default:
                break;
        }

        cliGetOptResult = aws_cli_getopt_long(argc, argv, optstring, options, &cliOptionIndex);
    }

    String argv0;

    if (argc >= 1)
    {
        argv0 = argv[0];
    }

    CanaryAppOptions canaryAppOptions(canaryAppOptionsConfig, argv0);

    aws_cli_optind = 1;
    cliGetOptResult = aws_cli_getopt_long(argc, argv, optstring, options, &cliOptionIndex);

    while (cliGetOptResult != -1)
    {
        if (cliGetOptResult == '?')
        {
            continue;
        }

        switch ((CLIOption)cliOptionIndex)
        {
            case CLIOption::ToolName:
                canaryAppOptions.toolName = aws_cli_optarg;
                break;
            case CLIOption::InstanceType:
                canaryAppOptions.instanceType = aws_cli_optarg;
                break;
            case CLIOption::MeasureSinglePartTransfer:
                canaryAppOptions.measureSinglePartTransfer = true;
                break;
            case CLIOption::MeasureMultiPartTransfer:
                canaryAppOptions.measureMultiPartTransfer = true;
                break;
            case CLIOption::MeasureHttpTransfer:
                canaryAppOptions.measureHttpTransfer = true;
                canaryAppOptions.httpTestEndpoint = aws_cli_optarg;
                break;
            case CLIOption::Logging:
                canaryAppOptions.loggingEnabled = true;
                break;
            case CLIOption::SendEncrypted:
                canaryAppOptions.sendEncrypted = true;
                break;
            case CLIOption::NumTransfers:
                ParseTransferPair(aws_cli_optarg, canaryAppOptions.numUpTransfers, canaryAppOptions.numDownTransfers);
                break;
            case CLIOption::NumConcurrentTransfers:
                ParseTransferPair(
                    aws_cli_optarg,
                    canaryAppOptions.numUpConcurrentTransfers,
                    canaryAppOptions.numDownConcurrentTransfers);
                break;
            case CLIOption::RehydrateBackup:
                canaryAppOptions.rehydrateBackupObjectName = aws_cli_optarg;
                canaryAppOptions.rehydrateBackup = true;
                break;
            case CLIOption::BucketName:
                canaryAppOptions.bucketName = aws_cli_optarg;
                break;
            case CLIOption::DownloadObjectName:
                canaryAppOptions.downloadObjectName = aws_cli_optarg;
                break;
            case CLIOption::Region:
                canaryAppOptions.region = aws_cli_optarg;
                break;
            case CLIOption::Config:
                /* We detect the the config value earlier. */
                break;
            case CLIOption::MaxNumThreads:
                canaryAppOptions.maxNumThreads = atoi(aws_cli_optarg);
                break;
            case CLIOption::MetricsPublishingEnabled:
                canaryAppOptions.metricsPublishingEnabled = atoi(aws_cli_optarg) != 0;
                break;
            default:
                AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "Unknown CLI option used.");
                break;
        }

        cliGetOptResult = aws_cli_getopt_long(argc, argv, optstring, options, &cliOptionIndex);
    }

    ClampConcurrentTransfers(canaryAppOptions.numUpTransfers, canaryAppOptions.numUpConcurrentTransfers);
    ClampConcurrentTransfers(canaryAppOptions.numDownTransfers, canaryAppOptions.numDownConcurrentTransfers);

    CanaryApp canaryApp(apiHandle, std::move(canaryAppOptions));
    canaryApp.Run();

    return 0;
}
