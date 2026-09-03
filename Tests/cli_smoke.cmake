if(NOT DEFINED STATEWRIGHT_EXECUTABLE)
  message(FATAL_ERROR "STATEWRIGHT_EXECUTABLE is required")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(workspace "${CMAKE_CURRENT_BINARY_DIR}/statewright-cli-smoke-${suffix}")
file(MAKE_DIRECTORY "${workspace}")
file(WRITE "${workspace}/sample.cpp" "int main() { return 0; }\n")

function(require_success label)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${label} failed (${result}): ${error}\n${output}")
  endif()
  set("${label}_OUTPUT" "${output}" PARENT_SCOPE)
endfunction()

require_success(
  inspect
  "${STATEWRIGHT_EXECUTABLE}" inspect
  "{\"workspace\":\"${workspace}\",\"path\":\".\"}"
)
if(NOT inspect_OUTPUT MATCHES "\"protocol\":\"statewright.cli.v1\"")
  message(FATAL_ERROR "inspect did not return the stable JSON protocol: ${inspect_OUTPUT}")
endif()
if(NOT inspect_OUTPUT MATCHES "sample.cpp")
  message(FATAL_ERROR "inspect did not report the fixture file: ${inspect_OUTPUT}")
endif()

require_success(
  hypothesis
  "${STATEWRIGHT_EXECUTABLE}" hypothesis
  "{\"input\":{\"problem_id\":\"problem:test\",\"proposals\":[{\"hypothesis_id\":\"hypothesis:a\",\"proposition\":\"A\",\"prior_bp\":5000},{\"hypothesis_id\":\"hypothesis:b\",\"proposition\":\"B\",\"prior_bp\":5000}]}}"
)
if(NOT hypothesis_OUTPUT MATCHES "\"ok\":true")
  message(FATAL_ERROR "hypothesis did not succeed: ${hypothesis_OUTPUT}")
endif()

require_success(
  command
  "${STATEWRIGHT_EXECUTABLE}" command
  "{\"workspace\":\"${workspace}\",\"command_id\":\"repo.metrics@1\",\"inputs\":{}}"
)
if(NOT command_OUTPUT MATCHES "\"status\":\"COMPLETED\"")
  message(FATAL_ERROR "command did not complete: ${command_OUTPUT}")
endif()
if(NOT command_OUTPUT MATCHES "\"read_only\":true")
  message(FATAL_ERROR "command did not retain read-only authority: ${command_OUTPUT}")
endif()

file(REMOVE_RECURSE "${workspace}")
