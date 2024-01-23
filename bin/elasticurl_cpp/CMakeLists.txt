project(elasticurl_cpp CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_PREFIX_PATH}/lib/cmake")

file(GLOB ELASTICURL_CPP_SRC
        "*.cpp"
        )

set(ELASTICURL_CPP_PROJECT_NAME elasticurl_cpp)
add_executable(${ELASTICURL_CPP_PROJECT_NAME} ${ELASTICURL_CPP_SRC})

aws_add_sanitizers(${ELASTICURL_CPP_PROJECT_NAME})

set_target_properties(${ELASTICURL_CPP_PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(${ELASTICURL_CPP_PROJECT_NAME} PROPERTIES CXX_STANDARD ${CMAKE_CXX_STANDARD})

#set warnings and runtime library
if (MSVC)
    if(AWS_STATIC_MSVC_RUNTIME_LIBRARY OR STATIC_CRT)
        target_compile_options(${ELASTICURL_CPP_PROJECT_NAME} PRIVATE "/MT$<$<CONFIG:Debug>:d>")
    else()
        target_compile_options(${ELASTICURL_CPP_PROJECT_NAME} PRIVATE "/MD$<$<CONFIG:Debug>:d>")
    endif()
    target_compile_options(${ELASTICURL_CPP_PROJECT_NAME} PRIVATE /W4 /WX)
else ()
    target_compile_options(${ELASTICURL_CPP_PROJECT_NAME} PRIVATE -Wall -Wno-long-long -pedantic -Werror)
endif ()


target_compile_definitions(${ELASTICURL_CPP_PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:DEBUG_BUILD>)

target_include_directories(${ELASTICURL_CPP_PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(${ELASTICURL_CPP_PROJECT_NAME} PRIVATE aws-crt-cpp)

if (BUILD_SHARED_LIBS AND NOT WIN32)
    message(INFO " elasticurl will be built with shared libs, but you may need to set LD_LIBRARY_PATH=${CMAKE_INSTALL_PREFIX}/lib to run the application")
endif()

install(TARGETS ${ELASTICURL_CPP_PROJECT_NAME}
        EXPORT ${ELASTICURL_CPP_PROJECT_NAME}-targets
        COMPONENT Runtime
        RUNTIME
        DESTINATION bin
        COMPONENT Runtime)
