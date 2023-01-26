/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/JsonObject.h>

#include <aws/common/json.h>

#include <algorithm>
#include <iterator>

namespace Aws
{
    namespace Crt
    {
        JsonObject::JsonObject() : m_wasParseSuccessful(true) { m_value = nullptr; }

        JsonObject::JsonObject(aws_json_value *value)
            : m_value(aws_json_value_duplicate(value)), m_wasParseSuccessful(true)
        {
        }

        JsonObject::JsonObject(const String &value) : m_wasParseSuccessful(true)
        {
            m_value = aws_json_value_new_from_string(ApiAllocator(), aws_byte_cursor_from_c_str(value.c_str()));

            if (m_value == nullptr || aws_json_value_is_null(m_value) == true)
            {
                m_wasParseSuccessful = false;
                m_errorMessage = "Failed to parse JSON: " + value;
            }
        }

        JsonObject::JsonObject(const JsonObject &value)
            : m_value(aws_json_value_duplicate(value.m_value)), m_wasParseSuccessful(value.m_wasParseSuccessful),
              m_errorMessage(value.m_errorMessage)
        {
        }

        JsonObject::JsonObject(JsonObject &&value) noexcept
            : m_value(value.m_value), m_wasParseSuccessful(value.m_wasParseSuccessful),
              m_errorMessage(std::move(value.m_errorMessage))
        {
            value.m_value = nullptr;
        }

        void JsonObject::Destroy() { aws_json_value_destroy(m_value); }

        JsonObject::~JsonObject() { Destroy(); }

        JsonObject &JsonObject::operator=(const JsonObject &other)
        {
            if (this == &other)
            {
                return *this;
            }

            Destroy();
            m_value = aws_json_value_duplicate(other.m_value);
            m_wasParseSuccessful = other.m_wasParseSuccessful;
            m_errorMessage = other.m_errorMessage;
            return *this;
        }

        JsonObject &JsonObject::operator=(JsonObject &&other) noexcept
        {
            if (this == &other)
            {
                return *this;
            }

            using std::swap;
            swap(m_value, other.m_value);
            swap(m_errorMessage, other.m_errorMessage);
            m_wasParseSuccessful = other.m_wasParseSuccessful;
            return *this;
        }

        static void AddOrReplace(aws_json_value *root, const char *key, aws_json_value *value)
        {
            struct aws_byte_cursor key_cursor = aws_byte_cursor_from_c_str(key);
            const auto existing = aws_json_value_get_from_object(root, key_cursor);
            if (existing != nullptr)
            {
                aws_json_value_remove_from_object(root, key_cursor);
                aws_json_value_add_to_object(root, key_cursor, value);
            }
            else
            {
                aws_json_value_add_to_object(root, key_cursor, value);
            }
        }

        JsonObject &JsonObject::WithString(const char *key, const String &value)
        {
            if (m_value == nullptr)
            {
                m_value = aws_json_value_new_object(ApiAllocator());
            }

            const auto val = aws_json_value_new_string(ApiAllocator(), aws_byte_cursor_from_c_str(value.c_str()));
            AddOrReplace(m_value, key, val);
            return *this;
        }

        JsonObject &JsonObject::WithString(const String &key, const String &value)
        {
            return WithString(key.c_str(), value);
        }

        JsonObject &JsonObject::AsString(const String &value)
        {
            Destroy();
            m_value = aws_json_value_new_string(ApiAllocator(), aws_byte_cursor_from_c_str(value.c_str()));
            return *this;
        }

        JsonObject &JsonObject::WithBool(const char *key, bool value)
        {
            if (m_value == nullptr)
            {
                m_value = aws_json_value_new_object(ApiAllocator());
            }

            const auto val = aws_json_value_new_boolean(ApiAllocator(), value);
            AddOrReplace(m_value, key, val);
            return *this;
        }

        JsonObject &JsonObject::WithBool(const String &key, bool value) { return WithBool(key.c_str(), value); }

        JsonObject &JsonObject::AsBool(bool value)
        {
            Destroy();
            m_value = aws_json_value_new_boolean(ApiAllocator(), value);
            return *this;
        }

        JsonObject &JsonObject::WithInteger(const char *key, int value)
        {
            return WithDouble(key, static_cast<double>(value));
        }

        JsonObject &JsonObject::WithInteger(const String &key, int value)
        {
            return WithDouble(key.c_str(), static_cast<double>(value));
        }

