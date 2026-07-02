/***************************************************************************
  tag: Peter Soetens  Thu Jul 3 15:34:48 CEST 2008  CorbaDeploymentComponent.cpp

                        CorbaDeploymentComponent.cpp -  description
                           -------------------
    begin                : Thu July 03 2008
    copyright            : (C) 2008 Peter Soetens
    email                : peter.soetens@fmtc.be

 ***************************************************************************
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Lesser General Public            *
 *   License as published by the Free Software Foundation; either          *
 *   version 2.1 of the License, or (at your option) any later version.    *
 *                                                                         *
 *   This library is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with this library; if not, write to the Free Software   *
 *   Foundation, Inc., 59 Temple Place,                                    *
 *   Suite 330, Boston, MA  02111-1307  USA                                *
 *                                                                         *
 ***************************************************************************/


#include "CorbaDeploymentComponent.hpp"
#include <rtt/transports/corba/TaskContextProxy.hpp>
#include <rtt/transports/corba/TaskContextServer.hpp>
#include <rtt/deployment/ComponentLoader.hpp>
#include "ocl/Component.hpp"
#include <fstream>

namespace OCL
{

    /**
     * This helper function looks up a server using the Naming Service
     * and creates a proxy for that object.
     */
    RTT::TaskContext* createTaskContextProxy(std::string name)
    {
        RTT::Logger::log().logf(RTT::Logger::Debug, "CorbaDeploymentComponent",
                                "createTaskContextProxy");
        return ::RTT::corba::TaskContextProxy::Create(name, false);
    }

    /**
     * This helper function looks up a server using an IOR file
     * and creates a proxy for that object.
     */
    RTT::TaskContext* createTaskContextProxyIORFile(std::string iorfilename)
    {
        RTT::Logger::log().logf(RTT::Logger::Debug, "CorbaDeploymentComponent",
                                "createTaskContextProxyIORFile");
        std::ifstream iorfile( iorfilename.c_str() );
        if (iorfile.is_open() && iorfile.good() ) {
            std::string ior;
            iorfile >> ior;
            return ::RTT::corba::TaskContextProxy::Create( ior, true);
        }
        else {
            RTT::Logger::log().logf(RTT::Logger::Error, "CorbaDeploymentComponent",
                                    "Could not open IORFile: '%s'.", iorfilename.c_str());
            return 0;
        }
    }

    /**
     * This helper function looks up a server using an IOR file
     * and creates a proxy for that object.
     */
    RTT::TaskContext* createTaskContextProxyIOR(std::string ior)
    {
        RTT::Logger::log().logf(RTT::Logger::Debug, "CorbaDeploymentComponent",
                                "createTaskContextProxyIOR");
        return ::RTT::corba::TaskContextProxy::Create( ior, true);
    }


CorbaDeploymentComponent::CorbaDeploymentComponent(const std::string& name, const std::string& siteFile)
        : DeploymentComponent(name, siteFile)
    {
        RTT::Logger::log().logf(RTT::Logger::Info, "CorbaDeploymentComponent",
                                "Registering TaskContextProxy factory.");
        ComponentLoader::Instance()->addFactory("TaskContextProxy", &createTaskContextProxy);
        ComponentLoader::Instance()->addFactory("CORBA", &createTaskContextProxy);
        ComponentLoader::Instance()->addFactory("IORFile", &createTaskContextProxyIORFile);
        ComponentLoader::Instance()->addFactory("IOR", &createTaskContextProxyIOR);

        this->addOperation("server", &CorbaDeploymentComponent::createServer, this, ClientThread).doc("Creates a CORBA TaskContext server for the given component").arg("tc", "Name of the RTT::TaskContext (must be a peer).").arg("UseNamingService", "Set to true to use the naming service.");
        this->addOperation("aliasServer", &CorbaDeploymentComponent::createAliasServer, this, ClientThread).doc("Creates a CORBA TaskContext server for the given component using an alias").arg("tc", "Name of the RTT::TaskContext (must be a peer).").arg("alias", "Alias to use when registering to the CORBA Namingservice").arg("UseNamingService", "Set to true to use the naming service.");
    }

    CorbaDeploymentComponent::~CorbaDeploymentComponent()
    {
        // removes our own server, before removing peer's.
        ::RTT::corba::TaskContextServer::CleanupServer(this);
    }

    bool CorbaDeploymentComponent::createServer(const std::string& tc, bool use_naming)
    {
        RTT::TaskContext* peer = this->getPeer(tc);
        if (!peer) {
            RTT::Logger::log().logf(RTT::Logger::Error, "CorbaDeploymentComponent",
                                    "No such peer: %s", tc.c_str());
            return false;
        }
        if ( ::RTT::corba::TaskContextServer::Create(peer, use_naming) != 0 )
            return true;
        return false;
    }

    bool CorbaDeploymentComponent::createAliasServer(const std::string& tc, const std::string& alias, bool use_naming)
    {
        RTT::TaskContext* peer = this->getPeer(tc);
        if (!peer) {
            RTT::Logger::log().logf(RTT::Logger::Error, "CorbaDeploymentComponent",
                                    "No such peer: %s", tc.c_str());
            return false;
        }
        if ( ::RTT::corba::TaskContextServer::Create(peer, alias, use_naming) != 0 )
            return true;
        return false;
    }

    bool CorbaDeploymentComponent::componentLoaded(RTT::TaskContext* c)
    {
        if ( dynamic_cast<RTT::corba::TaskContextProxy*>(c) ) {
            // is a proxy.
            for ( CompMap::iterator cit = compmap.begin(); cit != compmap.end(); ++cit) {
                if (cit->second.instance == c) {
                    cit->second.proxy = true;
                    return true;
                }
            }
            // impossible: proxy not found
            assert(false);
            return false;
        }
        bool use_naming = compmap[c->getName()].use_naming;
        bool server = compmap[c->getName()].server;
        RTT::Logger::log().logf(RTT::Logger::Info, "CorbaDeploymentComponent",
                                "Name:%s Server: %d Naming: %d",
                                c->getName().c_str(), server ? 1 : 0, use_naming ? 1 : 0);
        // create a server, use naming.
        if (server)
            ::RTT::corba::TaskContextServer::Create(c, use_naming);
        return true;
    }

    void CorbaDeploymentComponent::componentUnloaded(TaskContext* c)
    {
        ::RTT::corba::TaskContextServer::CleanupServer( c );
    }
}
