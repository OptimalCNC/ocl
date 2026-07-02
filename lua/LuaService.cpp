/*
 * Lua scripting plugin
 */

#include "LuaService.hpp"
#include <rtt/plugin/ServicePlugin.hpp>
#include <iostream>

#include "rtt.hpp"

#ifdef LUA_RTT_TLSF
extern "C" {
#include "tlsf_rtt.h"
}
#endif

#ifdef LUA_RTT_TLSF
#define LuaService LuaTLSFService
#else
#define LuaService LuaService
#endif

using namespace RTT;

namespace OCL {

  LuaService::LuaService(RTT::TaskContext* tc)
#if LUA_RTT_TLSF
    : RTT::Service("LuaTLSF", tc)
#else
    : RTT::Service("Lua", tc)
#endif
  {
    /* initialize lua */
    os::MutexLock lock(m);

#if LUA_RTT_TLSF
    tlsf_inf = new lua_tlsf_info;
    if(tlsf_rtt_init_mp(tlsf_inf, TLSF_INITIAL_POOLSIZE)) {
      Logger::log().logf(Logger::Error, "LuaService",
                         "LuaService (TLSF)'%s': failed to create tlsf pool (0x%x bytes)",
                         this->getOwner()->getName().c_str(), static_cast<unsigned>(TLSF_INITIAL_POOLSIZE));
      throw;
    }

    L = lua_newstate(tlsf_alloc, tlsf_inf);
    tlsf_inf->L = L;
    set_context_tlsf_info(tlsf_inf);
    register_tlsf_api(L);
#else
    L = luaL_newstate();
#endif

    if (L == NULL) {
      Logger::log().logf(Logger::Error, "LuaService",
                         "LuaService ctr '%s': cannot create state: not enough memory",
                         this->getOwner()->getName().c_str());
      throw;
    }


    lua_gc(L, LUA_GCSTOP, 0);
    luaL_openlibs(L);
    lua_gc(L, LUA_GCRESTART, 0);

    /* setup rtt bindings */
    lua_pushcfunction(L, luaopen_rtt);
    lua_call(L, 0, 0);

    set_context_tc(tc, L);

    this->addOperation("exec_file", &LuaService::exec_file, this)
      .doc("load (and run) the given lua script")
      .arg("filename", "filename of the lua script");

    this->addOperation("exec_str", &LuaService::exec_str, this)
      .doc("evaluate the given string in the lua environment")
      .arg("lua-string", "string of lua code to evaluate");

#ifdef LUA_RTT_TLSF
    this->addOperation("tlsf_incmem", &LuaService::tlsf_incmem, this, OwnThread)
      .doc("increase the TLSF memory pool")
      .arg("size", "size in bytes to add to pool");
#endif
  }

  // Destructor
  LuaService::~LuaService()
  {
    os::MutexLock lock(m);
    lua_close(L);
#ifdef LUA_RTT_TLSF
    tlsf_rtt_free_mp(tlsf_inf);
    delete tlsf_inf;
#endif
  }


#ifdef LUA_RTT_TLSF
  bool LuaService::tlsf_incmem(unsigned int size)
  {
    return tlsf_rtt_incmem(tlsf_inf, size);
  }
#endif

  bool LuaService::exec_file(const std::string &file)
  {
    os::MutexLock lock(m);
    if (luaL_dofile(L, file.c_str())) {
      const char* lua_error = lua_tostring(L, -1);
      Logger::log().logf(Logger::Error, "LuaService",
                         "LuaService '%s': %s",
                         this->getOwner()->getName().c_str(), lua_error ? lua_error : "<no Lua error>");
      return false;
    }
    return true;
  }

  bool LuaService::exec_str(const std::string &str)
  {
    os::MutexLock lock(m);
    if (luaL_dostring(L, str.c_str())) {
      const char* lua_error = lua_tostring(L, -1);
      Logger::log().logf(Logger::Error, "LuaService",
                         "LuaService '%s': %s",
                         this->getOwner()->getName().c_str(), lua_error ? lua_error : "<no Lua error>");
      return false;
    }
    return true;
  }

  LuaStateHandle LuaService::getLuaState()
  {
    return LuaStateHandle(L, m);
  }

} // namespace OCL

using namespace OCL;

#ifdef LUA_RTT_TLSF
 ORO_SERVICE_NAMED_PLUGIN(LuaService , "LuaTLSF")
#else
 ORO_SERVICE_NAMED_PLUGIN(LuaService , "Lua")
#endif
