# cmake/EnforceSubmoduleVersions.cmake
#
# Guard against multiple inclusion (for CMake 3.9 we cannot use
# the include_guard() command yet).
if(DEFINED _AWSCRT_ENFORCE_SUBMODULE_VERSIONS_INCLUDED)
    return()
endif()
set(_AWSCRT_ENFORCE_SUBMODULE_VERSIONS_INCLUDED TRUE)

# Nothing to do if the caller disabled the checks *or* if BUILD_DEPS is OFF
if(NOT ENFORCE_SUBMODULE_VERSIONS OR NOT BUILD_DEPS)
    return()
endif()

# Ensure Git is available
find_package(Git QUIET)
if(NOT GIT_FOUND)
    message(FATAL_ERROR
        "ENFORCE_SUBMODULE_VERSIONS is ON but Git was not found.\n"
        "  • Install Git and ensure it is in PATH,          e.g.   sudo apt install git\n"
        "  • or tell CMake where Git lives:                 -DGIT_EXECUTABLE=/path/git\n"
        "  • or rerun CMake with commit checks disabled:    -DENFORCE_SUBMODULE_VERSIONS=OFF")
endif()

function(check_submodule_commit name rel_path)
    set(_sub_dir  "${PROJECT_SOURCE_DIR}/${rel_path}")

    # determine baseline from super repository (aws-crt-cpp) to get expected commit of submodule
    # ask Git for the SHA that is stored in the super-repo’s index
    execute_process(
        COMMAND           ${GIT_EXECUTABLE} "rev-parse" "HEAD:${rel_path}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE   _baseline
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_VARIABLE    _err
        RESULT_VARIABLE   _rc)

    if(_rc OR "${_baseline}" STREQUAL "")
        message(STATUS "Could not query git-link for ${rel_path}: ${_err}. Skipping Submodule Version Check.")
    endif()

    # ensure the commit object exists (deepens shallow clones/branches)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} cat-file -e ${_baseline}^{commit}
        WORKING_DIRECTORY "${_sub_dir}"
        RESULT_VARIABLE _missing ERROR_QUIET)

    if(_missing)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} fetch --depth 1 origin ${_baseline}
            WORKING_DIRECTORY "${_sub_dir}" RESULT_VARIABLE _rc ERROR_QUIET)
        if(_rc)
            execute_process(
                COMMAND ${GIT_EXECUTABLE} fetch --unshallow --tags origin
                WORKING_DIRECTORY "${_sub_dir}" ERROR_QUIET)
        endif()
    endif()

    # ancestry check
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --verify HEAD
                    WORKING_DIRECTORY "${_sub_dir}"
                    OUTPUT_VARIABLE _head
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_VARIABLE    _err
                    RESULT_VARIABLE _rc ERROR_QUIET)
    
    if(_rc)
        message(STATUS "Could not get HEAD commit for ${name}: ${_err}. Skipping Submodule Version Check.")
        return()
    endif()

    execute_process(COMMAND ${GIT_EXECUTABLE} merge-base --is-ancestor
                            ${_baseline} ${_head}
                    WORKING_DIRECTORY "${_sub_dir}"
                    RESULT_VARIABLE _is_anc ERROR_QUIET)
    
    # Check if merge-base command failed (not just ancestry check)
    if(_is_anc EQUAL -1)
        message(WARNING "Could not determine ancestry for ${name} at ${_sub_dir}")
        return()
    endif()

    if(_is_anc GREATER 0)
        message(FATAL_ERROR
            "Sub-module ${name} at ${_sub_dir}\n"
            "  HEAD     : ${_head}\n"
            "  baseline : ${_baseline}\n"
            "is **older** or on a divergent branch.\n"
            "This check can be disabled with: -DENFORCE_SUBMODULE_VERSIONS=OFF")
    else()
        string(SUBSTRING "${_head}"     0 7 _head_short)
        string(SUBSTRING "${_baseline}" 0 7 _base_short)
        message(STATUS "Submodule ${name} commit:${_head_short} ≥ baseline:${_base_short}")
    endif()
endfunction()
