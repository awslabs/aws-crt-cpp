include(CMakeFindDependencyMacro)

find_dependency(aws-c-http)
find_dependency(aws-c-mqtt)
find_dependency(aws-c-cal)
find_dependency(aws-c-auth)

include(${CMAKE_CURRENT_LIST_DIR}/@CMAKE_PROJECT_NAME@-targets.cmake)