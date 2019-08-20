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
    ASSERT_TRUE(emptyCursor.GetImpl()->len == 0);
    ASSERT_TRUE(emptyCursor.GetImpl()->ptr == NULL);

    // C string construction
    char cString[] = "TEST";
    ByteCursor cStringCursor(cString);
    size_t cStringLength = AWS_ARRAY_SIZE(cString) - 1;
    ASSERT_TRUE(cStringCursor.GetImpl()->len == cStringLength);
    ASSERT_TRUE(cStringCursor.GetImpl()->ptr == (uint8_t *)cString);

    // aws_byte_cursor construction
    struct aws_byte_cursor rawCursor;
    rawCursor.ptr = (uint8_t *)s_rawString;
    rawCursor.len = strlen(s_rawString);

    ByteCursor rawCopyCursor(rawCursor);
    ASSERT_TRUE(rawCopyCursor.GetImpl()->len == rawCursor.len);
    ASSERT_TRUE(rawCopyCursor.GetImpl()->ptr == rawCursor.ptr);
    ASSERT_TRUE(rawCopyCursor.GetImpl() != &rawCursor);

    // Copy construction
    ByteCursor copyTargetCursor(rawCopyCursor);
    ASSERT_TRUE(copyTargetCursor.GetImpl()->len == rawCursor.len);
    ASSERT_TRUE(copyTargetCursor.GetImpl()->ptr == rawCursor.ptr);
    ASSERT_TRUE(rawCopyCursor.GetImpl()->len == rawCursor.len);
    ASSERT_TRUE(rawCopyCursor.GetImpl()->ptr == rawCursor.ptr);
    ASSERT_TRUE(copyTargetCursor.GetImpl() != rawCopyCursor.GetImpl());

    // Trying to move a cursor should just copy it
    ByteCursor moveTargetCursor(std::move(rawCopyCursor));
    ASSERT_TRUE(moveTargetCursor.GetImpl()->len == rawCursor.len);
    ASSERT_TRUE(moveTargetCursor.GetImpl()->ptr == rawCursor.ptr);
    ASSERT_TRUE(rawCopyCursor.GetImpl()->len == rawCursor.len);
    ASSERT_TRUE(rawCopyCursor.GetImpl()->ptr == rawCursor.ptr);
    ASSERT_TRUE(moveTargetCursor.GetImpl() != rawCopyCursor.GetImpl());

    // Array construction
    ByteCursor arrayCursor((uint8_t *)s_rawString, 3);
    ASSERT_TRUE(arrayCursor.GetImpl()->len == 3);
    ASSERT_TRUE(arrayCursor.GetImpl()->ptr == (uint8_t *)s_rawString);

    // String construction
    String helloWorldString("HelloWorld");
    ByteCursor stringCursor(helloWorldString);
    ASSERT_TRUE(stringCursor.GetImpl()->len == helloWorldString.size());
    ASSERT_TRUE(stringCursor.GetImpl()->ptr == (uint8_t *)helloWorldString.c_str());

    // Byte buf construction
    struct aws_byte_buf buffer = aws_byte_buf_from_c_str(s_rawString);
    ByteCursor bufferCursor(buffer);
    ASSERT_TRUE(bufferCursor.GetImpl()->len == buffer.len);
    ASSERT_TRUE(bufferCursor.GetImpl()->ptr == buffer.buffer);

    ByteCursor bufferPtrCursor(buffer);
    ASSERT_TRUE(bufferPtrCursor.GetImpl()->len == buffer.len);
    ASSERT_TRUE(bufferPtrCursor.GetImpl()->ptr == buffer.buffer);

    // Assignment
    ByteCursor assignTargetCursor;
    assignTargetCursor = rawCopyCursor;
    ASSERT_TRUE(assignTargetCursor.GetImpl()->len == rawCursor.len);
    ASSERT_TRUE(assignTargetCursor.GetImpl()->ptr == rawCursor.ptr);
    ASSERT_TRUE(assignTargetCursor.GetImpl() != rawCopyCursor.GetImpl());

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteCursorConstruction, s_ByteCursorConstruction)

