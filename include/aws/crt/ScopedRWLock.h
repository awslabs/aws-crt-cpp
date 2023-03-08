#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/common/rw_lock.h>

namespace Aws
{
    namespace Crt
    {
        /**
         * Custom implementation of an ScopedReadLock type. Wrapping the aws_rw_lock.
         * On creation, the ScopedTryReadLock will attempts to acquire the lock and returns immediately if it can not.
         * The lock will be unlocked on destruction.
         * Use aws_last_error() or operator bool() to check if the lock get acquired successfully.
         */

        class ScopedTryReadLock
        {
          public:
            ScopedTryReadLock() : m_lock(nullptr), m_last_error(AWS_ERROR_INVALID_ARGUMENT) {}

            //
            ScopedTryReadLock(aws_rw_lock *lock)
            {
                m_lock = lock;
                m_last_error = aws_rw_lock_try_rlock(m_lock);
            }

            int aws_last_error() { return m_last_error; }
            operator bool() const { return m_last_error == AWS_ERROR_SUCCESS; }

            ~ScopedTryReadLock()
            {
                if (m_last_error == AWS_ERROR_SUCCESS)
                {
                    aws_rw_lock_runlock(m_lock);
                }
            }
            ScopedTryReadLock(const ScopedTryReadLock &) noexcept = delete;
            ScopedTryReadLock(ScopedTryReadLock &&) noexcept = delete;
            ScopedTryReadLock &operator=(const ScopedTryReadLock &) noexcept = delete;
            ScopedTryReadLock &operator=(ScopedTryReadLock &&) noexcept = delete;

          private:
            int m_last_error;
            aws_rw_lock *m_lock;
        };
    } // namespace Crt
} // namespace Aws
