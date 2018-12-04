#pragma once
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
#include <aws/crt/Exports.h>

#include <aws/crt/Optional.h>

#include <aws/common/common.h>
#include <aws/mqtt/mqtt.h>
#include <aws/io/socket.h>

#include <string>
#include <map>
#include <unordered_map>
#include <vector>

struct aws_allocator;
struct aws_byte_buf;
struct aws_byte_cursor;
struct aws_socket_options;

namespace Aws
{
    namespace Crt
    {
        using Allocator = aws_allocator;
        using ByteBuf = aws_byte_buf;
        using ByteCursor = aws_byte_cursor;

        AWS_CRT_CPP_API Allocator* DefaultAllocator() noexcept;
        AWS_CRT_CPP_API ByteBuf ByteBufFromCString(const char* str) noexcept;
        AWS_CRT_CPP_API ByteBuf ByteBufFromArray(const uint8_t *array, size_t len) noexcept;
        
        namespace Io
        {
           using SocketOptions = aws_socket_options;
        }

        namespace Mqtt
        {
            using QOS = aws_mqtt_qos;
            using ReturnCode = aws_mqtt_connect_return_code;
        }

        template<typename T>
        class StlAllocator;
        using String = std::basic_string<char, std::char_traits<char>, StlAllocator<char>>;
        template<typename K, typename V> using Map = std::map<K, V, std::less<K>, StlAllocator<std::pair<const K, V>>>;
        template<typename K, typename V> using UnorderedMap =
        std::unordered_map< K, V, std::hash<K>, std::equal_to<K>, StlAllocator<std::pair<const K, V>>>;
        template<typename K, typename V> using MultiMap =
        std::multimap<K, V, std::less<K>, StlAllocator<std::pair<const K, V>>>;
        template<typename T> using Vector = std::vector< T, StlAllocator<T>>;


    }
}
