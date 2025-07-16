#pragma once

/*
 *Copyright 2010-2017 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 *Licensed under the Apache License, Version 2.0 (the "License").
 *You may not use this file except in compliance with the License.
 *A copy of the License is located at
 *
 * http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#if defined(AWS_CRT_USE_WINDOWS_DLL_SEMANTICS) || defined(_WIN32)
#    ifdef _MSC_VER
#        pragma warning(disable : 4251)
#    endif // _MSC_VER
#    ifdef AWS_CRT_CPP_USE_IMPORT_EXPORT
#        ifdef AWS_CRT_CPP_EXPORTS
#            define AWS_CRT_CPP_API __declspec(dllexport)
#        else
#            define AWS_CRT_CPP_API __declspec(dllimport)
#        endif /* AWS_CRT_CPP_API */
#    else
#        define AWS_CRT_CPP_API
#    endif // AWS_CRT_CPP_USE_IMPORT_EXPORT

#else // defined (AWS_CRT_USE_WINDOWS_DLL_SEMANTICS) || defined (_WIN32)
#    if defined(AWS_CRT_CPP_USE_IMPORT_EXPORT) && defined(AWS_CRT_CPP_EXPORTS)
#        define AWS_CRT_CPP_API __attribute__((visibility("default")))
#    else
#        define AWS_CRT_CPP_API
#    endif
#endif

/*
 * Deprecation warnings are emitted unless callers
 * compile with -DAWS_CRT_DISABLE_DEPRECATION_WARNINGS.
 */
#ifndef AWS_CRT_SOFT_DEPRECATED
#    if !defined(AWS_CRT_DISABLE_DEPRECATION_WARNINGS)
#        if defined(__has_attribute) /* Clang, GCC, Apple Clang, ICC all implement __has_attribute */
#            if __has_attribute(deprecated)
/* In these instances, we will use what's already provided */
#                define AWS_CRT_SOFT_DEPRECATED(msg) __attribute__((deprecated(msg)))
#            endif
#        endif
/* We fallback to standard C++14 or MSVC syntax */
#        if !defined(AWS_CRT_SOFT_DEPRECATED)
#            if __cplusplus >= 201402L /* C++14 supports [[deprecated]]   */
#                define AWS_CRT_SOFT_DEPRECATED(msg) [[deprecated(msg)]]
#            elif defined(_MSC_VER) /* Older MSVC                      */
#                define AWS_CRT_SOFT_DEPRECATED(msg) __declspec(deprecated(msg))
#            else /* Unknown compiler → bail out     */
#                define AWS_CRT_SOFT_DEPRECATED(msg)
#            endif
#        endif
#    else
#        define AWS_CRT_SOFT_DEPRECATED(msg)
#    endif
#endif
