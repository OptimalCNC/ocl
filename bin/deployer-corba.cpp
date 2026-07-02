/***************************************************************************
  tag: Peter Soetens  Thu Jul 3 15:30:24 CEST 2008  deployer-corba.cpp

                        deployer-corba.cpp -  description
                           -------------------
    begin                : Thu July 03 2008
    copyright            : (C) 2008 Peter Soetens
    email                : peter.soetens@fmtc.be

 ***************************************************************************
 *   This program is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Lesser General Public            *
 *   License as published by the Free Software Foundation; either          *
 *   version 2.1 of the License, or (at your option) any later version.    *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with this program; if not, write to the Free Software   *
 *   Foundation, Inc., 59 Temple Place,                                    *
 *   Suite 330, Boston, MA  02111-1307  USA                                *
 ***************************************************************************/

#include <rtt/rtt-config.h>
#ifdef OS_RT_MALLOC
// need access to all TLSF functions embedded in RTT
#define ORO_MEMORY_POOL
#include <rtt/os/tlsf/tlsf.h>
#endif
#include <rtt/os/main.h>
#include <rtt/RTT.hpp>
#include <rtt/Logger.hpp>

#include <taskbrowser/TaskBrowser.hpp>
#include <deployment/CorbaDeploymentComponent.hpp>
#include <rtt/transports/corba/TaskContextServer.hpp>
#include <iostream>
#include <string>
#include "deployer-funcs.hpp"

using namespace RTT;
using namespace RTT::corba;
namespace po = boost::program_options;