static const char *s_TestString = "ThisIsATest";
static const char *s_pointerString = "PointerBuffer";

static int s_ByteBufInitializationSuccess(struct aws_allocator *allocator, void *)
{
    ByteBuf defaultBuffer;
    ASSERT_TRUE(defaultBuffer.GetImpl()->len == 0);
    ASSERT_TRUE(defaultBuffer.GetImpl()->capacity == 0);
    ASSERT_TRUE(defaultBuffer.GetImpl()->buffer == nullptr);
    ASSERT_TRUE(defaultBuffer.GetImpl()->allocator == nullptr);

    struct aws_byte_buf buffer = aws_byte_buf_from_c_str(s_TestString);
    ByteBuf pointerBuffer(&buffer);
    ASSERT_TRUE(pointerBuffer.GetImpl() == &buffer);

    ByteBuf arrayBuffer((uint8_t *)s_TestString, 5, 3);
    ASSERT_TRUE(arrayBuffer.GetImpl()->len == 3);
    ASSERT_TRUE(arrayBuffer.GetImpl()->capacity == 5);
    ASSERT_TRUE(arrayBuffer.GetImpl()->buffer == (uint8_t *)s_TestString);
    ASSERT_TRUE(arrayBuffer.GetImpl()->allocator == nullptr);

    auto goodResult = ByteBuf(allocator, strlen(s_TestString));
    ASSERT_TRUE(goodResult.Append(ByteCursor((uint8_t *)s_TestString, strlen(s_TestString))));

    // Copy a full byte buf
    auto copyValueResult = ByteBuf(goodResult);
    ASSERT_TRUE(copyValueResult);
    ASSERT_TRUE(copyValueResult.GetImpl()->allocator == allocator);
    ASSERT_TRUE(copyValueResult.GetImpl()->len == goodResult.GetImpl()->len);
    ASSERT_TRUE(copyValueResult.GetImpl()->buffer != goodResult.GetImpl()->buffer);
    ASSERT_BIN_ARRAYS_EQUALS(
        copyValueResult.GetImpl()->buffer,
        copyValueResult.GetImpl()->len,
        goodResult.GetImpl()->buffer,
        goodResult.GetImpl()->len);

    // Copy a byte buf tracking a pointer
    auto copyPointerResult = ByteBuf(pointerBuffer);
    ASSERT_TRUE(copyPointerResult);
    ASSERT_TRUE(copyPointerResult.GetImpl() == pointerBuffer.GetImpl());

    // Empty allocation
    auto allocResult = ByteBuf(allocator, 10);
    ASSERT_TRUE(allocResult);
    ASSERT_TRUE(allocResult.GetImpl()->allocator == allocator);
    ASSERT_TRUE(allocResult.GetImpl()->len == 0);
    ASSERT_TRUE(allocResult.GetImpl()->buffer != nullptr);
    ASSERT_TRUE(allocResult.GetImpl()->capacity >= 10);

    // Copy an array
    auto arrayResult = ByteBuf(allocator, strlen(s_TestString));
    ASSERT_TRUE(arrayResult.Append(ByteCursor((uint8_t *)s_TestString, strlen(s_TestString))));
    ASSERT_TRUE(arrayResult.GetImpl()->allocator == allocator);
    ASSERT_TRUE(arrayResult.GetImpl()->len == strlen(s_TestString));
    ASSERT_TRUE(arrayResult.GetImpl()->capacity >= strlen(s_TestString));
    ASSERT_TRUE(arrayResult.GetImpl()->buffer != (uint8_t *)s_TestString);
    ASSERT_BIN_ARRAYS_EQUALS(
        arrayResult.GetImpl()->buffer,
        arrayResult.GetImpl()->len,
        s_TestString,
        strlen(s_TestString));

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteBufInitializationSuccess, s_ByteBufInitializationSuccess)

