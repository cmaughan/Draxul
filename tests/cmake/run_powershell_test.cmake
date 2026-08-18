if(NOT DEFINED DRAXUL_POWERSHELL_SCRIPT OR NOT DEFINED DRAXUL_EXECUTABLE)
    message(FATAL_ERROR
        "DRAXUL_POWERSHELL_SCRIPT and DRAXUL_EXECUTABLE are required")
endif()

# This script runs in a fresh CMake process for each CTest invocation. NO_CACHE
# keeps discovery tied to the current PATH rather than a configure-time result.
find_program(_draxul_powershell NAMES pwsh powershell NO_CACHE)
if(NOT _draxul_powershell)
    message(FATAL_ERROR
        "The topology CLI integration test requires pwsh or powershell on PATH")
endif()

execute_process(
    COMMAND "${_draxul_powershell}"
        -NoProfile
        -ExecutionPolicy Bypass
        -File "${DRAXUL_POWERSHELL_SCRIPT}"
        -Executable "${DRAXUL_EXECUTABLE}"
    RESULT_VARIABLE _draxul_powershell_result)
if(NOT _draxul_powershell_result STREQUAL "0")
    message(FATAL_ERROR
        "PowerShell test failed with exit code ${_draxul_powershell_result}")
endif()
