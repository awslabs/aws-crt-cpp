#pragma once

/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
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