        JsonObject &JsonObject::AsInteger(int value)
        {
            Destroy();
            m_value = aws_json_value_new_number(ApiAllocator(), static_cast<double>(value));
            return *this;
        }

        JsonObject &JsonObject::WithInt64(const char *key, int64_t value)
        {
            return WithDouble(key, static_cast<double>(value));
        }

        JsonObject &JsonObject::WithInt64(const String &key, int64_t value)
        {
            return WithDouble(key.c_str(), static_cast<double>(value));
        }

        JsonObject &JsonObject::AsInt64(int64_t value) { return AsDouble(static_cast<double>(value)); }

        JsonObject &JsonObject::WithDouble(const char *key, double value)
        {
            if (m_value == nullptr)
            {
                m_value = aws_json_value_new_object(ApiAllocator());
            }

            const auto val = aws_json_value_new_number(ApiAllocator(), value);
            AddOrReplace(m_value, key, val);
            return *this;
        }

        JsonObject &JsonObject::WithDouble(const String &key, double value) { return WithDouble(key.c_str(), value); }

        JsonObject &JsonObject::AsDouble(double value)
        {
            Destroy();
            m_value = aws_json_value_new_number(ApiAllocator(), value);
            return *this;
        }

        JsonObject &JsonObject::WithArray(const char *key, const Vector<String> &array)
        {
            if (m_value == nullptr)
            {
                m_value = aws_json_value_new_object(ApiAllocator());
            }

            auto arrayValue = aws_json_value_new_array(ApiAllocator());
            for (const auto &i : array)
            {
                aws_json_value_add_array_element(
                    arrayValue, aws_json_value_new_string(ApiAllocator(), aws_byte_cursor_from_c_str(i.c_str())));
            }

            AddOrReplace(m_value, key, arrayValue);
            return *this;
        }

        JsonObject &JsonObject::WithArray(const String &key, const Vector<String> &array)
        {
            return WithArray(key.c_str(), array);
        }

        JsonObject &JsonObject::WithArray(const String &key, const Vector<JsonObject> &array)
        {
            if (m_value == nullptr)
            {
                m_value = aws_json_value_new_object(ApiAllocator());
            }

            auto arrayValue = aws_json_value_new_array(ApiAllocator());
            for (const auto &i : array)
            {
                aws_json_value_add_array_element(arrayValue, aws_json_value_duplicate(i.m_value));
            }

            AddOrReplace(m_value, key.c_str(), arrayValue);
            return *this;
        }

        JsonObject &JsonObject::WithArray(const String &key, Vector<JsonObject> &&array)
        {
            if (m_value == nullptr)
            {
                m_value = aws_json_value_new_object(ApiAllocator());
            }

            auto arrayValue = aws_json_value_new_array(ApiAllocator());
            for (auto &i : array)
            {
                aws_json_value_add_array_element(arrayValue, i.m_value);
                i.m_value = nullptr;
            }

            AddOrReplace(m_value, key.c_str(), arrayValue);
            return *this;
        }

        JsonObject &JsonObject::AsArray(const Vector<JsonObject> &array)
        {
            auto arrayValue = aws_json_value_new_array(ApiAllocator());
            for (const auto &i : array)
            {
                aws_json_value_add_array_element(arrayValue, aws_json_value_duplicate(i.m_value));
            }

            Destroy();
            m_value = arrayValue;
            return *this;
        }

        JsonObject &JsonObject::AsArray(Vector<JsonObject> &&array)
        {
            auto arrayValue = aws_json_value_new_array(ApiAllocator());
            for (auto &i : array)
            {
                aws_json_value_add_array_element(arrayValue, i.m_value);
                i.m_value = nullptr;
            }

            Destroy();
            m_value = arrayValue;
            return *this;
        }

        JsonObject &JsonObject::AsNull()
        {
            m_value = aws_json_value_new_null(ApiAllocator());
            return *this;
        }

        JsonObject &JsonObject::WithObject(const char *key, const JsonObject &value)
        {
            if (m_value == nullptr)
            {
                m_value = aws_json_value_new_object(ApiAllocator());
            }

            const auto copy = value.m_value == nullptr ? aws_json_value_new_object(ApiAllocator())
                                                       : aws_json_value_duplicate(value.m_value);
            AddOrReplace(m_value, key, copy);
            return *this;
        }

