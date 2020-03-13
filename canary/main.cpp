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

#ifndef WIN32
#    include <sys/resource.h>
#    include <sys/types.h>
#    include <sys/wait.h>
#    include <unistd.h>
#endif

extern "C"
{
#include <aws/common/command_line_parser.h>
}

using namespace Aws::Crt;

int main(int argc, char *argv[])
{
    enum class CLIOption
    {
        ToolName,
        InstanceType,
        MeasureLargeTransfer,
        MeasureSmallTransfer,
        MeasureHttpTransfer,
        Logging,
        UsingNumaControl,
        SendEncrypted,
        MTU,
        Fork,
        NumTransfers,

        MAX
    };

    const aws_cli_option options[] = {{"toolName", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 't'},
                                      {"instanceType", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'i'},
                                      {"measureLargeTransfer", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'l'},
                                      {"measureSmallTransfer", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 's'},
                                      {"measureHttpTransfer", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'h'},
                                      {"logging", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'd'},
                                      {"usingNumaControl", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'c'},
                                      {"sendEncrypted", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'e'},
                                      {"mtu", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'm'},
                                      {"fork", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'f'},
                                      {"numTransfers", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'n'}};

    const char *optstring = "t:i:lsh:dcem:fn:";

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
    bool forkProcesses = false;

    while (aws_cli_getopt_long(argc, argv, optstring, options, &cliOptionIndex) != -1)
    {
        switch ((CLIOption)cliOptionIndex)
        {
            case CLIOption::ToolName:
                canaryAppOptions.toolName = aws_cli_optarg;
                break;
            case CLIOption::InstanceType:
                canaryAppOptions.instanceType = aws_cli_optarg;
                break;
            case CLIOption::MeasureLargeTransfer:
                canaryAppOptions.measureLargeTransfer = true;
                break;
            case CLIOption::MeasureSmallTransfer:
                canaryAppOptions.measureSmallTransfer = true;
                break;
            case CLIOption::MeasureHttpTransfer:
                canaryAppOptions.measureHttpTransfer = true;
                canaryAppOptions.httpTestEndpoint = aws_cli_optarg;
                break;
            case CLIOption::Logging:
                canaryAppOptions.loggingEnabled = true;
                break;
            case CLIOption::UsingNumaControl:
                canaryAppOptions.usingNumaControl = true;
                break;
            case CLIOption::SendEncrypted:
                canaryAppOptions.sendEncrypted = true;
                break;
            case CLIOption::MTU:
                canaryAppOptions.mtu = atoi(aws_cli_optarg);
                break;
            case CLIOption::Fork:
                forkProcesses = true;
                break;
            case CLIOption::NumTransfers:
                canaryAppOptions.numTransfers = (uint32_t)atoi(aws_cli_optarg);
                break;
            default:
                AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "Unknown CLI option used.");
                break;
        }
    }

    std::vector<CanaryAppChildProcess> children;

    if (forkProcesses)
    {
        canaryAppOptions.isParentProcess = true;

        for (uint32_t i = 0; i < canaryAppOptions.numTransfers; ++i)
        {
            int32_t pipeParentToChild[2];
            int32_t pipeChildToParent[2];

            pipe(pipeParentToChild);
            pipe(pipeChildToParent);

            pid_t childPid = fork();

            if (childPid == 0)
            {
                canaryAppOptions.isParentProcess = false;
                canaryAppOptions.isChildProcess = true;
                canaryAppOptions.readFromParentPipe = pipeParentToChild[0];
                canaryAppOptions.writeToParentPipe = pipeChildToParent[1];
                canaryAppOptions.childProcessIndex = i;
                canaryAppOptions.numTransfers = 1;
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

    CanaryApp canaryApp(std::move(canaryAppOptions), std::move(children));
    canaryApp.Run();

    if (forkProcesses && canaryApp.GetOptions().isParentProcess)
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

    return 0;
}
