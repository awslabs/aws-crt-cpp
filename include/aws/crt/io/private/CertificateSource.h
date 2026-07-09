/*! \cond DOXYGEN_PRIVATE
** Hide API from this file in doxygen. Set DOXYGEN_PRIVATE in doxygen
** config to enable this file for doxygen.
*/

#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            /**
             * @private
             * The source of the TLS certificate used for authentication.
             * Automatically determined from the TlsContextOptions factory method used.
             * Used internally for IoT SDK metrics tracking.
             */
            enum class CertificateSource
            {
                /// No mTLS certificate configured (default TLS context)
                None = 0,
                /// Certificate and private key loaded from PEM files or memory buffers
                CertificateFiles = 1,
                /// Certificate loaded via PKCS#11 library
                Pkcs11 = 2,
                /// Certificate loaded from a Windows certificate store
                WindowsCertStore = 3,
                /// Certificate loaded from a PKCS#12 (.p12) file
                Pkcs12File = 4,
            };

            /**
             * @private
             * Contains TLS connection metadata needed for IoT SDK metrics tracking.
             * Not part of the public API.
             */
            struct TlsConnectionInfo
            {
                CertificateSource certificateSource;
                aws_tls_versions tlsVersion;
                aws_tls_cipher_pref cipherPref;
            };

        } // namespace Io
    } // namespace Crt
} // namespace Aws
