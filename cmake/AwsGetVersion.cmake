find_package(Git QUIET) # Adding development helper tools as git_hash built when available.

function(obtain_project_version resultVarVersion resultVarGitHash)
    if (GIT_FOUND)
        execute_process(
                COMMAND ${GIT_EXECUTABLE}  --git-dir=${CMAKE_CURRENT_SOURCE_DIR}/.git rev-parse HEAD
                RESULT_VARIABLE git_result
                OUTPUT_VARIABLE GIT_HASH
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if (git_result EQUAL 0)
            set(${resultVarGitHash} ${GIT_HASH} PARENT_SCOPE)
            message(STATUS "Building git hash: ${GIT_HASH}")
        endif()
    endif ()

    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VERSION_STRING)

    set(${resultVarVersion} ${VERSION_STRING} PARENT_SCOPE)
endfunction()
