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

# Load the generated baseline table
set(_CRT_MIN_FILE "${CMAKE_CURRENT_LIST_DIR}/CRTMinVersions.cmake")
if(EXISTS "${_CRT_MIN_FILE}")
    include("${_CRT_MIN_FILE}")
endif()

# Helper: make sure a certain commit exists in what might be a shallow clone
function(_awscrt_ensure_commit_present sub_path commit)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} cat-file -e ${commit}^{commit}
        WORKING_DIRECTORY "${sub_path}"
        RESULT_VARIABLE _commit_missing
        ERROR_QUIET)

    if(NOT _commit_missing EQUAL 0)
        execute_process(        # cheap attempt
            COMMAND ${GIT_EXECUTABLE} fetch --depth 1 origin ${commit}
            WORKING_DIRECTORY "${sub_path}" RESULT_VARIABLE _rc ERROR_QUIET)

        if(NOT _rc EQUAL 0)   # fall-back
            message(STATUS
                "fetch uh-shallow on ${sub_path} to determine ancestory")
            execute_process(
                COMMAND ${GIT_EXECUTABLE} fetch --unshallow --tags origin
                WORKING_DIRECTORY "${sub_path}" ERROR_QUIET)
        endif()
    endif()
endfunction()

# Public function: check_submodule_commit(<name> <rel_path> <cmake_sym>)
function(check_submodule_commit name rel_path cmake_sym)
    # NOTE: we assume the caller is the top-level CMakeLists.txt,
    # therefore PROJECT_SOURCE_DIR is the repo root.
    set(_sub_dir "${PROJECT_SOURCE_DIR}/${rel_path}")

    if(NOT DEFINED MIN_${cmake_sym}_COMMIT)
        message(STATUS "Submodule ${name}: no baseline → skipping")
        return()
    endif()

    set(_baseline "${MIN_${cmake_sym}_COMMIT}")
    _awscrt_ensure_commit_present("${_sub_dir}" "${_baseline}")

    # current HEAD
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --verify HEAD
                    WORKING_DIRECTORY "${_sub_dir}"
                    OUTPUT_VARIABLE _cur_sha OUTPUT_STRIP_TRAILING_WHITESPACE)

    # ancestry test
    execute_process(COMMAND ${GIT_EXECUTABLE} merge-base --is-ancestor
                            ${_baseline} ${_cur_sha}
                    WORKING_DIRECTORY "${_sub_dir}"
                    RESULT_VARIABLE _is_ancestor ERROR_QUIET)

    if(_is_ancestor AND NOT _is_ancestor EQUAL 0)
        message(FATAL_ERROR
            "Submodule ${name} at ${_sub_dir}\n"
            "  HEAD     : ${_cur_sha}\n"
            "  baseline : ${_baseline}\n"
            "is **older** or on a divergent branch.\n"
            "Fast-forward the submodule or regenerate CRTMinVersions.cmake.")
    else()
        message(STATUS "Submodule ${name}: commit OK (≥ baseline)")
    endif()
endfunction()
