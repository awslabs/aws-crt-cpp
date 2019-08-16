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

    ByteCursor rawCopyRefCursor = ByteCursor::Wrap(&rawCursor);
    ASSERT_TRUE(rawCopyRefCursor.GetImpl() == &rawCursor);

    // Copy construction
    ByteCursor copyTargetCursor(rawCopyCursor);
    ASSERT_TRUE(copyTargetCursor.GetImpl()->len == rawCursor.len);
    ASSERT_TRUE(copyTargetCursor.GetImpl()->ptr == rawCursor.ptr);
    ASSERT_TRUE(rawCopyCursor.GetImpl()->len == rawCursor.len);
    ASSERT_TRUE(rawCopyCursor.GetImpl()->ptr == rawCursor.ptr);
    ASSERT_TRUE(copyTargetCursor.GetImpl() != rawCopyCursor.GetImpl());

    ByteCursor copyTargetRefCursor(rawCopyRefCursor);
    ASSERT_TRUE(
        copyTargetRefCursor.GetImpl() == rawCopyRefCursor.GetImpl() && copyTargetRefCursor.GetImpl() == &rawCursor);

    // Trying to move a cursor should just copy it
    ByteCursor moveTargetCursor(std::move(rawCopyCursor));
    ASSERT_TRUE(moveTargetCursor.GetImpl()->len == rawCursor.len);
    ASSERT_TRUE(moveTargetCursor.GetImpl()->ptr == rawCursor.ptr);
    ASSERT_TRUE(rawCopyCursor.GetImpl()->len == rawCursor.len);
    ASSERT_TRUE(rawCopyCursor.GetImpl()->ptr == rawCursor.ptr);
    ASSERT_TRUE(moveTargetCursor.GetImpl() != rawCopyCursor.GetImpl());

    ByteCursor moveTargetRefCursor(std::move(rawCopyRefCursor));
    ASSERT_TRUE(
        moveTargetRefCursor.GetImpl() == rawCopyRefCursor.GetImpl() && moveTargetRefCursor.GetImpl() == &rawCursor);

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

    ByteCursor assignTargetRefCursor;
    assignTargetRefCursor = rawCopyRefCursor;
    ASSERT_TRUE(
        assignTargetRefCursor.GetImpl() == rawCopyRefCursor.GetImpl() && assignTargetRefCursor.GetImpl() == &rawCursor);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteCursorConstruction, s_ByteCursorConstruction)

static const char *s_TestString = "ThisIsATest";

static int s_ByteBufConstruction(struct aws_allocator *allocator, void *)
{
    ByteBuf defaultBuffer;
    ASSERT_TRUE(defaultBuffer.GetImpl()->len == 0);
    ASSERT_TRUE(defaultBuffer.GetImpl()->capacity == 0);
    ASSERT_TRUE(defaultBuffer.GetImpl()->buffer == nullptr);
    ASSERT_TRUE(defaultBuffer.GetImpl()->allocator == nullptr);

    struct aws_byte_buf buffer = aws_byte_buf_from_c_str(s_TestString);
    ByteBuf pointerBuffer = ByteBuf::Wrap(&buffer);
    ASSERT_TRUE(pointerBuffer.GetImpl() == &buffer);

    ByteBuf arrayBuffer((uint8_t *)s_TestString, 5, 3);
    ASSERT_TRUE(arrayBuffer.GetImpl()->len == 3);
    ASSERT_TRUE(arrayBuffer.GetImpl()->capacity == 5);
    ASSERT_TRUE(arrayBuffer.GetImpl()->buffer == (uint8_t *)s_TestString);
    ASSERT_TRUE(arrayBuffer.GetImpl()->allocator == nullptr);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteBufConstruction, s_ByteBufConstruction)

