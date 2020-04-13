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
    enum class CLIOption
    {
        ToolName,
        InstanceType,
        MeasureSinglePartTransfer,
        MeasureMultiPartTransfer,
        MeasureHttpTransfer,
        Logging,
        SendEncrypted,
        Fork,
        NumTransfers,
        NumConcurrentTransfers,
        DownloadOnly,
        RehydrateBackup,
        DownloadBucketName,
        DownloadObjectName,

        MAX
    };

    const aws_cli_option options[] = {{"toolName", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 't'},
                                      {"instanceType", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'i'},
                                      {"measureSinglePartTransfer", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 's'},
                                      {"measureMultiPartTransfer", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'm'},
                                      {"measureHttpTransfer", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'h'},
                                      {"logging", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'd'},
                                      {"sendEncrypted", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'e'},
                                      {"fork", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'f'},
                                      {"numTransfers", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'n'},
                                      {"numConcurrentTransfers", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'c'},
                                      {"downloadOnly", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'z'},
                                      {"rehydrateBackup", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'r'},
                                      {"downloadBucketName", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'b'},
                                      {"downloadObjectName", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'o'}};

    const char *optstring = "t:i:smh:defn:c:zr:b:o:";

    CanaryAppOptions canaryAppOptions;

    if (argc >= 1)
    {
        std::string &toolName = canaryAppOptions.toolName;
        toolName = argv[0];
        size_t dirStart = toolName.rfind('\\');

        if (dirStart != std::string::npos)
        {
            toolName = toolName.substr(dirStart + 1);
        }
    }

    int cliOptionIndex = 0;
    int cliGetOptResult = aws_cli_getopt_long(argc, argv, optstring, options, &cliOptionIndex);

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
            case CLIOption::Fork:
#ifndef WIN32
                canaryAppOptions.forkModeEnabled = true;
#else
                AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "Fork mode not supported on Windows.");
#endif
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
            case CLIOption::DownloadOnly:
                canaryAppOptions.downloadOnly = true;
                break;
            case CLIOption::RehydrateBackup:
                canaryAppOptions.rehydrateBackupObjectName = aws_cli_optarg;
                canaryAppOptions.rehydrateBackup = true;
                break;
            case CLIOption::DownloadBucketName:
                canaryAppOptions.downloadBucketName = aws_cli_optarg;
                break;
            case CLIOption::DownloadObjectName:
                canaryAppOptions.downloadObjectName = aws_cli_optarg;
                break;
            default:
                AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "Unknown CLI option used.");
                break;
        }

        cliGetOptResult = aws_cli_getopt_long(argc, argv, optstring, options, &cliOptionIndex);
    }

    ClampConcurrentTransfers(canaryAppOptions.numUpTransfers, canaryAppOptions.numUpConcurrentTransfers);
    ClampConcurrentTransfers(canaryAppOptions.numDownTransfers, canaryAppOptions.numDownConcurrentTransfers);

#ifndef WIN32
    std::vector<CanaryAppChildProcess> children;

    // For fork mode, create a child process per transfer, setting up pipes for communication along the way.
    if (canaryAppOptions.forkModeEnabled)
    {
        canaryAppOptions.isParentProcess = true;

        uint32_t maxNumTransfers = std::max(canaryAppOptions.numUpTransfers, canaryAppOptions.numDownTransfers);

        for (uint32_t i = 0; i < maxNumTransfers; ++i)
        {
            int32_t pipeParentToChild[2];
            int32_t pipeChildToParent[2];

            if (pipe(pipeParentToChild) == -1)
            {
                AWS_LOGF_FATAL(AWS_LS_CRT_CPP_CANARY, "Could not create pipe from parent process to child process.");
                exit(EXIT_FAILURE);
            }

            if (pipe(pipeChildToParent) == -1)
            {
                AWS_LOGF_FATAL(AWS_LS_CRT_CPP_CANARY, "Could not create pipe from child process to parent process.");
                exit(EXIT_FAILURE);
            }

            pid_t childPid = fork();

            if (childPid == 0)
            {
                canaryAppOptions.isParentProcess = false;
                canaryAppOptions.isChildProcess = true;
                canaryAppOptions.readFromParentPipe = pipeParentToChild[0];
                canaryAppOptions.writeToParentPipe = pipeChildToParent[1];
                canaryAppOptions.childProcessIndex = i;
                canaryAppOptions.numUpTransfers = 1;
                canaryAppOptions.numUpConcurrentTransfers = 1;
                canaryAppOptions.numDownTransfers = 1;
                canaryAppOptions.numDownConcurrentTransfers = 1;
                break;
            }
            else
            {
                if (childPid == -1)
                {
                    AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "Error creating child process.");

                    close(pipeChildToParent[0]);
                    close(pipeChildToParent[1]);
                    close(pipeParentToChild[0]);
                    close(pipeParentToChild[1]);
                }
                else
                {
                    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Created child process for transfer %d", i);

                    children.emplace_back(childPid, pipeChildToParent[0], pipeParentToChild[1]);
                }
            }
        }
    }
#endif

#ifdef WIN32
    CanaryApp canaryApp(std::move(canaryAppOptions));
#else
    CanaryApp canaryApp(std::move(canaryAppOptions), std::move(children));
#endif

    canaryApp.Run();

#ifndef WIN32
    // If executing in a parent process, wait for all child processes to complete before exiting.
    if (canaryApp.GetOptions().isParentProcess)
    {
        bool waitingForChildren = true;

        AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Waiting for child processes to complete...");

        while (waitingForChildren)
        {
            int status = 0;
            wait(&status);

            AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "One or more child processes completed.");

            waitingForChildren = errno != ECHILD;
        }
    }
#endif

    return 0;
}
