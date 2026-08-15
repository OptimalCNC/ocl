if(NOT DEFINED DEPLOYER_OPCUA OR DEPLOYER_OPCUA STREQUAL "")
  message(FATAL_ERROR "DEPLOYER_OPCUA must name the deployer OPC UA binary")
endif()

execute_process(
  COMMAND "${DEPLOYER_OPCUA}" --opcua-address 127.0.0.1 --check
  TIMEOUT 10
  RESULT_VARIABLE command_result
  OUTPUT_VARIABLE command_output
  ERROR_VARIABLE command_output
)

if(NOT command_result MATCHES "^[0-9]+$")
  message(FATAL_ERROR
    "deployer OPC UA command did not run: ${command_result}\n${command_output}")
endif()
if(command_result EQUAL 0)
  message(FATAL_ERROR
    "deployer OPC UA accepted --opcua-address\n${command_output}")
endif()

set(expected_diagnostic
  "Exception:unrecognised option '--opcua-address'")
string(REPLACE "\r\n" "\n" normalized_output "${command_output}")
string(REPLACE "\r" "\n" normalized_output "${normalized_output}")
string(REPLACE "\n" ";" output_lines "${normalized_output}")

set(diagnostic_count 0)
foreach(output_line IN LISTS output_lines)
  if(output_line STREQUAL expected_diagnostic)
    math(EXPR diagnostic_count "${diagnostic_count} + 1")
  endif()
endforeach()

if(NOT diagnostic_count EQUAL 1)
  message(FATAL_ERROR
    "expected exact deployer OPC UA address rejection diagnostic\n"
    "result: ${command_result}\noutput:\n${command_output}")
endif()