        JsonObject &JsonObject::WithObject(const String &key, const JsonObject &value)
        {
            return WithObject(key.c_str(), value);
        }

        JsonObject &JsonObject::WithObject(const char *key, JsonObject &&value)
        {
            if (m_value == nullptr)
            {
                m_value = aws_json_value_new_object(ApiAllocator());
            }

            AddOrReplace(
                m_value, key, value.m_value == nullptr ? aws_json_value_new_object(ApiAllocator()) : value.m_value);
            value.m_value = nullptr;
            return *this;
        }

        JsonObject &JsonObject::WithObject(const String &key, JsonObject &&value)
        {
            return WithObject(key.c_str(), std::move(value));
        }

        JsonObject &JsonObject::AsObject(const JsonObject &value)
        {
            *this = value;
            return *this;
        }

        JsonObject &JsonObject::AsObject(JsonObject &&value)
        {
            *this = std::move(value);
            return *this;
        }

        bool JsonObject::operator==(const JsonObject &other) const
        {
            return aws_json_value_compare(m_value, other.m_value, true) != 0;
        }

        bool JsonObject::operator!=(const JsonObject &other) const { return !(*this == other); }

        JsonView JsonObject::View() const { return *this; }

        JsonView::JsonView() : m_value(nullptr) {}

        JsonView::JsonView(const JsonObject &val) : m_value(val.m_value) {}

        JsonView::JsonView(aws_json_value *val) : m_value(val) {}

        JsonView &JsonView::operator=(const JsonObject &v)
        {
            m_value = v.m_value;
            return *this;
        }

        JsonView &JsonView::operator=(aws_json_value *val)
        {
            m_value = val;
            return *this;
        }

        String JsonView::GetString(const String &key) const { return GetString(key.c_str()); }

        String JsonView::GetString(const char *key) const
        {
            AWS_ASSERT(m_value);
            auto item = aws_json_value_get_from_object(m_value, aws_byte_cursor_from_c_str(key));
            struct aws_byte_cursor output_cursor;
            if (aws_json_value_get_string(item, &output_cursor) != AWS_OP_SUCCESS)
            {
                AWS_LOGF_ERROR(AWS_LS_COMMON_JSON_PARSER, "Error: Could not get string from JSON with key %s", key);
                return "";
            }

            if (output_cursor.len > 0 && output_cursor.ptr != NULL)
            {
                aws_string *str = aws_string_new_from_cursor(ApiAllocator(), &output_cursor);
                String ret_val(aws_string_c_str(str));
                aws_string_destroy_secure(str);
                return ret_val;
            }
            return "";
        }

        String JsonView::AsString() const
        {
            struct aws_byte_cursor output_cursor;
            if (aws_json_value_get_string(m_value, &output_cursor) != AWS_OP_SUCCESS)
            {
                AWS_LOGF_ERROR(AWS_LS_COMMON_JSON_PARSER, "Error: Could not get string from JSON");
                return "";
            }
            if (output_cursor.len > 0 && output_cursor.ptr != NULL)
            {
                aws_string *str = aws_string_new_from_cursor(ApiAllocator(), &output_cursor);
                String ret_val(aws_string_c_str(str));
                aws_string_destroy_secure(str);
                return ret_val;
            }
            return "";
        }

        bool JsonView::GetBool(const String &key) const { return GetBool(key.c_str()); }

        bool JsonView::GetBool(const char *key) const
        {
            AWS_ASSERT(m_value);
            auto item = aws_json_value_get_from_object(m_value, aws_byte_cursor_from_c_str(key));
            AWS_ASSERT(item);
            bool output = false;
            if (aws_json_value_get_boolean(item, &output) != AWS_OP_SUCCESS)
            {
                AWS_LOGF_ERROR(AWS_LS_COMMON_JSON_PARSER, "Error: Could not get boolean from JSON with key %s", key);
                return false;
            }
            return output;
        }

        bool JsonView::AsBool() const
        {
            AWS_ASSERT(aws_json_value_is_boolean(m_value));
            bool output = false;
            if (aws_json_value_get_boolean(m_value, &output) != AWS_OP_SUCCESS)
            {
                AWS_LOGF_ERROR(AWS_LS_COMMON_JSON_PARSER, "Error: Could not get boolean from JSON");
                return false;
            }
            return output;
        }

        int JsonView::GetInteger(const String &key) const { return GetInteger(key.c_str()); }

