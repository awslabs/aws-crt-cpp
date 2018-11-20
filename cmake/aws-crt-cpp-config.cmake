include(CMakeFindDependencyMacro)

find_dependency(aws-c-mqtt)

include(${CMAKE_CURRENT_LIST_DIR}/@CMAKE_PROJECT_NAME@-targets.cmake)