static int s_ByteBufInitializationFailure(struct aws_allocator *allocator, void *)
{
    struct aws_allocator fail_to_allocate;
    AWS_ZERO_STRUCT(fail_to_allocate);

    ASSERT_SUCCESS(aws_timebomb_allocator_init(&fail_to_allocate, allocator, 0));

    ByteBuf goodResult(allocator, strlen(s_TestString));
    ASSERT_TRUE(goodResult.Append(ByteCursor((uint8_t *)s_TestString, strlen(s_TestString))));

    goodResult.GetImpl()->allocator = &fail_to_allocate;

    auto copyFailureResult = ByteBuf(goodResult);
    ASSERT_FALSE(copyFailureResult);
    ASSERT_TRUE(copyFailureResult.GetInitializationErrorCode() == AWS_ERROR_OOM);

    goodResult.GetImpl()->allocator = allocator;

    auto capacityFailureResult = ByteBuf(&fail_to_allocate, 10);
    ASSERT_FALSE(capacityFailureResult);

    auto arrayFailureResult = ByteBuf(&fail_to_allocate, strlen(s_TestString));
    ASSERT_FALSE(arrayFailureResult.Append(ByteCursor((uint8_t *)s_TestString, strlen(s_TestString))));

    aws_timebomb_allocator_clean_up(&fail_to_allocate);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteBufInitializationFailure, s_ByteBufInitializationFailure)

static int s_ByteBufMove(struct aws_allocator *allocator, void *)
{
    auto valueResult = ByteBuf(allocator, strlen(s_TestString));
    ASSERT_TRUE(valueResult.Append(ByteCursor((uint8_t *)s_TestString, strlen(s_TestString))));

    struct aws_byte_buf valueCopy = *valueResult.GetImpl();

    struct aws_byte_buf buffer = aws_byte_buf_from_c_str(s_pointerString);
    ByteBuf pointerBuffer = ByteBuf(&buffer);
    struct aws_byte_buf *pointerBufferPtr = pointerBuffer.GetImpl();

    ByteBuf valueMoveConstruct(std::move(valueResult));
    ASSERT_TRUE(valueMoveConstruct.GetImpl()->allocator == valueCopy.allocator);
    ASSERT_TRUE(valueMoveConstruct.GetImpl()->buffer == valueCopy.buffer);
    ASSERT_TRUE(valueMoveConstruct.GetImpl()->len == valueCopy.len);
    ASSERT_TRUE(valueMoveConstruct.GetImpl()->capacity == valueCopy.capacity);
    ASSERT_TRUE(valueResult.GetImpl()->len == 0);
    ASSERT_TRUE(valueResult.GetImpl()->buffer == nullptr);

    ByteBuf pointerMoveConstruct(std::move(pointerBuffer));
    ASSERT_TRUE(pointerMoveConstruct.GetImpl() == pointerBufferPtr);
    ASSERT_TRUE(pointerBuffer.GetImpl() == pointerBufferPtr);

    ByteBuf valueMoveAssign(std::move(valueMoveConstruct));

    auto toBeAssignedResult = ByteBuf(allocator, strlen(s_rawString));
    ASSERT_TRUE(toBeAssignedResult.Append(ByteCursor((uint8_t *)s_rawString, strlen(s_rawString))));

    struct aws_byte_buf toBeAssignedCopy = *toBeAssignedResult.GetImpl();

    valueMoveAssign = std::move(toBeAssignedResult);
    ASSERT_TRUE(valueMoveAssign.GetImpl()->allocator == toBeAssignedCopy.allocator);
    ASSERT_TRUE(valueMoveAssign.GetImpl()->buffer == toBeAssignedCopy.buffer);
    ASSERT_TRUE(valueMoveAssign.GetImpl()->len == toBeAssignedCopy.len);
    ASSERT_TRUE(valueMoveAssign.GetImpl()->capacity == toBeAssignedCopy.capacity);
    ASSERT_TRUE(toBeAssignedResult.GetImpl()->len == 0);
    ASSERT_TRUE(toBeAssignedResult.GetImpl()->buffer == 0);

    ByteBuf pointerMoveAssign(std::move(valueMoveAssign));

    pointerMoveAssign = std::move(pointerMoveConstruct);
    ASSERT_TRUE(pointerMoveAssign.GetImpl() == pointerBufferPtr);
    ASSERT_TRUE(pointerMoveConstruct.GetImpl() == pointerBufferPtr);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteBufMove, s_ByteBufMove)