static int s_ByteBufInitializationFailure(struct aws_allocator *allocator, void *)
{

    struct aws_allocator fail_to_allocate;
    AWS_ZERO_STRUCT(fail_to_allocate);

    ASSERT_SUCCESS(aws_timebomb_allocator_init(&fail_to_allocate, allocator, 0));

    auto goodResult = ByteBuf::Init(allocator, strlen(s_TestString));
    ASSERT_TRUE(goodResult.GetResult().Append(ByteCursor((uint8_t *)s_TestString, strlen(s_TestString))));

    goodResult.GetResult().GetImpl()->allocator = &fail_to_allocate;

    auto copyFailureResult = ByteBuf::Init(goodResult.GetResult());
    ASSERT_FALSE(copyFailureResult);

    goodResult.GetResult().GetImpl()->allocator = allocator;

    auto capacityFailureResult = ByteBuf::Init(&fail_to_allocate, 10);
    ASSERT_FALSE(capacityFailureResult);

    auto arrayFailureResult = ByteBuf::Init(&fail_to_allocate, strlen(s_TestString));
    ASSERT_FALSE(arrayFailureResult.GetResult().Append(ByteCursor((uint8_t *)s_TestString, strlen(s_TestString))));

    aws_timebomb_allocator_clean_up(&fail_to_allocate);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteBufInitializationFailure, s_ByteBufInitializationFailure)

static const char *s_pointerString = "PointerBuffer";

static int s_ByteBufInitializationSuccess(struct aws_allocator *allocator, void *)
{

    auto goodResult = ByteBuf::Init(allocator, strlen(s_TestString));
    ASSERT_TRUE(goodResult.GetResult().Append(ByteCursor((uint8_t *)s_TestString, strlen(s_TestString))));

    // Copy a full byte buf
    auto copyValueResult = ByteBuf::Init(goodResult.GetResult());
    ASSERT_TRUE(copyValueResult);
    ASSERT_TRUE(copyValueResult.GetResult().GetImpl()->allocator == allocator);
    ASSERT_TRUE(copyValueResult.GetResult().GetImpl()->len == goodResult.GetResult().GetImpl()->len);
    ASSERT_TRUE(copyValueResult.GetResult().GetImpl()->buffer != goodResult.GetResult().GetImpl()->buffer);
    ASSERT_BIN_ARRAYS_EQUALS(
        copyValueResult.GetResult().GetImpl()->buffer,
        copyValueResult.GetResult().GetImpl()->len,
        goodResult.GetResult().GetImpl()->buffer,
        goodResult.GetResult().GetImpl()->len);

    // Copy a byte buf tracking a pointer
    struct aws_byte_buf buffer = aws_byte_buf_from_c_str(s_pointerString);
    ByteBuf pointerBuffer = ByteBuf::Wrap(&buffer);

    auto copyPointerResult = ByteBuf::Init(pointerBuffer);
    ASSERT_TRUE(copyPointerResult);
    ASSERT_TRUE(copyPointerResult.GetResult().GetImpl() == pointerBuffer.GetImpl());

    // Empty allocation
    auto allocResult = ByteBuf::Init(allocator, 10);
    ASSERT_TRUE(allocResult);
    ASSERT_TRUE(allocResult.GetResult().GetImpl()->allocator == allocator);
    ASSERT_TRUE(allocResult.GetResult().GetImpl()->len == 0);
    ASSERT_TRUE(allocResult.GetResult().GetImpl()->buffer != nullptr);
    ASSERT_TRUE(allocResult.GetResult().GetImpl()->capacity >= 10);

    // Copy an array
    auto arrayResult = ByteBuf::Init(allocator, strlen(s_TestString));
    ASSERT_TRUE(arrayResult.GetResult().Append(ByteCursor((uint8_t *)s_TestString, strlen(s_TestString))));
    ASSERT_TRUE(arrayResult.GetResult().GetImpl()->allocator == allocator);
    ASSERT_TRUE(arrayResult.GetResult().GetImpl()->len == strlen(s_TestString));
    ASSERT_TRUE(arrayResult.GetResult().GetImpl()->capacity >= strlen(s_TestString));
    ASSERT_TRUE(arrayResult.GetResult().GetImpl()->buffer != (uint8_t *)s_TestString);
    ASSERT_BIN_ARRAYS_EQUALS(
        arrayResult.GetResult().GetImpl()->buffer,
        arrayResult.GetResult().GetImpl()->len,
        s_TestString,
        strlen(s_TestString));

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteBufInitializationSuccess, s_ByteBufInitializationSuccess)

