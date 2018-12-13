/*
  * Copyright 2010-2017 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include <aws/crt/JsonObject.h>

#include <aws/crt/external/cJSON.h>

#include <iterator>
#include <algorithm>

namespace Aws
{
    namespace Crt
    {
        JsonObject::JsonObject() : m_wasParseSuccessful(true)
        {
            m_value = nullptr;
        }

        JsonObject::JsonObject(cJSON* value) :
                m_value(cJSON_Duplicate(value, true /* recurse */)),
                m_wasParseSuccessful(true)
        {
        }

        JsonObject::JsonObject(const String& value) : m_wasParseSuccessful(true)
        {
            const char* return_parse_end;
            m_value = cJSON_ParseWithOpts(value.c_str(), value.length(), &return_parse_end);

            if (!m_value || cJSON_IsInvalid(m_value))
            {
                m_wasParseSuccessful = false;
                m_errorMessage = "Failed to parse JSON at: ";
                m_errorMessage += return_parse_end;
            }
        }

        JsonObject::JsonObject(const JsonObject& value) :
                m_value(cJSON_Duplicate(value.m_value, true/*recurse*/)),
                m_wasParseSuccessful(value.m_wasParseSuccessful),
                m_errorMessage(value.m_errorMessage)
        {
        }

        JsonObject::JsonObject(JsonObject&& value) :
                m_value(value.m_value),
                m_wasParseSuccessful(value.m_wasParseSuccessful),
                m_errorMessage(std::move(value.m_errorMessage))
        {
            value.m_value = nullptr;
        }

        void JsonObject::Destroy()
        {
            cJSON_Delete(m_value);
        }

        JsonObject::~JsonObject()
        {
            Destroy();
        }

        JsonObject& JsonObject::operator=(const JsonObject& other)
        {
            if (this == &other)
            {
                return *this;
            }

            Destroy();
            m_value = cJSON_Duplicate(other.m_value, true /*recurse*/);
            m_wasParseSuccessful = other.m_wasParseSuccessful;
            m_errorMessage = other.m_errorMessage;
            return *this;
        }

        JsonObject& JsonObject::operator=(JsonObject&& other)
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

        static void AddOrReplace(cJSON* root, const char* key, cJSON* value)
        {
            const auto existing = cJSON_GetObjectItemCaseSensitive(root, key);
            if (existing)
            {
                cJSON_ReplaceItemInObjectCaseSensitive(root, key, value);
            }
            else
            {
                cJSON_AddItemToObject(root, key, value);
            }
        }

        JsonObject& JsonObject::WithString(const char* key, const String& value)
        {
            if (!m_value)
            {
                m_value = cJSON_CreateObject();
            }

            const auto val = cJSON_CreateString(value.c_str());
            AddOrReplace(m_value, key, val);
            return *this;
        }

        JsonObject& JsonObject::WithString(const String& key, const String& value)
        {
            return WithString(key.c_str(), value);
        }

        JsonObject& JsonObject::AsString(const String& value)
        {
            Destroy();
            m_value = cJSON_CreateString(value.c_str());
            return *this;
        }

        JsonObject& JsonObject::WithBool(const char* key, bool value)
        {
            if (!m_value)
            {
                m_value = cJSON_CreateObject();
            }

            const auto val = cJSON_CreateBool(value);
            AddOrReplace(m_value, key, val);
            return *this;
        }

        JsonObject& JsonObject::WithBool(const String& key, bool value)
        {
            return WithBool(key.c_str(), value);
        }

        JsonObject& JsonObject::AsBool(bool value)
        {
            Destroy();
            m_value = cJSON_CreateBool(value);
            return *this;
        }

        JsonObject& JsonObject::WithInteger(const char* key, int value)
        {
            return WithDouble(key, static_cast<double>(value));
        }

        JsonObject& JsonObject::WithInteger(const String& key, int value)
        {
            return WithDouble(key.c_str(), static_cast<double>(value));
        }

        JsonObject& JsonObject::AsInteger(int value)
        {
            Destroy();
            m_value = cJSON_CreateNumber(static_cast<double>(value));
            return *this;
        }

        JsonObject& JsonObject::WithInt64(const char* key, long long value)
        {
            return WithDouble(key, static_cast<double>(value));
        }

        JsonObject& JsonObject::WithInt64(const String& key, long long value)
        {
            return WithDouble(key.c_str(), static_cast<double>(value));
        }

        JsonObject& JsonObject::AsInt64(long long value)
        {
            return AsDouble(static_cast<double>(value));
        }

        JsonObject& JsonObject::WithDouble(const char* key, double value)
        {
            if (!m_value)
            {
                m_value = cJSON_CreateObject();
            }

            const auto val = cJSON_CreateNumber(value);
            AddOrReplace(m_value, key, val);
            return *this;
        }

        JsonObject& JsonObject::WithDouble(const String& key, double value)
        {
            return WithDouble(key.c_str(), value);
        }

        JsonObject& JsonObject::AsDouble(double value)
        {
            Destroy();
            m_value = cJSON_CreateNumber(value);
            return *this;
        }

        JsonObject& JsonObject::WithArray(const char* key, const Vector<String>& array)
        {
            if (!m_value)
            {
                m_value = cJSON_CreateObject();
            }

            auto arrayValue = cJSON_CreateArray();
            for (unsigned i = 0; i < array.size(); ++i)
            {
                cJSON_AddItemToArray(arrayValue, cJSON_CreateString(array[i].c_str()));
            }

            AddOrReplace(m_value, key, arrayValue);
            return *this;
        }

        JsonObject& JsonObject::WithArray(const String& key, const Vector<String>& array)
        {
            return WithArray(key.c_str(), array);
        }

        JsonObject& JsonObject::WithArray(const String& key, const Vector<JsonObject>& array)
        {
            if (!m_value)
            {
                m_value = cJSON_CreateObject();
            }

            auto arrayValue = cJSON_CreateArray();
            for (unsigned i = 0; i < array.size(); ++i)
            {
                cJSON_AddItemToArray(arrayValue, cJSON_Duplicate(array[i].m_value, true /*recurse*/));
            }

            AddOrReplace(m_value, key.c_str(), arrayValue);
            return *this;
        }

        JsonObject& JsonObject::WithArray(const String& key, Vector<JsonObject>&& array)
        {
            if (!m_value)
            {
                m_value = cJSON_CreateObject();
            }

            auto arrayValue = cJSON_CreateArray();
            for (unsigned i = 0; i < array.size(); ++i)
            {
                cJSON_AddItemToArray(arrayValue, array[i].m_value);
                array[i].m_value = nullptr;
            }

            AddOrReplace(m_value, key.c_str(), arrayValue);
            return *this;
        }

        JsonObject& JsonObject::AsArray(const Vector<JsonObject>& array)
        {
            auto arrayValue = cJSON_CreateArray();
            for (unsigned i = 0; i < array.size(); ++i)
            {
                cJSON_AddItemToArray(arrayValue, cJSON_Duplicate(array[i].m_value, true /*recurse*/));
            }

            Destroy();
            m_value = arrayValue;
            return *this;
        }

        JsonObject& JsonObject::AsArray(Vector<JsonObject>&& array)
        {
            auto arrayValue = cJSON_CreateArray();
            for (unsigned i = 0; i < array.size(); ++i)
            {
                cJSON_AddItemToArray(arrayValue, array[i].m_value);
                array[i].m_value = nullptr;
            }

            Destroy();
            m_value = arrayValue;
            return *this;
        }

        JsonObject& JsonObject::WithObject(const char* key, const JsonObject& value)
        {
            if (!m_value)
            {
                m_value = cJSON_CreateObject();
            }

            const auto copy = value.m_value == nullptr ? cJSON_CreateObject() : cJSON_Duplicate(value.m_value, true /*recurse*/);
            AddOrReplace(m_value, key, copy);
            return *this;
        }

        JsonObject& JsonObject::WithObject(const String& key, const JsonObject& value)
        {
            return WithObject(key.c_str(), value);
        }

        JsonObject& JsonObject::WithObject(const char* key, JsonObject&& value)
        {
            if (!m_value)
            {
                m_value = cJSON_CreateObject();
            }

            AddOrReplace(m_value, key, value.m_value == nullptr ? cJSON_CreateObject() : value.m_value);
            value.m_value = nullptr;
            return *this;
        }

        JsonObject& JsonObject::WithObject(const String& key, JsonObject&& value)
        {
            return WithObject(key.c_str(), std::move(value));
        }

        JsonObject& JsonObject::AsObject(const JsonObject& value)
        {
            *this = value;
            return *this;
        }

        JsonObject& JsonObject::AsObject(JsonObject && value)
        {
            *this = std::move(value);
            return *this;
        }

        bool JsonObject::operator==(const JsonObject& other) const
        {
            return cJSON_Compare(m_value, other.m_value, true /*case-sensitive*/) != 0;
        }

        bool JsonObject::operator!=(const JsonObject& other) const
        {
            return !(*this == other);
        }

        JsonView JsonObject::View() const
        {
            return *this;
        }

        JsonView::JsonView() : m_value(nullptr)
        {
        }

        JsonView::JsonView(const JsonObject& val) : m_value(val.m_value)
        {
        }

        JsonView::JsonView(cJSON* val) : m_value(val)
        {
        }

        JsonView& JsonView::operator=(const JsonObject& v)
        {
            m_value = v.m_value;
            return *this;
        }

        JsonView& JsonView::operator=(cJSON* val)
        {
            m_value = val;
            return *this;
        }

        String JsonView::GetString(const String& key) const
        {
           return GetString(key.c_str());
        }

        String JsonView::GetString(const char* key) const
        {
            assert(m_value);
            auto item = cJSON_GetObjectItemCaseSensitive(m_value, key);
            auto str = cJSON_GetStringValue(item);
            return str ? str : "";
        }

        String JsonView::AsString() const
        {
            const char* str = cJSON_GetStringValue(m_value);
            if (str == nullptr)
            {
                return {};
            }
            return str;
        }

        bool JsonView::GetBool(const String& key) const
        {
            return GetBool(key.c_str());
        }

        bool JsonView::GetBool(const char* key) const
        {
            assert(m_value);
            auto item = cJSON_GetObjectItemCaseSensitive(m_value, key);
            assert(item);
            return item->valueint != 0;
        }

        bool JsonView::AsBool() const
        {
            assert(cJSON_IsBool(m_value));
            return cJSON_IsTrue(m_value) != 0;
        }

        int JsonView::GetInteger(const String& key) const
        {
            return GetInteger(key.c_str());
        }

        int JsonView::GetInteger(const char* key) const
        {
            assert(m_value);
            auto item = cJSON_GetObjectItemCaseSensitive(m_value, key);
            assert(item);
            return item->valueint;
        }

        int JsonView::AsInteger() const
        {
            assert(cJSON_IsNumber(m_value)); // can be double or value larger than int_max, but at least not UB
            return m_value->valueint;
        }

        int64_t JsonView::GetInt64(const String& key) const
        {
            return static_cast<int64_t>(GetDouble(key));
        }

        int64_t JsonView::GetInt64(const char* key) const
        {
            return static_cast<int64_t>(GetDouble(key));
        }

        int64_t JsonView::AsInt64() const
        {
            assert(cJSON_IsNumber(m_value));
            return static_cast<long long>(m_value->valuedouble);
        }

        double JsonView::GetDouble(const String& key) const
        {
            return GetDouble(key.c_str());
        }

        double JsonView::GetDouble(const char* key) const
        {
            assert(m_value);
            auto item = cJSON_GetObjectItemCaseSensitive(m_value, key);
            assert(item);
            return item->valuedouble;
        }

        double JsonView::AsDouble() const
        {
            assert(cJSON_IsNumber(m_value));
            return m_value->valuedouble;
        }

        JsonView JsonView::GetObject(const String& key) const
        {
            return GetObject(key.c_str());
        }

        JsonView JsonView::GetObject(const char* key) const
        {
            assert(m_value);
            auto item = cJSON_GetObjectItemCaseSensitive(m_value, key);
            return item;
        }

        JsonObject JsonView::GetObjectCopy(const String& key) const
        {
            return GetObjectCopy(key.c_str());
        }

        JsonObject JsonView::GetObjectCopy(const char* key) const
        {
            assert(m_value);
            /* force a deep copy */
            return JsonObject(cJSON_GetObjectItemCaseSensitive(m_value, key));
        }

        JsonView JsonView::AsObject() const
        {
            assert(cJSON_IsObject(m_value));
            return m_value;
        }

        Vector<JsonView> JsonView::GetArray(const String& key) const
        {
            return GetArray(key.c_str());
        }

        Vector<JsonView> JsonView::GetArray(const char* key) const
        {
            assert(m_value);
            auto array = cJSON_GetObjectItemCaseSensitive(m_value, key);
            assert(cJSON_IsArray(array));
            Vector<JsonView> returnArray(static_cast<size_t>(cJSON_GetArraySize(array)));

            auto element = array->child;
            for (unsigned i = 0; element && i < returnArray.size(); ++i, element = element->next)
            {
                returnArray[i] = element;
            }

            return returnArray;
        }

        Vector<JsonView> JsonView::AsArray() const
        {
            assert(cJSON_IsArray(m_value));
            Vector<JsonView> returnArray(static_cast<size_t>(cJSON_GetArraySize(m_value)));

            auto element = m_value->child;

            for (unsigned i = 0; element && i < returnArray.size(); ++i, element = element->next)
            {
                returnArray[i] = element;
            }

            return returnArray;
        }

        Map<String, JsonView> JsonView::GetAllObjects() const
        {
            Map<String, JsonView> valueMap;
            if (!m_value)
            {
                return valueMap;
            }

            for (auto iter = m_value->child; iter; iter = iter->next)
            {
                valueMap.emplace(std::make_pair(String(iter->string), JsonView(iter)));
            }

            return valueMap;
        }

        bool JsonView::ValueExists(const String& key) const
        {
            return ValueExists(key.c_str());
        }

        bool JsonView::ValueExists(const char* key) const
        {
            if (!cJSON_IsObject(m_value))
            {
                return false;
            }

            auto item = cJSON_GetObjectItemCaseSensitive(m_value, key);
            return !(item == nullptr || cJSON_IsNull(item));
        }

        bool JsonView::KeyExists(const String& key) const
        {
            return KeyExists(key.c_str());
        }

        bool JsonView::KeyExists(const char* key) const
        {
            if (!cJSON_IsObject(m_value))
            {
                return false;
            }

            return cJSON_GetObjectItemCaseSensitive(m_value, key) != nullptr;
        }

        bool JsonView::IsObject() const
        {
            return cJSON_IsObject(m_value) != 0;
        }

        bool JsonView::IsBool() const
        {
            return cJSON_IsBool(m_value) != 0;
        }

        bool JsonView::IsString() const
        {
            return cJSON_IsString(m_value) != 0;
        }

        bool JsonView::IsIntegerType() const
        {
            if (!cJSON_IsNumber(m_value))
            {
                return false;
            }

            return m_value->valuedouble == static_cast<long long>(m_value->valuedouble);
        }

        bool JsonView::IsFloatingPointType() const
        {
            if (!cJSON_IsNumber(m_value))
            {
                return false;
            }

            return m_value->valuedouble != static_cast<long long>(m_value->valuedouble);
        }

        bool JsonView::IsListType() const
        {
            return cJSON_IsArray(m_value) != 0;
        }

        bool JsonView::IsNull() const
        {
            return cJSON_IsNull(m_value) != 0;
        }

        String JsonView::WriteCompact(bool treatAsObject) const
        {
            if (!m_value)
            {
                if (treatAsObject)
                {
                    return "{}";
                }
                return "";
            }

            auto temp = cJSON_PrintUnformatted(m_value);
            String out(temp);
            cJSON_free(temp);
            return out;
        }

        String JsonView::WriteReadable(bool treatAsObject) const
        {
            if (!m_value)
            {
                if (treatAsObject)
                {
                    return "{\n}\n";
                }
                return "";
            }

            auto temp = cJSON_Print(m_value);
            String out(temp);
            cJSON_free(temp);
            return out;
        }

        JsonObject JsonView::Materialize() const
        {
            return m_value;
        }
    }
}