int main(int argc, char** argv)
{
	std::string                 siteFile;      // "" means use default in DeploymentComponent.cpp
	std::vector<std::string>    scriptFiles;
	std::string                 name("Deployer");
    bool                        requireNameService = false;
    bool                        deploymentOnlyChecked = false;
	int							minNumberCPU = 0;
    po::variables_map           vm;
	po::options_description     taoOptions("Additional options after a '--' are passed through to TAO");
	// we don't actually list any options for TAO ...

	po::options_description     otherOptions;

#ifdef  ORO_BUILD_RTALLOC
    OCL::memorySize             rtallocMemorySize   = ORO_DEFAULT_RTALLOC_SIZE;
	po::options_description     rtallocOptions      = OCL::deployerRtallocOptions(rtallocMemorySize);
	otherOptions.add(rtallocOptions);
    OCL::TLSFMemoryPool     memoryPool;
#endif

    // as last set of options
    otherOptions.add(taoOptions);

    // were we given non-deployer options? ie find "--"
    int     optIndex    = 0;
    bool    found       = false;

    while(!found && optIndex<argc)
    {
        found = (0 == strcmp("--", argv[optIndex]));
        if(!found) optIndex++;
    }

    if (found) {
        argv[optIndex] = argv[0];
    }

    // if extra options not found then process all command line options,
    // otherwise process all options up to but not including "--"
    int rc = OCL::deployerParseCmdLine(!found ? argc : optIndex, argv,
                                       siteFile, scriptFiles, name, requireNameService, deploymentOnlyChecked,
									   minNumberCPU,
                                       vm, &otherOptions);
	if (0 != rc)
	{
		return rc;
	}

	// check system capabilities
	rc = OCL::enforceMinNumberCPU(minNumberCPU);
	if (0 != rc)
	{
		return rc;
	}

#ifdef  ORO_BUILD_RTALLOC
    if (!memoryPool.initialize(rtallocMemorySize.size))
    {
        return -1;
    }
#endif  // ORO_BUILD_RTALLOC

    /******************** WARNING ***********************
     *   NO log(...) statements before __os_init() !!!!! 
     ***************************************************/

	if (0 == __os_init(argc - optIndex, &argv[optIndex]))
    {
        rc = -1;     // prove otherwise
        try {
            // if TAO options not found then have TAO process just the program name,
            // otherwise TAO processes the program name plus all options (potentially
            // none) after "--"
            TaskContextServer::InitOrb( argc - optIndex, &argv[optIndex] );

            // scope to force dc destruction prior to memory free and Orb shutdown
            {
                OCL::CorbaDeploymentComponent dc( name, siteFile );

                if (0 == TaskContextServer::Create( &dc, true, requireNameService ))
                {
                    return -1;
                }

                // The orb thread accepts incoming CORBA calls.
                TaskContextServer::ThreadOrb();

                /* Only start the scripts after the Orb was created. Processing of
                   scripts stops after the first failed script, and -1 is returned.
                   Whether a script failed or all scripts succeeded, in non-daemon
                   and non-checking mode the TaskBrowser will be run to allow
                   inspection if the input is a tty.
                 */
                bool result = true;
                for (std::vector<std::string>::const_iterator iter=scriptFiles.begin();
                     iter!=scriptFiles.end() && result;
                     ++iter)
                {
                    if ( !(*iter).empty() )
                    {
                        if ( (*iter).rfind(".xml", std::string::npos) == (*iter).length() - 4 ||
                             (*iter).rfind(".cpf", std::string::npos) == (*iter).length() - 4) {
                            if ( deploymentOnlyChecked ) {
                                bool loadOk         = true;
                                bool configureOk    = true;
                                bool startOk        = true;
                                if (!dc.kickStart2( (*iter), false, loadOk, configureOk, startOk )) {
                                    result = false;
                                    if (!loadOk) {
                                        Logger::log().logf(Logger::Error, "DeployerCorba",
                                                           "Failed to load file: '%s'.", iter->c_str());
                                    } else if (!configureOk) {
                                        Logger::log().logf(Logger::Error, "DeployerCorba",
                                                           "Failed to configure file: '%s'.", iter->c_str());
                                    }
                                    (void)startOk;      // unused - avoid compiler warning
                                }
                                // else leave result=true and continue
                            } else {
                                result = dc.kickStart( (*iter) ) && result;
                            }
                            continue;
                        }

                        if ( (*iter).rfind(".ops", std::string::npos) == (*iter).length() - 4 ||
                             (*iter).rfind(".osd", std::string::npos) == (*iter).length() - 4 ||
                             (*iter).rfind(".lua", std::string::npos) == (*iter).length() - 4) {
                            result = dc.runScript( (*iter) ) && result;
                            continue;
                        }

                        Logger::log().logf(Logger::Error, "DeployerCorba",
                                           "Unknown extension of file: '%s'. Must be xml, cpf for XML files or, ops, osd or lua for script files.",
                                           iter->c_str());
                    }
                }
                rc = (result ? 0 : -1);

                // We don't start an interactive console when we're a daemon
                if ( !deploymentOnlyChecked && !vm.count("daemon") ) {
                    if (isatty(fileno(stdin))) {
                        OCL::TaskBrowser tb( &dc );
                        tb.loop();
                    } else {
                        dc.waitForInterrupt();
                    }

                    // do it while CORBA is still up in case need to do anything remote.
                    dc.shutdownDeployment();
                }
            }

            TaskContextServer::ShutdownOrb();

            TaskContextServer::DestroyOrb();

        } catch( CORBA::Exception &e ) {
            Logger::log().logf(Logger::Error, "DeployerCorba",
                               "%s ORO_main : CORBA exception raised!", argv[0]);
            Logger::log().logf(Logger::Error, "DeployerCorba",
                               "%s", CORBA_EXCEPTION_INFO(e));
        } catch (...) {
            // catch this so that we can destroy the TLSF memory correctly
            Logger::log().logf(Logger::Error, "DeployerCorba",
                               "Uncaught exception.");
        }

        // shutdown Orocos
        __os_exit();
    }
    else
    {
        std::cerr << "Unable to start Orocos" << std::endl;
        rc = -1;
    }

#ifdef  ORO_BUILD_RTALLOC
    memoryPool.shutdown();
#endif

    return rc;
}
