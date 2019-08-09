/*
 * Copyright 2010-2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include <aws/testing/aws_test_harness.h>

#include <aws/crt/ByteBuf.h>
#include <aws/testing/aws_test_allocators.h>

using namespace Aws::Crt;

static const char *s_rawString = "IMACSTRING";

static int s_ByteCursorConstruction(struct aws_allocator *allocator, void *)
{
    // Default construction
    ByteCursor emptyCursor;
    ASSERT_TRUE(emptyCursor.Get()->len == 0);
    ASSERT_TRUE(emptyCursor.Get()->ptr == NULL);

    // C string construction
    char cString[] = "TEST";
    ByteCursor cStringCursor(cString);
    size_t cStringLength = AWS_ARRAY_SIZE(cString) - 1;
    ASSERT_TRUE(cStringCursor.Get()->len == cStringLength);
    ASSERT_TRUE(cStringCursor.Get()->ptr == (uint8_t *)cString);

    // aws_byte_cursor construction
    struct aws_byte_cursor rawCursor;
    rawCursor.ptr = (uint8_t *)s_rawString;
    rawCursor.len = strlen(s_rawString);

    ByteCursor rawCopyCursor(rawCursor);
    ASSERT_TRUE(rawCopyCursor.Get()->len == rawCursor.len);
    ASSERT_TRUE(rawCopyCursor.Get()->ptr == rawCursor.ptr);
    ASSERT_TRUE(rawCopyCursor.Get() != &rawCursor);

    ByteCursor rawCopyRefCursor(&rawCursor);
    ASSERT_TRUE(rawCopyRefCursor.Get() == &rawCursor);

    // Copy construction
    ByteCursor copyTargetCursor(rawCopyCursor);
    ASSERT_TRUE(copyTargetCursor.Get()->len == rawCursor.len);
    ASSERT_TRUE(copyTargetCursor.Get()->ptr == rawCursor.ptr);
    ASSERT_TRUE(rawCopyCursor.Get()->len == rawCursor.len);
    ASSERT_TRUE(rawCopyCursor.Get()->ptr == rawCursor.ptr);
    ASSERT_TRUE(copyTargetCursor.Get() != rawCopyCursor.Get());

    ByteCursor copyTargetRefCursor(rawCopyRefCursor);
    ASSERT_TRUE(copyTargetRefCursor.Get() == rawCopyRefCursor.Get() && copyTargetRefCursor.Get() == &rawCursor);

    // Trying to move a cursor should just copy it
    ByteCursor moveTargetCursor(std::move(rawCopyCursor));
    ASSERT_TRUE(moveTargetCursor.Get()->len == rawCursor.len);
    ASSERT_TRUE(moveTargetCursor.Get()->ptr == rawCursor.ptr);
    ASSERT_TRUE(rawCopyCursor.Get()->len == rawCursor.len);
    ASSERT_TRUE(rawCopyCursor.Get()->ptr == rawCursor.ptr);
    ASSERT_TRUE(moveTargetCursor.Get() != rawCopyCursor.Get());

    ByteCursor moveTargetRefCursor(std::move(rawCopyRefCursor));
    ASSERT_TRUE(moveTargetRefCursor.Get() == rawCopyRefCursor.Get() && moveTargetRefCursor.Get() == &rawCursor);

    // Array construction
    ByteCursor arrayCursor((uint8_t *)s_rawString, 3);
    ASSERT_TRUE(arrayCursor.Get()->len == 3);
    ASSERT_TRUE(arrayCursor.Get()->ptr == (uint8_t *)s_rawString);

    // String construction
    String helloWorldString("HelloWorld");
    ByteCursor stringCursor(helloWorldString);
    ASSERT_TRUE(stringCursor.Get()->len == helloWorldString.size());
    ASSERT_TRUE(stringCursor.Get()->ptr == (uint8_t *)helloWorldString.c_str());

    // Byte buf construction
    struct aws_byte_buf buffer = aws_byte_buf_from_c_str(s_rawString);
    ByteCursor bufferCursor(buffer);
    ASSERT_TRUE(bufferCursor.Get()->len == buffer.len);
    ASSERT_TRUE(bufferCursor.Get()->ptr == buffer.buffer);

    ByteCursor bufferPtrCursor(&buffer);
    ASSERT_TRUE(bufferPtrCursor.Get()->len == buffer.len);
    ASSERT_TRUE(bufferPtrCursor.Get()->ptr == buffer.buffer);

    // Assignment
    ByteCursor assignTargetCursor;
    assignTargetCursor = rawCopyCursor;
    ASSERT_TRUE(assignTargetCursor.Get()->len == rawCursor.len);
    ASSERT_TRUE(assignTargetCursor.Get()->ptr == rawCursor.ptr);
    ASSERT_TRUE(assignTargetCursor.Get() != rawCopyCursor.Get());

    ByteCursor assignTargetRefCursor;
    assignTargetRefCursor = rawCopyRefCursor;
    ASSERT_TRUE(assignTargetRefCursor.Get() == rawCopyRefCursor.Get() && assignTargetRefCursor.Get() == &rawCursor);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteCursorConstruction, s_ByteCursorConstruction)

static const char *s_TestString = "ThisIsATest";

static int s_ByteBufConstruction(struct aws_allocator *allocator, void *)
{
    ByteBuf defaultBuffer;
    ASSERT_TRUE(defaultBuffer.Get()->len == 0);
    ASSERT_TRUE(defaultBuffer.Get()->capacity == 0);
    ASSERT_TRUE(defaultBuffer.Get()->buffer == nullptr);
    ASSERT_TRUE(defaultBuffer.Get()->allocator == nullptr);

    ByteBuf stringBuffer(s_TestString);
    ASSERT_TRUE(stringBuffer.Get()->len == strlen(s_TestString));
    ASSERT_TRUE(stringBuffer.Get()->capacity == strlen(s_TestString));
    ASSERT_TRUE(stringBuffer.Get()->buffer == (uint8_t *)s_TestString);
    ASSERT_TRUE(stringBuffer.Get()->allocator == nullptr);

    struct aws_byte_buf buffer = aws_byte_buf_from_c_str(s_TestString);
    ByteBuf pointerBuffer(&buffer);
    ASSERT_TRUE(pointerBuffer.Get() == &buffer);

    ByteBuf arrayBuffer((uint8_t *)s_TestString, 5, 3);
    ASSERT_TRUE(arrayBuffer.Get()->len == 3);
    ASSERT_TRUE(arrayBuffer.Get()->capacity == 5);
    ASSERT_TRUE(arrayBuffer.Get()->buffer == (uint8_t *)s_TestString);
    ASSERT_TRUE(arrayBuffer.Get()->allocator == nullptr);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteBufConstruction, s_ByteBufConstruction)

static int s_ByteBufInitializationFailure(struct aws_allocator *allocator, void *)
{

    struct aws_allocator fail_to_allocate;
    AWS_ZERO_STRUCT(fail_to_allocate);

    ASSERT_SUCCESS(aws_timebomb_allocator_init(&fail_to_allocate, allocator, 0));

    auto goodResult = ByteBuf::InitFromArray(allocator, (uint8_t *)s_TestString, strlen(s_TestString));
    ASSERT_TRUE(goodResult);

    goodResult.GetResult().Get()->allocator = &fail_to_allocate;

    auto copyFailureResult = ByteBuf::Init(goodResult.GetResult());
    ASSERT_FALSE(copyFailureResult);

    goodResult.GetResult().Get()->allocator = allocator;

    auto capacityFailureResult = ByteBuf::Init(&fail_to_allocate, 10);
    ASSERT_FALSE(capacityFailureResult);

    auto arrayFailureResult = ByteBuf::InitFromArray(&fail_to_allocate, (uint8_t *)s_TestString, strlen(s_TestString));
    ASSERT_FALSE(arrayFailureResult);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteBufInitializationFailure, s_ByteBufInitializationFailure)

static const char *s_pointerString = "PointerBuffer";

static int s_ByteBufInitializationSuccess(struct aws_allocator *allocator, void *)
{

    auto goodResult = ByteBuf::InitFromArray(allocator, (uint8_t *)s_TestString, strlen(s_TestString));
    ASSERT_TRUE(goodResult);

    // Copy a full byte buf
    auto copyValueResult = ByteBuf::Init(goodResult.GetResult());
    ASSERT_TRUE(copyValueResult);
    ASSERT_TRUE(copyValueResult.GetResult().Get()->allocator == allocator);
    ASSERT_TRUE(copyValueResult.GetResult().Get()->len == goodResult.GetResult().Get()->len);
    ASSERT_TRUE(copyValueResult.GetResult().Get()->buffer != goodResult.GetResult().Get()->buffer);
    ASSERT_BIN_ARRAYS_EQUALS(
        copyValueResult.GetResult().Get()->buffer,
        copyValueResult.GetResult().Get()->len,
        goodResult.GetResult().Get()->buffer,
        goodResult.GetResult().Get()->len);

    // Copy a byte buf tracking a pointer
    struct aws_byte_buf buffer = aws_byte_buf_from_c_str(s_pointerString);
    ByteBuf pointerBuffer(&buffer);

    auto copyPointerResult = ByteBuf::Init(pointerBuffer);
    ASSERT_TRUE(copyPointerResult);
    ASSERT_TRUE(copyPointerResult.GetResult().Get() == pointerBuffer.Get());

    // Empty allocation
    auto allocResult = ByteBuf::Init(allocator, 10);
    ASSERT_TRUE(allocResult);
    ASSERT_TRUE(allocResult.GetResult().Get()->allocator == allocator);
    ASSERT_TRUE(allocResult.GetResult().Get()->len == 0);
    ASSERT_TRUE(allocResult.GetResult().Get()->buffer != nullptr);
    ASSERT_TRUE(allocResult.GetResult().Get()->capacity >= 10);

    // Copy an array
    auto arrayResult = ByteBuf::InitFromArray(allocator, (uint8_t *)s_TestString, strlen(s_TestString));
    ASSERT_TRUE(arrayResult);
    ASSERT_TRUE(arrayResult.GetResult().Get()->allocator == allocator);
    ASSERT_TRUE(arrayResult.GetResult().Get()->len == strlen(s_TestString));
    ASSERT_TRUE(arrayResult.GetResult().Get()->capacity >= strlen(s_TestString));
    ASSERT_TRUE(arrayResult.GetResult().Get()->buffer != (uint8_t *)s_TestString);
    ASSERT_BIN_ARRAYS_EQUALS(
        arrayResult.GetResult().Get()->buffer, arrayResult.GetResult().Get()->len, s_TestString, strlen(s_TestString));

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteBufInitializationSuccess, s_ByteBufInitializationSuccess)

static int s_ByteBufMove(struct aws_allocator *allocator, void *)
{
    auto valueResult = ByteBuf::InitFromArray(allocator, (uint8_t *)s_TestString, strlen(s_TestString));
    ASSERT_TRUE(valueResult);

    struct aws_byte_buf valueCopy = *valueResult.GetResult().Get();

    struct aws_byte_buf buffer = aws_byte_buf_from_c_str(s_pointerString);
    ByteBuf pointerBuffer(&buffer);
    struct aws_byte_buf *pointerBufferPtr = pointerBuffer.Get();

    ByteBuf valueMoveConstruct(valueResult.AcquireResult());
    ASSERT_TRUE(valueMoveConstruct.Get()->allocator == valueCopy.allocator);
    ASSERT_TRUE(valueMoveConstruct.Get()->buffer == valueCopy.buffer);
    ASSERT_TRUE(valueMoveConstruct.Get()->len == valueCopy.len);
    ASSERT_TRUE(valueMoveConstruct.Get()->capacity == valueCopy.capacity);
    ASSERT_TRUE(valueResult.GetResult().Get() == nullptr);

    ByteBuf pointerMoveConstruct(std::move(pointerBuffer));
    ASSERT_TRUE(pointerMoveConstruct.Get() == pointerBufferPtr);
    ASSERT_TRUE(pointerBuffer.Get() == pointerBufferPtr);

    ByteBuf valueMoveAssign(std::move(valueMoveConstruct));

    auto toBeAssignedResult = ByteBuf::InitFromArray(allocator, (uint8_t *)s_rawString, strlen(s_rawString));
    ASSERT_TRUE(toBeAssignedResult);

    struct aws_byte_buf toBeAssignedCopy = *toBeAssignedResult.GetResult().Get();

    valueMoveAssign = toBeAssignedResult.AcquireResult();
    ASSERT_TRUE(valueMoveAssign.Get()->allocator == toBeAssignedCopy.allocator);
    ASSERT_TRUE(valueMoveAssign.Get()->buffer == toBeAssignedCopy.buffer);
    ASSERT_TRUE(valueMoveAssign.Get()->len == toBeAssignedCopy.len);
    ASSERT_TRUE(valueMoveAssign.Get()->capacity == toBeAssignedCopy.capacity);
    ASSERT_TRUE(toBeAssignedResult.GetResult().Get() == nullptr);

    ByteBuf pointerMoveAssign(std::move(valueMoveAssign));

    pointerMoveAssign = std::move(pointerMoveConstruct);
    ASSERT_TRUE(pointerMoveAssign.Get() == pointerBufferPtr);
    ASSERT_TRUE(pointerMoveConstruct.Get() == pointerBufferPtr);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteBufMove, s_ByteBufMove)

static int s_ByteBufAppend(struct aws_allocator *allocator, void *)
{

    auto appendBufferResult = ByteBuf::Init(allocator, 10);
    ASSERT_TRUE(appendBufferResult);

    auto appendResult1 = appendBufferResult.GetResult().Append(ByteCursor("abc"));
    ASSERT_TRUE(appendResult1);

    auto appendResult2 = appendBufferResult.GetResult().Append(ByteCursor("def"));
    ASSERT_TRUE(appendResult2);

    auto appendResult3 = appendBufferResult.GetResult().Append(ByteCursor("ghijklmnop"));
    ASSERT_FALSE(appendResult3);

    ASSERT_TRUE(appendBufferResult.GetResult().Get()->len == 6);
    ASSERT_BIN_ARRAYS_EQUALS(
        appendBufferResult.GetResult().Get()->buffer, appendBufferResult.GetResult().Get()->len, "abddef", 6);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteBufAppend, s_ByteBufAppend)

static const char *s_appendString = "abcdefghijklmnop";

static int s_ByteBufAppendDynamicSuccess(struct aws_allocator *allocator, void *)
{

    auto appendBufferResult = ByteBuf::Init(allocator, 10);
    ASSERT_TRUE(appendBufferResult);

    auto appendResult1 = appendBufferResult.GetResult().AppendDynamic(ByteCursor("abc"));
    ASSERT_TRUE(appendResult1);

    auto appendResult2 = appendBufferResult.GetResult().AppendDynamic(ByteCursor("def"));
    ASSERT_TRUE(appendResult2);

    auto appendResult3 = appendBufferResult.GetResult().AppendDynamic(ByteCursor("ghijklmnop"));
    ASSERT_TRUE(appendResult3);

    ASSERT_TRUE(appendBufferResult.GetResult().Get()->len == strlen(s_appendString));
    ASSERT_BIN_ARRAYS_EQUALS(
        appendBufferResult.GetResult().Get()->buffer,
        appendBufferResult.GetResult().Get()->len,
        s_appendString,
        strlen(s_appendString));

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteBufAppendDynamicSuccess, s_ByteBufAppendDynamicSuccess)

static int s_ByteBufAppendDynamicFailure(struct aws_allocator *allocator, void *)
{
    struct aws_allocator allocate_once;
    AWS_ZERO_STRUCT(allocate_once);

    ASSERT_SUCCESS(aws_timebomb_allocator_init(&allocate_once, allocator, 1));

    auto appendBufferResult = ByteBuf::Init(&allocate_once, 10);
    ASSERT_TRUE(appendBufferResult);

    auto appendResult1 = appendBufferResult.GetResult().AppendDynamic(ByteCursor("abc"));
    ASSERT_TRUE(appendResult1);

    auto appendResult2 = appendBufferResult.GetResult().AppendDynamic(ByteCursor("def"));
    ASSERT_TRUE(appendResult2);

    auto appendResult3 = appendBufferResult.GetResult().AppendDynamic(ByteCursor("ghijklmnop"));
    ASSERT_FALSE(appendResult3);

    ASSERT_TRUE(appendBufferResult.GetResult().Get()->len == 6);
    ASSERT_BIN_ARRAYS_EQUALS(
        appendBufferResult.GetResult().Get()->buffer, appendBufferResult.GetResult().Get()->len, "abcdef", 6);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteBufAppendDynamicFailure, s_ByteBufAppendDynamicFailure)