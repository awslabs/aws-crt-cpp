project(mqtt5_app CXX)

file(GLOB MQTT5_APP_SRC
        "*.cpp"
        )

set(MQTT5_APP_PROJECT_NAME mqtt5_app)
add_executable(${MQTT5_APP_PROJECT_NAME} ${MQTT5_APP_SRC})

aws_add_sanitizers(${MQTT5_APP_PROJECT_NAME})

set_target_properties(${MQTT5_APP_PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(${MQTT5_APP_PROJECT_NAME} PROPERTIES CXX_STANDARD ${CMAKE_CXX_STANDARD})

#set warnings and runtime library
if (MSVC)
    if(AWS_STATIC_MSVC_RUNTIME_LIBRARY OR STATIC_CRT)
        target_compile_options(${MQTT5_APP_PROJECT_NAME} PRIVATE "/MT$<$<CONFIG:Debug>:d>")
    else()
        target_compile_options(${MQTT5_APP_PROJECT_NAME} PRIVATE "/MD$<$<CONFIG:Debug>:d>")
    endif()
    target_compile_options(${MQTT5_APP_PROJECT_NAME} PRIVATE /W4 /WX)
else ()
    target_compile_options(${MQTT5_APP_PROJECT_NAME} PRIVATE -Wall -Wno-long-long -pedantic -Werror)
endif ()


target_compile_definitions(${MQTT5_APP_PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:DEBUG_BUILD>)

target_include_directories(${MQTT5_APP_PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(${MQTT5_APP_PROJECT_NAME} PRIVATE aws-crt-cpp)

if (BUILD_SHARED_LIBS AND NOT WIN32)
    message(INFO " mqtt5 app will be built with shared libs, but you may need to set LD_LIBRARY_PATH=${CMAKE_INSTALL_PREFIX}/lib to run the application")
endif()

install(TARGETS ${MQTT5_APP_PROJECT_NAME}
        EXPORT ${MQTT5_APP_PROJECT_NAME}-targets
        COMPONENT Runtime
        RUNTIME
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT Runtime)
