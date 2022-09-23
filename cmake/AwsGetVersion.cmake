include_guard()


find_package(Git QUIET) # Adding development helper tools as git_hash built when available.

function(obtain_project_version resultVarVersion resultVarGitHash)
    if (GIT_FOUND)
        execute_process(
                COMMAND ${GIT_EXECUTABLE} --git-dir=${CMAKE_CURRENT_SOURCE_DIR}/.git describe --abbrev=0 --tags
                OUTPUT_VARIABLE VERSION_STRING
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        execute_process(
                COMMAND ${GIT_EXECUTABLE}  --git-dir=${CMAKE_CURRENT_SOURCE_DIR}/.git rev-parse HEAD
                RESULT_VARIABLE git_result
                OUTPUT_VARIABLE GIT_HASH
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if (NOT git_result)
            set(${resultVarGitHash} ${GIT_HASH} PARENT_SCOPE)
            message(STATUS "Building git hash: ${GIT_HASH}")
        endif()
    endif ()

    if (NOT VERSION_STRING OR NOT ${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
        # If not the top level git project or Non DEV build, set the version from the VERSION file.
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VERSION_STRING)
    endif ()

    set(${resultVarVersion} ${VERSION_STRING} PARENT_SCOPE)
endfunction()
