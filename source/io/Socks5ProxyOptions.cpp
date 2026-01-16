#include <aws/common/byte_buf.h>
#include <aws/common/common.h>
#include <aws/common/string.h>
#include <aws/crt/io/Socks5ProxyOptions.h>
#include <aws/io/socks5.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <utility>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {

            Socks5ProxyOptions::Socks5ProxyOptions() noexcept
                : m_allocator(aws_default_allocator()), m_lastError(AWS_ERROR_SUCCESS)
            {
                if (aws_socks5_proxy_options_init_default(&m_options))
                {
                    m_lastError = aws_last_error();
                }
            }

            //TODO: remove deprecated constructor
            Socks5ProxyOptions::Socks5ProxyOptions(
                const String &hostName,
                uint32_t port,
                AwsSocks5AuthMethod authMethod,
                const String &username,
                const String &password,
                uint32_t connectionTimeoutMs,
                struct aws_allocator *allocator,
                AwsSocks5HostResolutionMode resolutionMode)
                : Socks5ProxyOptions()
            {
                m_allocator = allocator ? allocator : aws_default_allocator();

                if (!SetProxyEndpoint(hostName, port))
                {
                    return;
                }

                SetConnectionTimeoutMs(connectionTimeoutMs);
                SetHostResolutionMode(resolutionMode);

                if (authMethod == AwsSocks5AuthMethod::UsernamePassword)
                {
                    if (!SetAuthCredentials(username, password))
                    {
                        return;
                    }
                }

                m_lastError = AWS_ERROR_SUCCESS;
            }

            Socks5ProxyOptions::Socks5ProxyOptions(
                const String &hostName,
                uint32_t port,
                const Socks5ProxyAuthConfig &authConfig,
                uint32_t connectionTimeoutMs,
                AwsSocks5HostResolutionMode resolutionMode,
                struct aws_allocator *allocator)
                : Socks5ProxyOptions()
            {
                m_allocator = allocator ? allocator : aws_default_allocator();

                if (!SetProxyEndpoint(hostName, port))
                {
                    return;
                }

                SetConnectionTimeoutMs(connectionTimeoutMs);
                SetHostResolutionMode(resolutionMode);

                if (!SetAuth(authConfig))
                {
                    return;
                }

                m_lastError = AWS_ERROR_SUCCESS;
            }

            Socks5ProxyOptions::~Socks5ProxyOptions()
            {
                aws_socks5_proxy_options_clean_up(&m_options);
            }

            Socks5ProxyOptions::operator bool() const noexcept
            {
                return m_options.host != nullptr;
            }

            int Socks5ProxyOptions::LastError() const noexcept
            {
                return m_lastError;
            }

            bool Socks5ProxyOptions::SetProxyEndpoint(const String &hostName, uint32_t port)
            {
                if (hostName.empty() || port > UINT16_MAX)
                {
                    m_lastError = AWS_ERROR_INVALID_ARGUMENT;
                    return false;
                }

                struct aws_allocator *allocator = m_allocator ? m_allocator : aws_default_allocator();
                m_allocator = allocator;

                aws_socks5_proxy_options newOptions;
                AWS_ZERO_STRUCT(newOptions);

                aws_byte_cursor hostCursor =
                    aws_byte_cursor_from_array(reinterpret_cast<const uint8_t *>(hostName.data()), hostName.length());

                if (aws_socks5_proxy_options_init(&newOptions, allocator, hostCursor, static_cast<uint16_t>(port)))
                {
                    m_lastError = aws_last_error();
                    aws_socks5_proxy_options_clean_up(&newOptions);
                    return false;
                }

                uint32_t previousTimeout = m_options.connection_timeout_ms;
                AwsSocks5HostResolutionMode previousMode = GetHostResolutionMode();
                newOptions.connection_timeout_ms = previousTimeout;
                aws_socks5_proxy_options_set_host_resolution_mode(
                    &newOptions, static_cast<aws_socks5_host_resolution_mode>(previousMode));

                if (!ApplyAuthConfig(newOptions, m_authConfig))
                {
                    aws_socks5_proxy_options_clean_up(&newOptions);
                    return false;
                }

                aws_socks5_proxy_options_clean_up(&m_options);
                m_options = newOptions;
                AWS_ZERO_STRUCT(newOptions);

                m_lastError = AWS_ERROR_SUCCESS;
                return true;
            }

            bool Socks5ProxyOptions::SetAuth(const Socks5ProxyAuthConfig &authConfig)
            {
                if (authConfig.Method == AwsSocks5AuthMethod::None)
                {
                    if (authConfig.Username.has_value() || authConfig.Password.has_value())
                    {
                        m_lastError = AWS_ERROR_INVALID_ARGUMENT;
                        return false;
                    }
                }
                else if (authConfig.Method == AwsSocks5AuthMethod::UsernamePassword)
                {
                    if (!authConfig.Username.has_value() || authConfig.Username->empty() ||
                        !authConfig.Password.has_value() || authConfig.Password->empty())
                    {
                        m_lastError = AWS_ERROR_INVALID_ARGUMENT;
                        return false;
                    }
                }
                else
                {
                    m_lastError = AWS_ERROR_INVALID_ARGUMENT;
                    return false;
                }

                if (!ApplyAuthConfig(m_options, authConfig))
                {
                    return false;
                }

                m_authConfig = authConfig;
                m_lastError = AWS_ERROR_SUCCESS;
                return true;
            }

            bool Socks5ProxyOptions::SetAuthCredentials(const String &username, const String &password)
            {
                return SetAuth(Socks5ProxyAuthConfig::CreateUsernamePassword(username, password));
            }

            void Socks5ProxyOptions::ClearAuthCredentials()
            {
                Socks5ProxyAuthConfig noneConfig = Socks5ProxyAuthConfig::CreateNone();
                (void)SetAuth(noneConfig);
            }

            void Socks5ProxyOptions::SetConnectionTimeoutMs(uint32_t timeoutMs)
            {
                m_options.connection_timeout_ms = timeoutMs;
            }

            Socks5ProxyOptions::Socks5ProxyOptions(const Socks5ProxyOptions &other) : Socks5ProxyOptions()
            {
                m_allocator = other.m_allocator ? other.m_allocator : aws_default_allocator();
                m_lastError = AWS_ERROR_SUCCESS;
                aws_socks5_proxy_options_clean_up(&m_options);
                if (aws_socks5_proxy_options_copy(&m_options, &other.m_options) != AWS_OP_SUCCESS)
                {
                    m_lastError = aws_last_error();
                    aws_socks5_proxy_options_init_default(&m_options);
                    m_authConfig = Socks5ProxyAuthConfig::CreateNone();
                }
                else
                {
                    m_authConfig = other.m_authConfig;
                }
            }

            Socks5ProxyOptions::Socks5ProxyOptions(Socks5ProxyOptions &&other) noexcept
                : m_options(other.m_options), m_allocator(other.m_allocator), m_lastError(other.m_lastError),
                  m_authConfig(std::move(other.m_authConfig))
            {
                AWS_ZERO_STRUCT(other.m_options);
                other.m_allocator = aws_default_allocator();
                other.m_lastError = AWS_ERROR_SUCCESS;
                other.m_authConfig = Socks5ProxyAuthConfig::CreateNone();
            }

            Socks5ProxyOptions &Socks5ProxyOptions::operator=(const Socks5ProxyOptions &other)
            {
                if (this != &other)
                {
                    aws_socks5_proxy_options_clean_up(&m_options);
                    m_allocator = other.m_allocator ? other.m_allocator : aws_default_allocator();
                    if (aws_socks5_proxy_options_copy(&m_options, &other.m_options) != AWS_OP_SUCCESS)
                    {
                        m_lastError = aws_last_error();
                        aws_socks5_proxy_options_init_default(&m_options);
                        m_authConfig = Socks5ProxyAuthConfig::CreateNone();
                    }
                    else
                    {
                        m_lastError = AWS_ERROR_SUCCESS;
                        m_authConfig = other.m_authConfig;
                    }
                }
                return *this;
            }

            Socks5ProxyOptions &Socks5ProxyOptions::operator=(Socks5ProxyOptions &&other) noexcept
            {
                if (this != &other)
                {
                    aws_socks5_proxy_options_clean_up(&m_options);
                    m_options = other.m_options;
                    m_allocator = other.m_allocator;
                    m_lastError = other.m_lastError;
                    m_authConfig = std::move(other.m_authConfig);
                    AWS_ZERO_STRUCT(other.m_options);
                    other.m_allocator = aws_default_allocator();
                    other.m_lastError = AWS_ERROR_SUCCESS;
                    other.m_authConfig = Socks5ProxyAuthConfig::CreateNone();
                }
                return *this;
            }

            Optional<String> Socks5ProxyOptions::GetHost() const
            {
                if (m_options.host && m_options.host->len > 0)
                {
                    return String(reinterpret_cast<const char *>(m_options.host->bytes), m_options.host->len);
                }
                return Optional<String>();
            }

            uint16_t Socks5ProxyOptions::GetPort() const
            {
                return m_options.port;
            }

            Optional<String> Socks5ProxyOptions::GetUsername() const
            {
                if (m_options.username && m_options.username->len > 0)
                {
                    return String(reinterpret_cast<const char *>(m_options.username->bytes), m_options.username->len);
                }
                return Optional<String>();
            }

            Optional<String> Socks5ProxyOptions::GetPassword() const
            {
                if (m_options.password && m_options.password->len > 0)
                {
                    return String(reinterpret_cast<const char *>(m_options.password->bytes), m_options.password->len);
                }
                return Optional<String>();
            }

            uint32_t Socks5ProxyOptions::GetConnectionTimeoutMs() const
            {
                return m_options.connection_timeout_ms;
            }

            AwsSocks5AuthMethod Socks5ProxyOptions::GetAuthMethod() const
            {
                return m_authConfig.Method;
            }

            AwsSocks5HostResolutionMode Socks5ProxyOptions::GetResolutionMode() const
            {
                return GetHostResolutionMode();
            }

            Optional<Socks5ProxyOptions> Socks5ProxyOptions::CreateFromUri(
                const Uri &uri,
                uint32_t connectionTimeoutMs,
                struct aws_allocator *allocator)
            {
                ByteCursor schemeCursor = uri.GetScheme();
                if (schemeCursor.len == 0)
                {
                    aws_raise_error(AWS_ERROR_INVALID_ARGUMENT);
                    return Optional<Socks5ProxyOptions>();
                }

                String scheme(reinterpret_cast<const char *>(schemeCursor.ptr), schemeCursor.len);
                std::transform(
                    scheme.begin(),
                    scheme.end(),
                    scheme.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                if (scheme != "socks5" && scheme != "socks5h")
                {
                    aws_raise_error(AWS_ERROR_INVALID_ARGUMENT);
                    return Optional<Socks5ProxyOptions>();
                }

                AwsSocks5HostResolutionMode resolutionMode =
                    (scheme == "socks5h") ? AwsSocks5HostResolutionMode::Proxy : AwsSocks5HostResolutionMode::Client;

                ByteCursor hostCursor = uri.GetHostName();
                if (hostCursor.len == 0)
                {
                    aws_raise_error(AWS_ERROR_INVALID_ARGUMENT);
                    return Optional<Socks5ProxyOptions>();
                }

                String host(reinterpret_cast<const char *>(hostCursor.ptr), hostCursor.len);

                uint32_t port = uri.GetPort();
                if (port == 0)
                {
                    port = Socks5ProxyOptions::DefaultProxyPort;
                }
                if (port > UINT16_MAX)
                {
                    aws_raise_error(AWS_ERROR_INVALID_ARGUMENT);
                    return Optional<Socks5ProxyOptions>();
                }

                String username;
                String password;

                ByteCursor authorityCursor = uri.GetAuthority();
                if (authorityCursor.len > 0)
                {
                    String authority(reinterpret_cast<const char *>(authorityCursor.ptr), authorityCursor.len);
                    auto atPos = authority.find('@');
                    if (atPos != String::npos)
                    {
                        String userinfo = authority.substr(0, atPos);
                        auto colonPos = userinfo.find(':');
                        if (colonPos == String::npos)
                        {
                            username = userinfo;
                        }
                        else
                        {
                            username = userinfo.substr(0, colonPos);
                            password = userinfo.substr(colonPos + 1);
                        }
                    }
                }

                Socks5ProxyAuthConfig authConfig = Socks5ProxyAuthConfig::CreateNone();
                if (!username.empty() && !password.empty())
                {
                    authConfig = Socks5ProxyAuthConfig::CreateUsernamePassword(username, password);
                }

                Socks5ProxyOptions options(
                    host,
                    static_cast<uint32_t>(port),
                    authConfig,
                    connectionTimeoutMs,
                    resolutionMode,
                    allocator);

                if (!options)
                {
                    return Optional<Socks5ProxyOptions>();
                }

                if (options.LastError() != AWS_ERROR_SUCCESS)
                {
                    aws_raise_error(options.LastError());
                    return Optional<Socks5ProxyOptions>();
                }

                return Optional<Socks5ProxyOptions>(std::move(options));
            }

            void Socks5ProxyOptions::SetHostResolutionMode(AwsSocks5HostResolutionMode mode)
            {
                aws_socks5_proxy_options_set_host_resolution_mode(
                    &m_options, static_cast<aws_socks5_host_resolution_mode>(mode));
            }

            AwsSocks5HostResolutionMode Socks5ProxyOptions::GetHostResolutionMode() const
            {
                return static_cast<AwsSocks5HostResolutionMode>(
                    aws_socks5_proxy_options_get_host_resolution_mode(&m_options));
            }

            bool Socks5ProxyOptions::ApplyAuthConfig(
                aws_socks5_proxy_options &options,
                const Socks5ProxyAuthConfig &authConfig)
            {
                struct aws_allocator *allocator = m_allocator ? m_allocator : aws_default_allocator();
                m_allocator = allocator;

                switch (authConfig.Method)
                {
                    case AwsSocks5AuthMethod::None:
                    {
                        aws_byte_cursor emptyCursor;
                        AWS_ZERO_STRUCT(emptyCursor);
                        if (aws_socks5_proxy_options_set_auth(&options, allocator, emptyCursor, emptyCursor))
                        {
                            m_lastError = aws_last_error();
                            return false;
                        }
                        return true;
                    }
                    case AwsSocks5AuthMethod::UsernamePassword:
                    {
                        if (!authConfig.Username.has_value() || authConfig.Username->empty() ||
                            !authConfig.Password.has_value() || authConfig.Password->empty())
                        {
                            m_lastError = AWS_ERROR_INVALID_ARGUMENT;
                            return false;
                        }

                        aws_byte_cursor usernameCursor = aws_byte_cursor_from_array(
                            reinterpret_cast<const uint8_t *>(authConfig.Username->data()), authConfig.Username->length());
                        aws_byte_cursor passwordCursor = aws_byte_cursor_from_array(
                            reinterpret_cast<const uint8_t *>(authConfig.Password->data()), authConfig.Password->length());

                        if (aws_socks5_proxy_options_set_auth(&options, allocator, usernameCursor, passwordCursor))
                        {
                            m_lastError = aws_last_error();
                            return false;
                        }
                        return true;
                    }
                    default:
                        m_lastError = AWS_ERROR_INVALID_ARGUMENT;
                        return false;
                }
            }

        } // namespace Io
    }     // namespace Crt
} // namespace Aws
