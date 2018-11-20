cmake_minimum_required(VERSION 3.1)
project(aws-crt-cpp CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_INSTALL_PREFIX}/lib/cmake")

if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 11)
endif()

file(GLOB AWS_CRT_HEADERS
        "include/aws/crt/*.h"
)

file(GLOB AWS_CRT_IO_HEADERS
        "include/aws/crt/io/*.h"
)

file(GLOB AWS_CRT_MQTT_HEADERS
        "include/aws/crt/mqtt/*.h"
)

file(GLOB AWS_CRT_CPP_HEADERS
        ${AWS_CRT_HEADERS}
	    ${AWS_CRT_IO_HEADERS}
        ${AWS_CRT_MQTT_HEADERS}
)

file(GLOB AWS_CRT_SRC 
       "source/*.cpp"
)

file (GLOB AWS_CRT_IO_SRC
       "source/io/*.cpp"
)

file (GLOB AWS_CRT_MQTT_SRC
       "source/mqtt/*.cpp"
)

file(GLOB AWS_CRT_CPP_SRC
        ${AWS_CRT_SRC}
        ${AWS_CRT_IO_SRC}
        ${AWS_CRT_MQTT_SRC}        
)

if (WIN32)
    if (MSVC)
        source_group("Header Files\\aws\\crt" FILES ${AWS_CRT_HEADERS})
        source_group("Header Files\\aws\\crt\\io" FILES ${AWS_CRT_IO_HEADERS})
        source_group("Header Files\\aws\\crt\\mqtt" FILES ${AWS_CRT_MQTT_HEADERS})
        source_group("Source Files" FILES ${AWS_CRT_SRC})
        source_group("Source Files\\io" FILES ${AWS_CRT_IO_SRC})
        source_group("Source Files\\mqtt" FILES ${AWS_CRT_MQTT_SRC})
    endif ()
endif()

add_library(${CMAKE_PROJECT_NAME} ${AWS_CRT_CPP_SRC})

if (BUILD_SHARED_LIBS AND WIN32)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC -DUSE_IMPORT_EXPORT)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE -DAWS_CRT_CPP_EXPORTS)
endif()

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

set(CMAKE_C_FLAGS_DEBUGOPT "")

#set warnings
if (MSVC)
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /W4 /WX)
else ()
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall -Wno-long-long -pedantic -Werror)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "" OR CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE "-DDEBUG_BUILD")
endif ()

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

find_package(aws-c-mqtt REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} AWS::aws-c-mqtt)

install(FILES ${AWS_CRT_HEADERS} DESTINATION "include/aws/crt")
install(FILES ${AWS_CRT_IO_HEADERS} DESTINATION "include/aws/crt/io")
install(FILES ${AWS_CRT_MQTT_HEADERS} DESTINATION "include/aws/crt/mqtt")

install(
        TARGETS ${CMAKE_PROJECT_NAME}
        EXPORT ${CMAKE_PROJECT_NAME}-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
        LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
)

install(EXPORT "${CMAKE_PROJECT_NAME}-targets"
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/${CMAKE_PROJECT_NAME}/cmake/"
        NAMESPACE AWS::)

configure_file("cmake/${CMAKE_PROJECT_NAME}-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}-config.cmake"
        @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}-config.cmake"
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/${CMAKE_PROJECT_NAME}/cmake/")

enable_testing()
add_subdirectory(tests)
add_subdirectory(samples/mqtt_pub_sub)
