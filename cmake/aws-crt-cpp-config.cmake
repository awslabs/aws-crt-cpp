include(CMakeFindDependencyMacro)

find_dependency(aws-c-mqtt)
find_dependency(aws-c-cal)

include(${CMAKE_CURRENT_LIST_DIR}/@CMAKE_PROJECT_NAME@-targets.cmake)