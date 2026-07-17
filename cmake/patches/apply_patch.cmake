# Idempotent git-apply for FetchContent PATCH_COMMAND.
# Usage: cmake -DPATCH_FILE=<path> -P apply_patch.cmake  (cwd = source tree)
# Skips silently when the patch is already applied (patch step can re-run).

execute_process(
    COMMAND git apply --reverse --check --ignore-whitespace "${PATCH_FILE}"
    RESULT_VARIABLE _already_applied
    OUTPUT_QUIET ERROR_QUIET
)
if(_already_applied EQUAL 0)
    message(STATUS "Patch already applied: ${PATCH_FILE}")
    return()
endif()

execute_process(
    COMMAND git apply --ignore-whitespace "${PATCH_FILE}"
    RESULT_VARIABLE _apply_result
)
if(NOT _apply_result EQUAL 0)
    message(FATAL_ERROR "Failed to apply patch: ${PATCH_FILE}")
endif()
