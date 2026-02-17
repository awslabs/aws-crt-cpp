project(stream_comparison CXX)

set(STREAM_COMPARISON_PROJECT_NAME stream_comparison)
add_executable(${STREAM_COMPARISON_PROJECT_NAME} main.cpp)

aws_add_sanitizers(${STREAM_COMPARISON_PROJECT_NAME})

set_target_properties(${STREAM_COMPARISON_PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(${STREAM_COMPARISON_PROJECT_NAME} PROPERTIES CXX_STANDARD ${CMAKE_CXX_STANDARD})

if (MSVC)
    if(AWS_STATIC_MSVC_RUNTIME_LIBRARY OR STATIC_CRT)
        target_compile_options(${STREAM_COMPARISON_PROJECT_NAME} PRIVATE "/MT$<$<CONFIG:Debug>:d>")
    else()
        target_compile_options(${STREAM_COMPARISON_PROJECT_NAME} PRIVATE "/MD$<$<CONFIG:Debug>:d>")
    endif()
    target_compile_options(${STREAM_COMPARISON_PROJECT_NAME} PRIVATE /W4 /WX)
else ()
    target_compile_options(${STREAM_COMPARISON_PROJECT_NAME} PRIVATE -Wall -Wno-long-long -pedantic -Werror)
endif ()

target_compile_definitions(${STREAM_COMPARISON_PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:DEBUG_BUILD>)

target_include_directories(${STREAM_COMPARISON_PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(${STREAM_COMPARISON_PROJECT_NAME} PRIVATE aws-crt-cpp)
