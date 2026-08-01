SET(EXTRA_LIBRARIES)
SET(EXTRA_DEPS)

IF ( BUILD_RTALLOC )
  # default to 512 kilobytes - see minimum size warning in deployer-funcs.cpp
  SET(ORO_DEFAULT_RTALLOC_SIZE 524288 CACHE STRING "Real-time memory pool size (bytes) for operations and logging.")
  ADD_DEFINITIONS("-DORO_BUILD_RTALLOC -DORO_DEFAULT_RTALLOC_SIZE=${ORO_DEFAULT_RTALLOC_SIZE}")
ELSE ( BUILD_RTALLOC )
  ADD_DEFINITIONS("-DORO_DEFAULT_RTALLOC_SIZE=0")
ENDIF ( BUILD_RTALLOC )

# TODO get this from RTT config
IF (UNIX AND NOT APPLE AND NOT SSC_NO_CPU_AFFINITY)
  ADD_DEFINITIONS("-DORO_SUPPORT_CPU_AFFINITY=1")
ENDIF (UNIX AND NOT APPLE AND NOT SSC_NO_CPU_AFFINITY)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
configure_file(installpath.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/installpath.hpp @ONLY)

# Local Deployer application
IF ( BUILD_TASKBROWSER AND BUILD_DEPLOYMENT)

    orocos_executable( deployer deployer.cpp deployer-funcs.cpp )
    target_link_libraries( deployer ${EXTRA_DEPS} )
    target_link_libraries( deployer ${OROCOS-RTT_LIBRARIES} orocos-ocl-taskbrowser orocos-ocl-deployment ${Boost_PROGRAM_OPTIONS_LIBRARY} ${EXTRA_LIBRARIES})
    list(APPEND UTILITY_SCRIPTS deployer)

ENDIF ( BUILD_TASKBROWSER AND BUILD_DEPLOYMENT)
IF ( BUILD_DEPLOYMENT)

    orocos_executable( rttscript deployer.cpp deployer-funcs.cpp )
    target_link_libraries( rttscript ${EXTRA_DEPS} )
    target_link_libraries( rttscript ${OROCOS-RTT_LIBRARIES} orocos-ocl-deployment ${Boost_PROGRAM_OPTIONS_LIBRARY} ${EXTRA_LIBRARIES})
    set_target_properties( rttscript PROPERTIES COMPILE_DEFINITIONS RTT_SCRIPT_PROGRAM )
    list(APPEND UTILITY_SCRIPTS rttscript)

ENDIF ( BUILD_DEPLOYMENT)

IF (BUILD_TASKBROWSER AND BUILD_DEPLOYMENT AND OCL_OPCUA_FOUND)

    orocos_executable(deployer-opcua deployer.cpp deployer-funcs.cpp)
    target_compile_definitions(deployer-opcua PRIVATE OCL_OPCUA_DEPLOYER)
    target_include_directories(deployer-opcua PRIVATE ${RTT_OPCUA_INCLUDE_DIRS})
    target_compile_options(deployer-opcua PRIVATE ${RTT_OPCUA_CFLAGS_OTHER})
    target_link_libraries(deployer-opcua ${EXTRA_DEPS})
    target_link_libraries(deployer-opcua
      ${OROCOS-RTT_LIBRARIES} orocos-ocl-taskbrowser
      orocos-ocl-deployment-opcua ${Boost_PROGRAM_OPTIONS_LIBRARY}
      ${RTT_OPCUA_LDFLAGS} ${EXTRA_LIBRARIES})

    orocos_executable(ctaskbrowser-opcua ctaskbrowser-opcua.cpp)
    target_include_directories(ctaskbrowser-opcua PRIVATE
      ${RTT_OPCUA_INCLUDE_DIRS})
    target_compile_options(ctaskbrowser-opcua PRIVATE
      ${RTT_OPCUA_CFLAGS_OTHER})
    target_link_libraries(ctaskbrowser-opcua ${EXTRA_DEPS})
    target_link_libraries(ctaskbrowser-opcua
      ${OROCOS-RTT_LIBRARIES} orocos-ocl-taskbrowser
      ${RTT_OPCUA_LDFLAGS} ${EXTRA_LIBRARIES})

    configure_file(deployer.in deployer-opcua @ONLY)
    configure_file(ctaskbrowser.in ctaskbrowser-opcua @ONLY)
    install(PROGRAMS
      ${CMAKE_CURRENT_BINARY_DIR}/deployer-opcua
      ${CMAKE_CURRENT_BINARY_DIR}/ctaskbrowser-opcua
      DESTINATION bin)

ENDIF (BUILD_TASKBROWSER AND BUILD_DEPLOYMENT AND OCL_OPCUA_FOUND)

# Remote TaskBrowser application
IF ( BUILD_TASKBROWSER AND OROCOS-RTT_CORBA_FOUND)

    orocos_executable( ctaskbrowser ctaskbrowser.cpp )
    target_link_libraries( ctaskbrowser ${EXTRA_DEPS} )
    target_link_libraries( ctaskbrowser ${OROCOS-RTT_LIBRARIES} orocos-ocl-taskbrowser ${OROCOS-RTT_CORBA_LIBRARIES} ${EXTRA_LIBRARIES} )
    list(APPEND UTILITY_SCRIPTS ctaskbrowser)

ENDIF ( BUILD_TASKBROWSER AND OROCOS-RTT_CORBA_FOUND)

# Deployment Server which can be accessed using ctaskbrowser
IF ( BUILD_DEPLOYMENT AND OROCOS-RTT_CORBA_FOUND)

    orocos_executable( cdeployer cdeployer.cpp deployer-funcs.cpp )
    target_link_libraries( cdeployer ${EXTRA_DEPS} )
    target_link_libraries( cdeployer ${OROCOS-RTT_LIBRARIES} ${OROCOS-RTT_CORBA_LIBRARIES} orocos-ocl-deployment-corba ${Boost_PROGRAM_OPTIONS_LIBRARY} ${EXTRA_LIBRARIES})
    list(APPEND UTILITY_SCRIPTS cdeployer)

    orocos_executable( deployer-corba deployer-corba.cpp deployer-funcs.cpp )
    target_link_libraries( deployer-corba ${EXTRA_DEPS} )
    target_link_libraries( deployer-corba ${OROCOS-RTT_LIBRARIES} ${OROCOS-RTT_CORBA_LIBRARIES} orocos-ocl-deployment-corba orocos-ocl-taskbrowser ${Boost_PROGRAM_OPTIONS_LIBRARY} ${EXTRA_LIBRARIES})
    list(APPEND UTILITY_SCRIPTS deployer-corba)

ENDIF ( BUILD_DEPLOYMENT AND OROCOS-RTT_CORBA_FOUND)

# Utility scripts
FOREACH ( SCRIPT ${UTILITY_SCRIPTS} )
    configure_file( ${SCRIPT}.in ${SCRIPT} @ONLY )
    install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/${SCRIPT} DESTINATION bin)
ENDFOREACH ( SCRIPT ${UTILITY_SCRIPTS} )