static int s_ByteBufMove(struct aws_allocator *allocator, void *)
{
    auto valueResult = ByteBuf::Init(allocator, strlen(s_TestString));
    ASSERT_TRUE(valueResult.GetResult().Append(ByteCursor((uint8_t *)s_TestString, strlen(s_TestString))));

    struct aws_byte_buf valueCopy = *valueResult.GetResult().GetImpl();

    struct aws_byte_buf buffer = aws_byte_buf_from_c_str(s_pointerString);
    ByteBuf pointerBuffer = ByteBuf::Wrap(&buffer);
    struct aws_byte_buf *pointerBufferPtr = pointerBuffer.GetImpl();

    ByteBuf valueMoveConstruct(std::move(valueResult.GetResult()));
    ASSERT_TRUE(valueMoveConstruct.GetImpl()->allocator == valueCopy.allocator);
    ASSERT_TRUE(valueMoveConstruct.GetImpl()->buffer == valueCopy.buffer);
    ASSERT_TRUE(valueMoveConstruct.GetImpl()->len == valueCopy.len);
    ASSERT_TRUE(valueMoveConstruct.GetImpl()->capacity == valueCopy.capacity);
    ASSERT_TRUE(valueResult.GetResult().GetImpl()->len == 0);
    ASSERT_TRUE(valueResult.GetResult().GetImpl()->buffer == nullptr);

    ByteBuf pointerMoveConstruct(std::move(pointerBuffer));
    ASSERT_TRUE(pointerMoveConstruct.GetImpl() == pointerBufferPtr);
    ASSERT_TRUE(pointerBuffer.GetImpl() == pointerBufferPtr);

    ByteBuf valueMoveAssign(std::move(valueMoveConstruct));

    auto toBeAssignedResult = ByteBuf::Init(allocator, strlen(s_rawString));
    ASSERT_TRUE(toBeAssignedResult.GetResult().Append(ByteCursor((uint8_t *)s_rawString, strlen(s_rawString))));

    struct aws_byte_buf toBeAssignedCopy = *toBeAssignedResult.GetResult().GetImpl();

    valueMoveAssign = std::move(toBeAssignedResult.GetResult());
    ASSERT_TRUE(valueMoveAssign.GetImpl()->allocator == toBeAssignedCopy.allocator);
    ASSERT_TRUE(valueMoveAssign.GetImpl()->buffer == toBeAssignedCopy.buffer);
    ASSERT_TRUE(valueMoveAssign.GetImpl()->len == toBeAssignedCopy.len);
    ASSERT_TRUE(valueMoveAssign.GetImpl()->capacity == toBeAssignedCopy.capacity);
    ASSERT_TRUE(toBeAssignedResult.GetResult().GetImpl()->len == 0);
    ASSERT_TRUE(toBeAssignedResult.GetResult().GetImpl()->buffer == 0);

    ByteBuf pointerMoveAssign(std::move(valueMoveAssign));

    pointerMoveAssign = std::move(pointerMoveConstruct);
    ASSERT_TRUE(pointerMoveAssign.GetImpl() == pointerBufferPtr);
    ASSERT_TRUE(pointerMoveConstruct.GetImpl() == pointerBufferPtr);

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

    ASSERT_TRUE(appendBufferResult.GetResult().GetImpl()->len == 6);
    ASSERT_BIN_ARRAYS_EQUALS(
        appendBufferResult.GetResult().GetImpl()->buffer, appendBufferResult.GetResult().GetImpl()->len, "abcdef", 6);

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

    ASSERT_TRUE(appendBufferResult.GetResult().GetImpl()->len == strlen(s_appendString));
    ASSERT_BIN_ARRAYS_EQUALS(
        appendBufferResult.GetResult().GetImpl()->buffer,
        appendBufferResult.GetResult().GetImpl()->len,
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

        auto appendBufferResult = ByteBuf::Init(&allocate_once, 10);
        ASSERT_TRUE(appendBufferResult);

        auto appendResult1 = appendBufferResult.GetResult().AppendDynamic(ByteCursor("abc"));
        ASSERT_TRUE(appendResult1);

        auto appendResult2 = appendBufferResult.GetResult().AppendDynamic(ByteCursor("def"));
        ASSERT_TRUE(appendResult2);

        auto appendResult3 = appendBufferResult.GetResult().AppendDynamic(ByteCursor("ghijklmnop"));
        ASSERT_FALSE(appendResult3);

        ASSERT_TRUE(appendBufferResult.GetResult().GetImpl()->len == 6);
        ASSERT_BIN_ARRAYS_EQUALS(
            appendBufferResult.GetResult().GetImpl()->buffer,
            appendBufferResult.GetResult().GetImpl()->len,
            "abcdef",
            6);
    }

    aws_timebomb_allocator_clean_up(&allocate_once);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ByteBufAppendDynamicFailure, s_ByteBufAppendDynamicFailure)