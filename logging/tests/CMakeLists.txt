CMAKE_DEPENDENT_OPTION( BUILD_LOGGING_TEST "Build logging tests" ON "BUILD_LOGGING;BUILD_TESTS" OFF)

IF ( BUILD_LOGGING_TEST )

  GLOBAL_ADD_TEST(testlogging testlogging.cpp)
  target_link_libraries(testlogging orocos-ocl-logging)
  set_tests_properties(testlogging PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_INSTALL_PREFIX}/lib:$ENV{LD_LIBRARY_PATH}")

ENDIF ( BUILD_LOGGING_TEST )
