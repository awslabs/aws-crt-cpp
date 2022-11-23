project(mqtt5_canary CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_PREFIX_PATH}/lib/cmake")

file(GLOB MQTT5_CANARY_SRC
        "*.cpp"
        )

set(MQTT5_CANARY_PROJECT_NAME mqtt5_canary)
add_executable(${MQTT5_CANARY_PROJECT_NAME} ${MQTT5_CANARY_SRC})

set(CMAKE_CXX_STANDARD 20)
set_target_properties(${MQTT5_CANARY_PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(${MQTT5_CANARY_PROJECT_NAME} PROPERTIES CXX_STANDARD ${CMAKE_CXX_STANDARD})


#set warnings and runtime library
if (MSVC)
    if(STATIC_CRT)
        target_compile_options(${PROJECT_NAME} PRIVATE "/MT$<$<CONFIG:Debug>:d>")
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE "/MD$<$<CONFIG:Debug>:d>")
    endif()
    target_compile_options(${MQTT5_CANARY_PROJECT_NAME} PRIVATE /W4 /WX)
else ()
    target_compile_options(${MQTT5_CANARY_PROJECT_NAME} PRIVATE -Wall -Wno-long-long -pedantic -Werror)
endif ()


if (CMAKE_BUILD_TYPE STREQUAL "" OR CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_definitions(${MQTT5_CANARY_PROJECT_NAME} PRIVATE "-DDEBUG_BUILD")
endif ()

target_include_directories(${MQTT5_CANARY_PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(${MQTT5_CANARY_PROJECT_NAME} aws-crt-cpp)

if (BUILD_SHARED_LIBS AND NOT WIN32)
    message(INFO " mqtt5 will be built with shared libs, but you may need to set LD_LIBRARY_PATH=${CMAKE_INSTALL_PREFIX}/lib to run the application")
endif()

install(TARGETS ${MQTT5_CANARY_PROJECT_NAME}
        EXPORT ${MQTT5_CANARY_PROJECT_NAME}-targets
        COMPONENT Runtime
        RUNTIME
        DESTINATION bin
        COMPONENT Runtime)
