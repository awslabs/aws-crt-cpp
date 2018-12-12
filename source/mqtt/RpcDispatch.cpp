/*
 * Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
#include <aws/crt/mqtt/RpcDispatch.h>

namespace Aws
{
    namespace Crt
    {
        namespace Mqtt
        {
            RpcNonceContainer::~RpcNonceContainer()
            {
            }

            void RpcNonceDispatcher::RegisterRpcRequest
                (const RpcNonceContainer& request, const RpcDispatchHandler& handler)
            {
                std::lock_guard<std::mutex> locker(m_tableLock);
                m_dispatchTable[request.GetNonce()] = handler;
            }

            void RpcNonceDispatcher::RegisterRpcRequest
                    (const RpcNonceContainer& request, RpcDispatchHandler&& handler)
            {
                std::lock_guard<std::mutex> locker(m_tableLock);
                m_dispatchTable[request.GetNonce()] = std::move(handler);
            }

            bool RpcNonceDispatcher::DispatchRpcResponse(const RpcNonceContainer& response)
            {
                RpcDispatchHandler handler;

                {
                    std::lock_guard<std::mutex> locker(m_tableLock);

                    auto iter = m_dispatchTable.find(response.GetNonce());
                    if (iter == m_dispatchTable.end())
                    {
                        return false;
                    }

                    handler = iter->second;
                    m_dispatchTable.erase(iter);
                }

                if (handler)
                {
                    handler(response);
                    return true;
                }


                return false;
            }
        }
    }
}