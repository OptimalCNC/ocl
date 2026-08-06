OPTION( BUILD_DEPLOYMENT "Build Deployment Component" ON )

IF ( BUILD_DEPLOYMENT )

  find_package(RTTPlugin REQUIRED rtt-marshalling rtt-scripting)
  
  find_package(Boost 1.36 REQUIRED filesystem system)

    # This gathers all the .cpp files into the variable 'SRCS'
    SET( HPPS DeploymentComponent.hpp )
    SET( SRCS DeploymentComponent.cpp )

    # Add Lua support if BUILD_LUA_RTT is on
    if(BUILD_LUA_RTT)
        add_definitions(-DBUILD_LUA_RTT)
    endif()

    orocos_library( orocos-ocl-deployment ${SRCS})
    SET_TARGET_PROPERTIES( orocos-ocl-deployment PROPERTIES
      SOVERSION ${OCL_SOVERSION}
      DEFINE_SYMBOL OCL_DLL_EXPORT)
    orocos_install_headers( ${HPPS} INSTALL include/orocos/ocl )
    
    TARGET_LINK_LIBRARIES( orocos-ocl-deployment ${RTT_PLUGIN_rtt-marshalling_${OROCOS_TARGET}_LIBRARIES} ${RTT_PLUGIN_rtt-scripting_${OROCOS_TARGET}_LIBRARIES})
    
    IF (OROCOS-RTT_CORBA_FOUND)
        orocos_library(orocos-ocl-deployment-corba CorbaDeploymentComponent.cpp )
        SET_TARGET_PROPERTIES( orocos-ocl-deployment-corba PROPERTIES
          SOVERSION ${OCL_SOVERSION}
          DEFINE_SYMBOL OCL_DLL_EXPORT)
        orocos_install_headers( CorbaDeploymentComponent.hpp INSTALL include/orocos/ocl )
        TARGET_LINK_LIBRARIES( orocos-ocl-deployment-corba orocos-ocl-deployment)
        TARGET_LINK_LIBRARIES( orocos-ocl-deployment-corba ${OROCOS-RTT_CORBA_LIBRARIES})
    ENDIF(OROCOS-RTT_CORBA_FOUND)

    IF (OCL_OPCUA_FOUND)
        orocos_library(orocos-ocl-deployment-opcua OpcUaDeploymentComponent.cpp)
        SET_TARGET_PROPERTIES(orocos-ocl-deployment-opcua PROPERTIES
          SOVERSION ${OCL_SOVERSION}
          DEFINE_SYMBOL OCL_DLL_EXPORT)
        orocos_install_headers(OpcUaDeploymentComponent.hpp INSTALL include/orocos/ocl)
        TARGET_INCLUDE_DIRECTORIES(orocos-ocl-deployment-opcua PRIVATE
          ${RTT_OPCUA_INCLUDE_DIRS})
        TARGET_COMPILE_OPTIONS(orocos-ocl-deployment-opcua PRIVATE
          ${RTT_OPCUA_CFLAGS_OTHER})
        IF(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
          TARGET_COMPILE_OPTIONS(orocos-ocl-deployment-opcua PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        ENDIF()
        TARGET_LINK_LIBRARIES(orocos-ocl-deployment-opcua
          orocos-ocl-deployment ${RTT_OPCUA_LDFLAGS})
    ENDIF(OCL_OPCUA_FOUND)
    
    IF(OCL_OPCUA_FOUND)
      orocos_generate_package(DEPENDS rtt_opcua-${OROCOS_TARGET})
    ELSE(OCL_OPCUA_FOUND)
      orocos_generate_package()
    ENDIF(OCL_OPCUA_FOUND)

    IF(BUILD_TESTING)
      # Compile-only API guard: Xenomai auto-init can abort before main on non-RT hosts.
      add_executable(ocl_deployment_api_check tests/deployment_api_test.cpp)
      orocos_configure_executable(ocl_deployment_api_check)
      target_link_libraries(ocl_deployment_api_check orocos-ocl-deployment)

      IF(OCL_OPCUA_FOUND)
        add_executable(ocl_opcua_deployment_test tests/opcua_deployment_test.cpp)
        orocos_configure_executable(ocl_opcua_deployment_test)
        target_include_directories(ocl_opcua_deployment_test PRIVATE
          ${RTT_OPCUA_INCLUDE_DIRS})
        target_compile_options(ocl_opcua_deployment_test PRIVATE
          ${RTT_OPCUA_CFLAGS_OTHER})
        IF(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
          target_compile_options(ocl_opcua_deployment_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        ENDIF()
        target_link_libraries(ocl_opcua_deployment_test
          orocos-ocl-deployment-opcua ${RTT_OPCUA_LDFLAGS}
          ${OROCOS-RTT_TYPEKIT_LIBRARIES})
        foreach(case IN ITEMS
            explicit_start_publishes_only_deployer
            strict_publication_is_static_and_idempotent
            server_metadata_does_not_auto_publish
            failed_start_freezes_registry_and_can_retry
            remote_components_remain_aliased_client_peers
            published_component_unload_is_rejected)
          add_test(
            NAME ocl_opcua_deployment_${case}
            COMMAND ocl_opcua_deployment_test --run_test=${case})
        endforeach()
      ENDIF(OCL_OPCUA_FOUND)
    ENDIF(BUILD_TESTING)

    ADD_SUBDIRECTORY( tests )

    # This requires the orocos-rtt library to be carefully tagged
    # with the necessary 'export' attributes, or unresolved symbols
    # will appear. Especially template classes and exceptions are 
    # sensitive.
    # SET_SOURCE_FILES_PROPERTIES( ${SRCS} PROPERTIES COMPILE_FLAGS "-fvisibility=hidden")

ENDIF ( BUILD_DEPLOYMENT )
