project(aws-crt-cpp-canary CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_INSTALL_PREFIX}/lib/cmake")

file(GLOB CANARY_SRC
       "*.cpp"
)

set(CANARY_PROJECT_NAME aws-crt-cpp-canary)
add_executable(${CANARY_PROJECT_NAME} ${CANARY_SRC})
set_target_properties(${CANARY_PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(${CANARY_PROJECT_NAME} PROPERTIES CXX_STANDARD ${CMAKE_CXX_STANDARD})

set(CMAKE_C_FLAGS_DEBUGOPT "")

#set warnings
if (MSVC)
    target_compile_options(${CANARY_PROJECT_NAME} PRIVATE /W4 /WX /wd4068)
else ()
    target_compile_options(${CANARY_PROJECT_NAME} PRIVATE -Wall -Wno-long-long -pedantic -Werror)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "" OR CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_definitions(${CANARY_PROJECT_NAME} PRIVATE "-DDEBUG_BUILD")
endif ()

target_include_directories(${CANARY_PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(${CANARY_PROJECT_NAME} aws-crt-cpp)

install(
        TARGETS ${CANARY_PROJECT_NAME}
        RUNTIME DESTINATION bin COMPONENT Runtime
)