static int s_ByteBufAppend(struct aws_allocator *allocator, void *)
{

    auto appendBufferResult = ByteBuf(allocator, 10);
    ASSERT_TRUE(appendBufferResult);

    auto appendResult1 = appendBufferResult.Append(ByteCursor("abc"));
    ASSERT_TRUE(appendResult1);

    auto appendResult2 = appendBufferResult.Append(ByteCursor("def"));
    ASSERT_TRUE(appendResult2);

    auto appendResult3 = appendBufferResult.Append(ByteCursor("ghijklmnop"));
    ASSERT_FALSE(appendResult3);

    ASSERT_TRUE(appendBufferResult.GetImpl()->len == 6);
    ASSERT_BIN_ARRAYS_EQUALS(
        appendBufferResult.GetImpl()->buffer, appendBufferResult.GetImpl()->len, "abcdef", 6);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteBufAppend, s_ByteBufAppend)

static const char *s_appendString = "abcdefghijklmnop";

static int s_ByteBufAppendDynamicSuccess(struct aws_allocator *allocator, void *)
{

    auto appendBufferResult = ByteBuf(allocator, 10);
    ASSERT_TRUE(appendBufferResult);

    auto appendResult1 = appendBufferResult.AppendDynamic(ByteCursor("abc"));
    ASSERT_TRUE(appendResult1);

    auto appendResult2 = appendBufferResult.AppendDynamic(ByteCursor("def"));
    ASSERT_TRUE(appendResult2);

    auto appendResult3 = appendBufferResult.AppendDynamic(ByteCursor("ghijklmnop"));
    ASSERT_TRUE(appendResult3);

    ASSERT_TRUE(appendBufferResult.GetImpl()->len == strlen(s_appendString));
    ASSERT_BIN_ARRAYS_EQUALS(
        appendBufferResult.GetImpl()->buffer,
        appendBufferResult.GetImpl()->len,
        s_appendString,
        strlen(s_appendString));

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteBufAppendDynamicSuccess, s_ByteBufAppendDynamicSuccess)

static int s_ByteBufAppendDynamicFailure(struct aws_allocator *allocator, void *)
{
    struct aws_allocator allocate_once;
    AWS_ZERO_STRUCT(allocate_once);

    {
        ASSERT_SUCCESS(aws_timebomb_allocator_init(&allocate_once, allocator, 1));

        auto appendBufferResult = ByteBuf(&allocate_once, 10);
        ASSERT_TRUE(appendBufferResult);

        auto appendResult1 = appendBufferResult.AppendDynamic(ByteCursor("abc"));
        ASSERT_TRUE(appendResult1);

        auto appendResult2 = appendBufferResult.AppendDynamic(ByteCursor("def"));
        ASSERT_TRUE(appendResult2);

        auto appendResult3 = appendBufferResult.AppendDynamic(ByteCursor("ghijklmnop"));
        ASSERT_FALSE(appendResult3);

        ASSERT_TRUE(appendBufferResult.GetImpl()->len == 6);
        ASSERT_BIN_ARRAYS_EQUALS(
            appendBufferResult.GetImpl()->buffer,
            appendBufferResult.GetImpl()->len,
            "abcdef",
            6);
    }

    aws_timebomb_allocator_clean_up(&allocate_once);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteBufAppendDynamicFailure, s_ByteBufAppendDynamicFailure)