        int JsonView::GetInteger(const char *key) const
        {
            AWS_ASSERT(m_value);
            auto item = aws_json_value_get_from_object(m_value, aws_byte_cursor_from_c_str(key));
            AWS_ASSERT(item);
            double output = 0;
            if (aws_json_value_get_number(item, &output) != AWS_OP_SUCCESS)
            {
                AWS_LOGF_ERROR(AWS_LS_COMMON_JSON_PARSER, "Error: Could not get integer from JSON with key %s", key);
                return 0;
            }
            return static_cast<int>(output);
        }

        int JsonView::AsInteger() const
        {
            AWS_ASSERT(aws_json_value_is_number(m_value));
            double output = 0;
            if (aws_json_value_get_number(m_value, &output) != AWS_OP_SUCCESS)
            {
                AWS_LOGF_ERROR(AWS_LS_COMMON_JSON_PARSER, "Error: Could not get integer from JSON");
                return 0;
            };
            return static_cast<int>(output);
        }

        int64_t JsonView::GetInt64(const String &key) const { return static_cast<int64_t>(GetDouble(key)); }

        int64_t JsonView::GetInt64(const char *key) const { return static_cast<int64_t>(GetDouble(key)); }

        int64_t JsonView::AsInt64() const
        {
            AWS_ASSERT(aws_json_value_is_number(m_value));
            double output = 0;
            if (aws_json_value_get_number(m_value, &output) != AWS_OP_SUCCESS)
            {
                AWS_LOGF_ERROR(AWS_LS_COMMON_JSON_PARSER, "Error: Could not get int64 from JSON");
                return 0;
            }
            return static_cast<int64_t>(output);
        }

        double JsonView::GetDouble(const String &key) const { return GetDouble(key.c_str()); }

        double JsonView::GetDouble(const char *key) const
        {
            AWS_ASSERT(m_value);
            auto item = aws_json_value_get_from_object(m_value, aws_byte_cursor_from_c_str(key));
            AWS_ASSERT(item);
            double output = 0;
            if (aws_json_value_get_number(item, &output) != AWS_OP_SUCCESS)
            {
                AWS_LOGF_ERROR(AWS_LS_COMMON_JSON_PARSER, "Error: Could not get double from JSON with key %s", key);
                return 0;
            }
            return output;
        }

        double JsonView::AsDouble() const
        {
            AWS_ASSERT(aws_json_value_is_number(m_value));
            double output = 0;
            if (aws_json_value_get_number(m_value, &output) != AWS_OP_SUCCESS)
            {
                AWS_LOGF_ERROR(AWS_LS_COMMON_JSON_PARSER, "Error: Could not get double from JSON");
                return 0;
            }
            return output;
        }

        JsonView JsonView::GetJsonObject(const String &key) const { return GetJsonObject(key.c_str()); }

        JsonView JsonView::GetJsonObject(const char *key) const
        {
            AWS_ASSERT(m_value);
            auto item = aws_json_value_get_from_object(m_value, aws_byte_cursor_from_c_str(key));
            return item;
        }

        JsonObject JsonView::GetJsonObjectCopy(const String &key) const { return GetJsonObjectCopy(key.c_str()); }

        JsonObject JsonView::GetJsonObjectCopy(const char *key) const
        {
            AWS_ASSERT(m_value);
            /* force a deep copy */
            return JsonObject(aws_json_value_get_from_object(m_value, aws_byte_cursor_from_c_str(key)));
        }

        JsonView JsonView::AsObject() const
        {
            AWS_ASSERT(aws_json_value_is_object(m_value));
            return m_value;
        }

        Vector<JsonView> JsonView::GetArray(const String &key) const { return GetArray(key.c_str()); }

        Vector<JsonView> JsonView::GetArray(const char *key) const
        {
            AWS_ASSERT(m_value);
            auto array = aws_json_value_get_from_object(m_value, aws_byte_cursor_from_c_str(key));
            AWS_ASSERT(aws_json_value_is_array(array));
            Vector<JsonView> returnArray(static_cast<size_t>(aws_json_get_array_size(array)));

            for (size_t i = 0; i < returnArray.size(); i++)
            {
                returnArray[i] = aws_json_get_array_element(array, i);
            }

            return returnArray;
        }

