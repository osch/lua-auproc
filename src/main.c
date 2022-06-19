#include "main.h"

#include "midi_sender.h"
#include "midi_receiver.h"
#include "midi_mixer.h"

#include "audio_sender.h"
#include "audio_receiver.h"
#include "audio_mixer.h"

/* ============================================================================================ */

#ifndef AUPROC_VERSION
    #error AUPROC_VERSION is not defined
#endif 

#define AUPROC_STRINGIFY(x) #x
#define AUPROC_TOSTRING(x) AUPROC_STRINGIFY(x)
#define AUPROC_VERSION_STRING AUPROC_TOSTRING(AUPROC_VERSION)

const char* const AUPROC_MODULE_NAME = "auproc";

/* ============================================================================================ */

static const luaL_Reg ModuleFunctions[] = 
{
    { NULL,             NULL } /* sentinel */
};

/* ============================================================================================ */

DLL_PUBLIC int luaopen_auproc(lua_State* L)
{
    luaL_checkversion(L); /* does nothing if compiled for Lua 5.1 */

    /* ---------------------------------------- */

    int n = lua_gettop(L);
    
    int module      = ++n; lua_newtable(L);
    int errorModule = ++n; lua_newtable(L);

    lua_pushvalue(L, module);
        luaL_setfuncs(L, ModuleFunctions, 0);
    lua_pop(L, 1);
    
    lua_pushliteral(L, AUPROC_VERSION_STRING);
    lua_setfield(L, module, "_VERSION");
    
    lua_checkstack(L, LUA_MINSTACK);
    
    auproc_midi_sender_init_module   (L, module);
    auproc_midi_receiver_init_module (L, module);
    auproc_midi_mixer_init_module    (L, module);

    auproc_audio_sender_init_module  (L, module);
    auproc_audio_receiver_init_module(L, module);
    auproc_audio_mixer_init_module   (L, module);
    
    lua_settop(L, module);
    return 1;
}

/* ============================================================================================ */
