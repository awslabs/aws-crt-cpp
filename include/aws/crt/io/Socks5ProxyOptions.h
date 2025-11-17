#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Types.h>
#include <aws/crt/io/TlsOptions.h>
#include <aws/crt/io/Uri.h>
#include <aws/io/socks5.h>

#include <memory>

struct aws_socks5_proxy_options;

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            /**
             * SOCKS5 authentication methods as defined in RFC 1928
             */
            enum class AwsSocks5AuthMethod
            {
                /**
                 * No authentication required
                 */
                None = 0x00,

                /**
                 * Username/password authentication (RFC 1929)
                 */
                UsernamePassword = 0x02,

                /**
                 * No acceptable methods (server response)
                 */
                NoAcceptableMethods = 0xFF
            };

            enum class AwsSocks5HostResolutionMode
            {
                Proxy = AWS_SOCKS5_HOST_RESOLUTION_PROXY,
                Client = AWS_SOCKS5_HOST_RESOLUTION_CLIENT,
            };

            struct AWS_CRT_CPP_API Socks5ProxyAuthConfig
            {
                AwsSocks5AuthMethod Method{AwsSocks5AuthMethod::None};
                Optional<String> Username;
                Optional<String> Password;

                static Socks5ProxyAuthConfig CreateNone() noexcept
                {
                    return Socks5ProxyAuthConfig{};
                }

                static Socks5ProxyAuthConfig CreateUsernamePassword(const String &username, const String &password)
                {
                    Socks5ProxyAuthConfig config;
                    config.Method = AwsSocks5AuthMethod::UsernamePassword;
                    config.Username = username;
                    config.Password = password;
                    return config;
                }
            };

            /**
             * Configuration structure that holds all SOCKS5 proxy-related connection options
             */
            class AWS_CRT_CPP_API Socks5ProxyOptions
            {
              public:
                /* Default SOCKS5 proxy port */
                static constexpr uint16_t DefaultProxyPort = 1080;

                Socks5ProxyOptions() noexcept;

                //TODO: remove deprecated constructor
                Socks5ProxyOptions(
                    const String &hostName,
                    uint32_t port,
                    AwsSocks5AuthMethod authMethod,
                    const String &username,
                    const String &password,
                    uint32_t connectionTimeoutMs,
                    struct aws_allocator *allocator,
                    AwsSocks5HostResolutionMode resolutionMode = AwsSocks5HostResolutionMode::Proxy);

                Socks5ProxyOptions(
                    const String &hostName,
                    uint32_t port = DefaultProxyPort,
                    const Socks5ProxyAuthConfig &authConfig = Socks5ProxyAuthConfig::CreateNone(),
                    uint32_t connectionTimeoutMs = 0,
                    AwsSocks5HostResolutionMode resolutionMode = AwsSocks5HostResolutionMode::Proxy,
                    struct aws_allocator *allocator = aws_default_allocator());


                Socks5ProxyOptions(const Socks5ProxyOptions &rhs);

                Socks5ProxyOptions(Socks5ProxyOptions &&rhs) noexcept;

                Socks5ProxyOptions &operator=(const Socks5ProxyOptions &rhs);
                Socks5ProxyOptions &operator=(Socks5ProxyOptions &&rhs) noexcept;

                ~Socks5ProxyOptions();

                /**
                 * @return true when the proxy options contain a configured endpoint, false otherwise.
                 */
                explicit operator bool() const noexcept;

                /**
                 * @return the last aws error code encountered while operating on this instance.
                 */
                int LastError() const noexcept;

                /**
                 * Returns the underlying C struct for SOCKS5 proxy options.
                 */
                aws_socks5_proxy_options *GetUnderlyingHandle() { return &m_options; }

                /**
                 * Returns the underlying C struct for SOCKS5 proxy options (const).
                 */
                const aws_socks5_proxy_options *GetUnderlyingHandle() const { return &m_options; }

                /**
                 * Updates the SOCKS5 proxy endpoint configuration.
                 *
                 * @param hostName Proxy host name or address.
                 * @param port Proxy port. Must be <= UINT16_MAX.
                 *
                 * @return true when the endpoint was accepted, false otherwise with LastError() set.
                 */
                bool SetProxyEndpoint(const String &hostName, uint32_t port);

                /**
                 * Applies a SOCKS5 authentication configuration.
                 *
                 * @param authConfig Authentication configuration to apply.
                 *
                 * @return true on success, false on failure with LastError() set.
                 */
                bool SetAuth(const Socks5ProxyAuthConfig &authConfig);

                /**
                 * Sets username/password authentication for the SOCKS5 proxy.
                 *
                 * @param username User name for proxy authentication. Must be non-empty.
                 * @param password Password for proxy authentication. Must be non-empty.
                 *
                 * @return true on success, false on failure with LastError() set.
                 */
                bool SetAuthCredentials(const String &username, const String &password);

                /**
                 * Clears any previously configured authentication credentials.
                 */
                void ClearAuthCredentials();

                /**
                 * Sets the host resolution mode (proxy/client) for the SOCKS5 proxy.
                 */
                void SetHostResolutionMode(AwsSocks5HostResolutionMode mode);

                /**
                 * Returns the host resolution mode (proxy/client) for the SOCKS5 proxy.
                 */
                AwsSocks5HostResolutionMode GetHostResolutionMode() const;

                /**
                 * Sets the connection timeout in milliseconds for the SOCKS5 proxy.
                 */
                void SetConnectionTimeoutMs(uint32_t timeoutMs);

                /**
                 * Returns the SOCKS5 proxy host as a string, or empty Optional if not set.
                 */
                Optional<String> GetHost() const;

                /**
                 * Returns the SOCKS5 proxy port number.
                 */
                uint16_t GetPort() const;

                /**
                 * Returns the connection timeout in milliseconds for the SOCKS5 proxy.
                 */
                uint32_t GetConnectionTimeoutMs() const;

                /**
                 * Returns the authentication method used for the SOCKS5 proxy.
                 */
                AwsSocks5AuthMethod GetAuthMethod() const;

                /**
                 * Returns the SOCKS5 proxy username as a string, or empty Optional if not set.
                 */
                Optional<String> GetUsername() const;

                /**
                 * Returns the SOCKS5 proxy password as a string, or empty Optional if not set.
                 */
                Optional<String> GetPassword() const;

                /**
                 * Returns the host resolution mode (proxy/client) for the SOCKS5 proxy.
                 */
                AwsSocks5HostResolutionMode GetResolutionMode() const;

                /**
                 * Creates SOCKS5 proxy options from a parsed URI. The URI scheme must be socks5 or socks5h.
                 * Username and password are pulled from the authority userinfo when present (requires both
                 * username and password to enable authentication). socks5h implies proxy-side name resolution,
                 * socks5 implies client-side name resolution.
                 *
                 * Uri format: socks5[h]://[username:password@]host[:port]
                 *
                 * @param uri Parsed URI describing the proxy endpoint.
                 * @param connectionTimeoutMs Optional connection timeout in milliseconds applied to the proxy
                 * connection.
                 * @param allocator Allocator used for underlying allocations (defaults to the process allocator).
                 *
                 * @return Populated proxy options on success, otherwise an empty Optional with aws_last_error() set.
                 */
                static Optional<Socks5ProxyOptions> CreateFromUri(
                    const Uri &uri,
                    uint32_t connectionTimeoutMs = 0,
                    struct aws_allocator *allocator = nullptr);

              private:
                // Helper function to apply authentication configuration to aws_socks5_proxy_options struct
                bool ApplyAuthConfig(aws_socks5_proxy_options &options, const Socks5ProxyAuthConfig &authConfig);

                aws_socks5_proxy_options m_options{};
                struct aws_allocator *m_allocator{nullptr};
                int m_lastError{0};
                Socks5ProxyAuthConfig m_authConfig{};

            };

        } // namespace Io
    }     // namespace Crt
} // namespace Aws