        Vector<JsonView> JsonView::AsArray() const
        {
            AWS_ASSERT(aws_json_value_is_array(m_value));
            Vector<JsonView> returnArray(static_cast<size_t>(aws_json_get_array_size(m_value)));

            auto element = aws_json_get_array_element(m_value, 0);
            for (size_t i = 0; element != nullptr && i < returnArray.size(); i++)
            {
                element = aws_json_get_array_element(m_value, i);
                returnArray[i] = element;
            }

            return returnArray;
        }

        Map<String, JsonView> JsonView::GetAllObjects() const
        {
            Map<String, JsonView> valueMap;
            if (m_value == nullptr)
            {
                return valueMap;
            }

            size_t array_size = aws_json_get_array_size(m_value);
            auto element = aws_json_get_array_element(m_value, 0);
            for (size_t i = 0; element != nullptr && i < array_size; i++)
            {
                element = aws_json_get_array_element(m_value, i);
                aws_byte_cursor element_cursor;
                aws_json_value_get_string(m_value, &element_cursor);

                aws_string *aws_element_str = aws_string_new_from_cursor(ApiAllocator(), &element_cursor);
                String element_str(aws_string_c_str(aws_element_str));
                aws_string_destroy_secure(aws_element_str);

                valueMap.emplace(std::make_pair(element_str, JsonView(element)));
            }

            return valueMap;
        }

        bool JsonView::ValueExists(const String &key) const { return ValueExists(key.c_str()); }

        bool JsonView::ValueExists(const char *key) const
        {
            if (aws_json_value_is_object(m_value) == 0)
            {
                return false;
            }

            auto item = aws_json_value_get_from_object(m_value, aws_byte_cursor_from_c_str(key));
            return !(item == nullptr || aws_json_value_is_null(item) != 0);
        }

        bool JsonView::KeyExists(const String &key) const { return KeyExists(key.c_str()); }

        bool JsonView::KeyExists(const char *key) const
        {
            if (aws_json_value_is_object(m_value) == 0)
            {
                return false;
            }

            return aws_json_value_get_from_object(m_value, aws_byte_cursor_from_c_str(key)) != nullptr;
        }

        bool JsonView::IsObject() const { return aws_json_value_is_object(m_value) != 0; }

        bool JsonView::IsBool() const { return aws_json_value_is_boolean(m_value) != 0; }

        bool JsonView::IsString() const { return aws_json_value_is_string(m_value) != 0; }

        bool JsonView::IsIntegerType() const
        {
            if (aws_json_value_is_number(m_value) == 0)
            {
                return false;
            }
            double value_double = 0;
            aws_json_value_get_number(m_value, &value_double);
            return value_double == static_cast<int64_t>(value_double);
        }

        bool JsonView::IsFloatingPointType() const
        {
            if (aws_json_value_is_number(m_value) == 0)
            {
                return false;
            }
            double value_double = 0;
            aws_json_value_get_number(m_value, &value_double);
            return value_double != static_cast<int64_t>(value_double);
        }

        bool JsonView::IsListType() const { return aws_json_value_is_array(m_value) != 0; }

        bool JsonView::IsNull() const { return aws_json_value_is_null(m_value) != 0; }

        String JsonView::WriteCompact(bool treatAsObject) const
        {
            if (m_value == nullptr)
            {
                if (treatAsObject)
                {
                    return "{}";
                }
                return "";
            }

            struct aws_byte_buf buf;
            aws_byte_buf_init(&buf, ApiAllocator(), 0);
            aws_byte_buf_append_json_string(m_value, &buf);

            struct aws_string *str = aws_string_new_from_buf(ApiAllocator(), &buf);
            String out(aws_string_c_str(str));
            aws_string_destroy_secure(str);

            aws_byte_buf_clean_up(&buf);
            return out;
        }

        String JsonView::WriteReadable(bool treatAsObject) const
        {
            if (m_value == nullptr)
            {
                if (treatAsObject)
                {
                    return "{\n}\n";
                }
                return "";
            }
            struct aws_byte_buf buf;
            aws_byte_buf_init(&buf, ApiAllocator(), 0);
            aws_byte_buf_append_json_string_formatted(m_value, &buf);

            struct aws_string *str = aws_string_new_from_buf(ApiAllocator(), &buf);
            String out(aws_string_c_str(str));
            aws_string_destroy_secure(str);

            aws_byte_buf_clean_up(&buf);
            return out;
        }

        JsonObject JsonView::Materialize() const { return m_value; }
    } // namespace Crt
} // namespace Aws
