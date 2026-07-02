OPTION( BUILD_LOGGING "Build RTT-backed logging service" ON)

IF ( BUILD_LOGGING )

  orocos_component( orocos-ocl-logging LoggingService.cpp )
  set_target_properties(orocos-ocl-logging PROPERTIES
    SOVERSION ${OCL_SOVERSION}
  )

  orocos_install_headers( LoggingService.hpp INSTALL include/orocos/ocl )

  orocos_generate_package()
  ADD_SUBDIRECTORY( tests )

ENDIF ( BUILD_LOGGING )
