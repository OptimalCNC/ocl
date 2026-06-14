macro(GLOBAL_ADD_TEST TEST_NAME)
  add_executable(${TEST_NAME} EXCLUDE_FROM_ALL ${ARGN})
  orocos_configure_executable(${TEST_NAME})
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endmacro()

macro(PROGRAM_ADD_DEPS TEST_NAME)
  target_link_libraries(${TEST_NAME} ${ARGN})
endmacro()
