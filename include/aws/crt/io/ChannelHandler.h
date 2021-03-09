#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Exports.h>
#include <aws/crt/Types.h>
#include <aws/io/channel.h>

#include <cstddef>

struct aws_array_list;
struct aws_io_message;

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            enum class ChannelDirection
            {
                Read,
                Write,
            };

            enum class MessageType
            {
                ApplicationData,
            };

            /**
             * Wrapper for aws-c-io channel handlers. The semantics are identical as the functions on
             * aws_channel_handler.
             */
            class AWS_CRT_CPP_API ChannelHandler : public std::enable_shared_from_this<ChannelHandler>
            {
              public:
                virtual ~ChannelHandler() = default;

                ChannelHandler(const ChannelHandler &) = delete;
                ChannelHandler(ChannelHandler &&) = delete;

                const ChannelHandler &operator=(const ChannelHandler &) = delete;
                const ChannelHandler &operator=(ChannelHandler &&) = delete;

                /**
                 * Called by the channel when a message is available for processing in the read direction. It is your
                 * responsibility to call aws_mem_release(message->allocator, message); on message when you are finished
                 * with it.
                 *
                 * Also keep in mind that your slot's internal window has been decremented. You'll want to call
                 * aws_channel_slot_increment_read_window() at some point in the future if you want to keep receiving
                 * data.
                 */
                virtual int ProcessReadMessage(struct aws_io_message &message) = 0;

                /**
                 * Called by the channel when a message is available for processing in the write direction. It is your
                 * responsibility to call aws_mem_release(message->allocator, message); on message when you are finished
                 * with it.
                 */
                virtual int ProcessWriteMessage(struct aws_io_message &message) = 0;

                /**
                 * Called by the channel when a downstream handler has issued a window increment. You'll want to update
                 * your internal state and likely propagate a window increment message of your own by calling
                 * 'aws_channel_slot_increment_read_window()'
                 */
                virtual int IncrementReadWindow(size_t size) = 0;

                /**
                 * The channel calls shutdown on all handlers twice, once to shut down reading, and once to shut down
                 * writing. Shutdown always begins with the left-most handler, and proceeds to the right with dir set to
                 * ChannelDirection::Read. Then shutdown is called on handlers from right to left with dir set to
                 * ChannelDirection::Write.
                 *
                 * The shutdown process does not need to complete immediately and may rely on scheduled tasks.
                 * The handler must call aws_channel_slot_on_handler_shutdown_complete() when it is finished,
                 * which propagates shutdown to the next handler.  If 'free_scarce_resources_immediately' is true,
                 * then resources vulnerable to denial-of-service attacks (such as sockets and file handles)
                 * must be closed immediately before the shutdown() call returns.
                 */
                virtual int Shutdown(ChannelDirection dir, int errorCode, bool freeScarceResourcesImmediately) = 0;

                /**
                 * Called by the channel when the handler is added to a slot, to get the initial window size.
                 */
                virtual size_t InitialWindowSize() = 0;

                /**
                 * Called by the channel anytime a handler is added or removed, provides a hint for downstream
                 * handlers to avoid message fragmentation due to message overhead.
                 */
                virtual size_t MessageOverhead() = 0;

                /**
                 * Directs the channel handler to reset all of the internal statistics it tracks about itself.
                 */
                virtual void ResetStatistics(){};

                /**
                 * Adds a pointer to the handler's internal statistics (if they exist) to a list of statistics
                 * structures associated with the channel's handler chain.
                 */
                virtual void GatherStatistics(struct aws_array_list *) {}

                struct aws_channel_handler *GetUnderlyingHandle() { return &m_handler; }

              protected:
                ChannelHandler(Allocator *allocator = g_allocator);

                struct aws_io_message *AcquireMessageFromPool(MessageType messageType, size_t sizeHint);
                struct aws_io_message *AcquireMaxSizeMessageForWrite();

                bool ChannelsThreadIsCallersThread() const;
                bool SendMessage(struct aws_io_message *message, ChannelDirection direction);
                bool IncrementUpstreamReadWindow(size_t windowUpdateSize);
                bool OnShutdownComplete(ChannelDirection direction, int errorCode, bool freeScarceResourcesImmediately);
                size_t DownstreamReadWindow() const;
                size_t UpstreamMessageOverhead() const;
                struct aws_channel_slot *GetSlot() const;

                struct aws_channel_handler m_handler;

              private:
                std::shared_ptr<ChannelHandler> m_selfReference;
                static struct aws_channel_handler_vtable s_vtable;

                static void s_Destroy(struct aws_channel_handler *handler);
            };
        } // namespace Io
    }     // namespace Crt
} // namespace Aws