/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/common/clock.h>

#include <aws/crt/io/Bootstrap.h>

// Maximum time to wait (in seconds) for the blocking shutdown sequence.
// This is to avoid hanging at program exit due to unexpected conditions.
#define FINAL_TIMEOUT_SECS 10

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            namespace
            {
                /* Called by event-loop host-resolver, and client-bootstrap shutdown. */
                void shutdown_callback(void *user_data)
                {
                    auto shutdown_user_data = static_cast<struct shutdown_user_data *>(user_data);

                    aws_mutex_lock(&shutdown_user_data->mtx);
                    shutdown_user_data->shutdown_count += 1;
                    aws_condition_variable_notify_one(&shutdown_user_data->cv);
                    aws_mutex_unlock(&shutdown_user_data->mtx);
                }
                /**
                 * Return true if all three components (event_loop_group, host_resolver, client bootstrap)
                 * have invoked the above shutdown_callback.
                 */
                bool condition_predicate_fn(void *pred_ctx)
                {
                    const auto shutdown_user_data = static_cast<struct shutdown_user_data *>(pred_ctx);
                    return shutdown_user_data->shutdown_count == 3;
                }
            }

            ClientBootstrap::ClientBootstrap(
                EventLoopGroup &elGroup,
                HostResolver &resolver,
                Allocator *allocator) noexcept
                : m_bootstrap(nullptr), m_lastError(AWS_ERROR_SUCCESS)
            {
                aws_client_bootstrap_options options;

                if (aws_mutex_init(&m_shutdown_cb_user_data.mtx) != AWS_ERROR_SUCCESS)
                {
                    m_lastError = aws_last_error();
                    return;
                }
                if (aws_condition_variable_init(&m_shutdown_cb_user_data.cv) != AWS_ERROR_SUCCESS)
                {
                    m_lastError = aws_last_error();
                    return;
                }

                options.event_loop_group = elGroup.GetUnderlyingHandle();
                options.host_resolution_config = resolver.GetConfig();
                options.host_resolver = resolver.GetUnderlyingHandle();
                options.on_shutdown_complete = shutdown_callback;
                options.user_data = &m_shutdown_cb_user_data;
                m_bootstrap = aws_client_bootstrap_new(allocator, &options);
                if (!m_bootstrap)
                {
                    m_lastError = aws_last_error();
                }
                else
                {
                    /*
                     * Override the event_loop_group and host_resolver shutdown callbacks.
                     * This is ok since within aws-crt-cpp, none of these are set.
                     * In each case, the shutdown_callback_fn is invoked after the asynchronous
                     * shutdown of the exiting threads.
                     */
                    void *user_data = static_cast<void *>(&m_shutdown_cb_user_data);

                    m_bootstrap->event_loop_group->shutdown_options.shutdown_callback_fn = shutdown_callback;
                    m_bootstrap->event_loop_group->shutdown_options.shutdown_callback_user_data = user_data;

                    m_bootstrap->host_resolver->shutdown_options.shutdown_callback_fn = shutdown_callback;
                    m_bootstrap->host_resolver->shutdown_options.shutdown_callback_user_data = user_data;
                }
            }

            ClientBootstrap::~ClientBootstrap()
            {
                if (m_bootstrap)
                {
                    aws_client_bootstrap_release(m_bootstrap);
                    {
                        aws_mutex_lock(&m_shutdown_cb_user_data.mtx);
                        aws_condition_variable_wait_for_pred(&m_shutdown_cb_user_data.cv,
                                                             &m_shutdown_cb_user_data.mtx,
                                                             aws_timestamp_convert(FINAL_TIMEOUT_SECS,
                                                                                   AWS_TIMESTAMP_SECS,
                                                                                   AWS_TIMESTAMP_NANOS, NULL),
                                                             condition_predicate_fn,
                                                             static_cast<void *>(&m_shutdown_cb_user_data));
                        aws_mutex_unlock(&m_shutdown_cb_user_data.mtx);
                    }
                    if (m_shutdownCallback) {
                        m_shutdownCallback();
                    }
                }
            }

            ClientBootstrap::operator bool() const noexcept { return m_lastError == AWS_ERROR_SUCCESS; }

            int ClientBootstrap::LastError() const noexcept { return m_lastError; }

            void ClientBootstrap::SetShutdownCompleteCallback(OnClientBootstrapShutdownComplete callback)
            {
                m_shutdownCallback = std::move(callback);
            }

            aws_client_bootstrap *ClientBootstrap::GetUnderlyingHandle() const noexcept
            {
                if (*this)
                {
                    return m_bootstrap;
                }

                return nullptr;
            }
        } // namespace Io
    }     // namespace Crt
} // namespace Aws
