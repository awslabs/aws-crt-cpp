include(CMakeFindDependencyMacro)

find_dependency(aws-c-http)
find_dependency(aws-c-mqtt)
find_dependency(aws-c-cal)
find_dependency(aws-c-auth)
find_dependency(aws-c-event-stream)
find_dependency(aws-c-s3)

if (BUILD_SHARED_LIBS)
    include(${CMAKE_CURRENT_LIST_DIR}/shared/@PROJECT_NAME@-targets.cmake)
else ()
    include(${CMAKE_CURRENT_LIST_DIR}/static/@PROJECT_NAME@-targets.cmake)
endif()
