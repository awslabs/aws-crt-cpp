/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 *
 * Comparison test: InputStream (sync) vs AsyncInputStream (async)
 * Demonstrates CPU usage difference when data source is slow.
 */

#include <aws/crt/Api.h>
#include <aws/crt/io/Stream.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace Aws::Crt;

static std::atomic<int> g_readCallCount{0};
static const int CHUNK_COUNT = 5;
static const int CHUNK_DELAY_MS = 500;

/**
 * Synchronous stream with simulated slow data source.
 * CRT polls ReadImpl() repeatedly - causes hot loop when data isn't ready.
 */
class SlowSyncStream : public Io::InputStream
{
    mutable int m_chunksRemaining = CHUNK_COUNT;
    mutable std::chrono::steady_clock::time_point m_nextDataTime;

  public:
    SlowSyncStream() : Io::InputStream(), m_nextDataTime(std::chrono::steady_clock::now()) {}

    bool IsValid() const noexcept override { return true; }

  protected:
    bool ReadImpl(ByteBuf &buffer) noexcept override
    {
        g_readCallCount++;

        auto now = std::chrono::steady_clock::now();

        // No data ready yet - return without writing
        if (now < m_nextDataTime)
        {
            return true;
        }

        // EOF
        if (m_chunksRemaining <= 0)
        {
            return true;
        }

        // Write chunk
        const char *chunk = "chunk";
        aws_byte_buf_write(&buffer, (const uint8_t *)chunk, 5);
        m_chunksRemaining--;
        m_nextDataTime = now + std::chrono::milliseconds(CHUNK_DELAY_MS);
        return true;
    }

    bool ReadSomeImpl(ByteBuf &buffer) noexcept override
    {
        return ReadImpl(buffer);
    }

    Io::StreamStatus GetStatusImpl() const noexcept override
    {
        Io::StreamStatus status;
        status.is_valid = true;
        status.is_end_of_stream = (m_chunksRemaining <= 0);
        return status;
    }

    int64_t GetLengthImpl() const noexcept override
    {
        return -1; // Unknown length
    }

    bool SeekImpl(int64_t, Io::StreamSeekBasis) noexcept override { return false; }
    int64_t PeekImpl() const noexcept override { return 0; }
};

/**
 * Asynchronous stream with simulated slow data source.
 * ReadImpl() called once per chunk - callback fires when data ready.
 */
class SlowAsyncStream : public Io::AsyncInputStream
{
    int m_chunksRemaining = CHUNK_COUNT;

  public:
    SlowAsyncStream() : Io::AsyncInputStream() {}

    bool IsValid() const noexcept override { return true; }

    // Public wrapper for testing
    void Read(ByteBuf &buffer, std::function<void(bool)> onComplete)
    {
        ReadImpl(buffer, std::move(onComplete));
    }

  protected:
    void ReadImpl(ByteBuf &buffer, std::function<void(bool)> onComplete) noexcept override
    {
        g_readCallCount++;

        if (m_chunksRemaining <= 0)
        {
            onComplete(true); // EOF
            return;
        }

        // Simulate async wait for data
        std::thread(
            [this, &buffer, onComplete]()
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(CHUNK_DELAY_MS));

                const char *chunk = "chunk";
                aws_byte_buf_write(&buffer, (const uint8_t *)chunk, 5);
                m_chunksRemaining--;
                onComplete(true);
            })
            .detach();
    }
};

void testSyncStream()
{
    g_readCallCount = 0;
    auto stream = std::make_shared<SlowSyncStream>();

    uint8_t buf[64];
    ByteBuf buffer = aws_byte_buf_from_empty_array(buf, sizeof(buf));

    auto start = std::chrono::steady_clock::now();

    // Simulate CRT polling loop
    while (true)
    {
        Io::StreamStatus status;
        stream->GetStatus(status);
        if (status.is_end_of_stream)
            break;

        buffer.len = 0;
        stream->Read(buffer);
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    std::cout << "=== InputStream (sync) ===" << std::endl;
    std::cout << "ReadImpl calls: " << g_readCallCount << std::endl;
    std::cout << "Time: " << ms << "ms" << std::endl;
    std::cout << std::endl;
}

void testAsyncStream()
{
    g_readCallCount = 0;
    auto stream = std::make_shared<SlowAsyncStream>();

    uint8_t buf[64];
    ByteBuf buffer = aws_byte_buf_from_empty_array(buf, sizeof(buf));

    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    int chunksRead = 0;

    auto start = std::chrono::steady_clock::now();

    std::function<void()> readNext = [&]()
    {
        buffer.len = 0;
        stream->Read(buffer, [&](bool success)
        {
            if (!success || buffer.len == 0)
            {
                std::lock_guard<std::mutex> lock(mtx);
                done = true;
                cv.notify_one();
                return;
            }
            chunksRead++;
            if (chunksRead >= CHUNK_COUNT)
            {
                std::lock_guard<std::mutex> lock(mtx);
                done = true;
                cv.notify_one();
            }
            else
            {
                readNext();
            }
        });
    };

    readNext();

    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&] { return done; });

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    std::cout << "=== AsyncInputStream (async) ===" << std::endl;
    std::cout << "ReadImpl calls: " << g_readCallCount << std::endl;
    std::cout << "Time: " << ms << "ms" << std::endl;
    std::cout << std::endl;
}

int main()
{
    ApiHandle apiHandle;

    std::cout << "Stream Comparison Test" << std::endl;
    std::cout << "Chunks: " << CHUNK_COUNT << ", Delay: " << CHUNK_DELAY_MS << "ms each" << std::endl;
    std::cout << "Expected time: ~" << (CHUNK_COUNT * CHUNK_DELAY_MS) << "ms" << std::endl;
    std::cout << std::endl;

    testSyncStream();
    testAsyncStream();

    std::cout << "Sync stream polls continuously (high CPU)." << std::endl;
    std::cout << "Async stream waits for callback (idle CPU)." << std::endl;

    return 0;
}
