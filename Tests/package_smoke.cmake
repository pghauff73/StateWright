if(NOT DEFINED STATEWRIGHT_BINARY_DIR)
  message(FATAL_ERROR "STATEWRIGHT_BINARY_DIR is required")
endif()
if(NOT DEFINED STATEWRIGHT_FIXTURE_SERVER)
  message(FATAL_ERROR "STATEWRIGHT_FIXTURE_SERVER is required")
endif()
if(NOT DEFINED STATEWRIGHT_SOURCE_DIR)
  message(FATAL_ERROR "STATEWRIGHT_SOURCE_DIR is required")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(prefix "/tmp/statewright-package-smoke-${suffix}")
set(workspace "${prefix}/workspace")
file(MAKE_DIRECTORY "${workspace}")
file(WRITE "${workspace}/sample.cpp" "int main() { return 0; }\n")

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${STATEWRIGHT_BINARY_DIR}" --prefix "${prefix}"
  RESULT_VARIABLE install_result
  OUTPUT_VARIABLE install_output
  ERROR_VARIABLE install_error
)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "install failed (${install_result}): ${install_error}\n${install_output}")
endif()

set(executable "${prefix}/bin/statewright")
if(NOT EXISTS "${executable}")
  message(FATAL_ERROR "installed statewright executable is missing")
endif()
if(NOT EXISTS "${prefix}/share/statewright/manifest.sha256")
  message(FATAL_ERROR "installed StateWright resource manifest is missing")
endif()
foreach(required_resource IN ITEMS
    "policies/internet/default-source-policy-v1.json"
    "policies/internet/default-promotion-policy-v1.json"
    "schemas/statewright-v1/internet-improvement-extension.schema.json"
    "fixtures/internet/identity-v1.json")
  if(NOT EXISTS "${prefix}/share/statewright/${required_resource}")
    message(FATAL_ERROR "installed internet resource is missing: ${required_resource}")
  endif()
endforeach()
if(NOT EXISTS "${prefix}/share/statewright/contracts/migrations/0005-internet-source-freshness-extension.json")
  message(FATAL_ERROR "installed internet migration history is incomplete")
endif()
set(installed_howto
    "${prefix}/share/doc/StateWright/SAA_PERSISTENT_INTERNET_IMPROVEMENT_HOWTO.md")
if(NOT EXISTS "${installed_howto}")
  message(FATAL_ERROR "installed SAA internet HOWTO is missing")
endif()

execute_process(
  COMMAND "${executable}" egcf-command-describe algorithm.search@1
  RESULT_VARIABLE describe_result
  OUTPUT_VARIABLE describe_output
  ERROR_VARIABLE describe_error
)
if(NOT describe_result EQUAL 0 OR
   NOT describe_output MATCHES "algorithm.search@1")
  message(FATAL_ERROR "installed resource discovery failed: ${describe_error}\n${describe_output}")
endif()

execute_process(
  COMMAND "${executable}" command
          "{\"workspace\":\"${workspace}\",\"command_id\":\"repo.metrics@1\",\"inputs\":{}}"
  RESULT_VARIABLE command_result
  OUTPUT_VARIABLE command_output
  ERROR_VARIABLE command_error
)
if(NOT command_result EQUAL 0 OR
   NOT command_output MATCHES "\"status\":\"COMPLETED\"")
  message(FATAL_ERROR "installed CLI command smoke failed: ${command_error}\n${command_output}")
endif()

execute_process(
  COMMAND "${STATEWRIGHT_SOURCE_DIR}/Tests/saa_internet_howto_smoke.sh"
          "${installed_howto}"
          "${executable}"
          "${STATEWRIGHT_FIXTURE_SERVER}"
          "${prefix}/share/statewright/fixtures/internet/identity-v1.json"
  RESULT_VARIABLE internet_result
  OUTPUT_VARIABLE internet_output
  ERROR_VARIABLE internet_error
  TIMEOUT 300
)
if(NOT internet_result EQUAL 0 OR
   NOT internet_output MATCHES "SAA internet HOWTO validation passed")
  message(FATAL_ERROR "installed SAA internet HOWTO smoke failed (${internet_result}): ${internet_error}\n${internet_output}")
endif()

file(REMOVE_RECURSE "${prefix}")
