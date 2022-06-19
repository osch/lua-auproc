#include "audio_receiver.h"

#define AUPROC_CAPI_IMPLEMENT_GET_CAPI 1
#include "auproc_capi.h"

#define RECEIVER_CAPI_IMPLEMENT_GET_CAPI 1
#include "receiver_capi.h"

/* ============================================================================================ */

static const char* const AUDIO_RECEIVER_CLASS_NAME = "auproc.audio_receiver";

static const char* ERROR_INVALID_AUDIO_RECEIVER = "invalid auproc.audio_receiver";

/* ============================================================================================ */

typedef struct AudioReceiverUserData AudioReceiverUserData;

struct AudioReceiverUserData
{
    const char*           className;
    auproc_processor*     processor;
    
    bool                  closed;
    bool                  activated;
    
    const auproc_capi*     auprocCapi;
    auproc_engine*         auprocEngine;

    auproc_connector*       audioInConnector;
    const auproc_audiometh* audioMethods;
    
    const receiver_capi* receiverCapi;
    receiver_object*     receiver;
    receiver_writer*     receiverWriter;
};

/* ============================================================================================ */

static void setupAudioReceiverMeta(lua_State* L);

static int pushAudioReceiverMeta(lua_State* L)
{
    if (luaL_newmetatable(L, AUDIO_RECEIVER_CLASS_NAME)) {
        setupAudioReceiverMeta(L);
    }
    return 1;
}

/* ============================================================================================ */

static AudioReceiverUserData* checkAudioReceiverUdata(lua_State* L, int arg)
{
    AudioReceiverUserData* udata        = luaL_checkudata(L, arg, AUDIO_RECEIVER_CLASS_NAME);
    const auproc_capi*    auprocCapi   = udata->auprocCapi;
    auproc_engine*        auprocEngine = udata->auprocEngine;

    if (auprocCapi) {
        auprocCapi->checkEngineIsNotClosed(L, auprocEngine);
    }
    if (udata->closed) {
        luaL_error(L, ERROR_INVALID_AUDIO_RECEIVER);
        return NULL;
    }
    return udata;
}

/* ============================================================================================ */

static int processCallback(uint32_t nframes, void* processorData)
{
    AudioReceiverUserData* udata        = (AudioReceiverUserData*) processorData;
    const auproc_capi*    auprocCapi   = udata->auprocCapi;
    auproc_engine*        auprocEngine = udata->auprocEngine;

    const auproc_audiometh* methods = udata->audioMethods;
    float*                  inBuf   = methods->getAudioBuffer(udata->audioInConnector, nframes);
    
    const receiver_capi* receiverCapi = udata->receiverCapi;
    receiver_object*     receiver     = udata->receiver;
    receiver_writer*     writer       = udata->receiverWriter;

    uint32_t t0 = auprocCapi->getProcessBeginFrameTime(auprocEngine);
    if (receiver) {
        int rc = receiverCapi->addIntegerToWriter(writer, t0);
        unsigned char* data = NULL;
        if (rc == 0) {
            data = receiverCapi->addArrayToWriter(writer, RECEIVER_FLOAT, nframes);
        }
        if (data) {
            memcpy(data, inBuf, nframes * sizeof(float));
            rc = receiverCapi->msgToReceiver(receiver, writer, false /* clear */, false /* nonblock */, 
                                             NULL /* error handler */, NULL /* error handler data */);
        }
        if (!data || rc != 0) {
            receiverCapi->clearWriter(writer);
        }
    }
    
    return 0;
}

/* ============================================================================================ */

static void engineClosedCallback(void* processorData)
{
    AudioReceiverUserData* udata = (AudioReceiverUserData*) processorData;
 
    udata->closed     = true;
    udata->activated  = false;
}

static void engineReleasedCallback(void* processorData)
{
    AudioReceiverUserData* udata = (AudioReceiverUserData*) processorData;
 
    udata->closed      = true;
    udata->activated   = false;
    udata->auprocCapi   = NULL;
    udata->auprocEngine = NULL;
}

/* ============================================================================================ */

static int AudioReceiver_new(lua_State* L)
{
    const int conArg = 1;
    const int recvArg = 2;
    AudioReceiverUserData* udata = lua_newuserdata(L, sizeof(AudioReceiverUserData));
    memset(udata, 0, sizeof(AudioReceiverUserData));
    udata->className = AUDIO_RECEIVER_CLASS_NAME;
    pushAudioReceiverMeta(L);                                /* -> udata, meta */
    lua_setmetatable(L, -2);                                /* -> udata */
    int versionError = 0;
    const auproc_capi* capi = auproc_get_capi(L, conArg, &versionError);
    auproc_engine* engine = NULL;
    if (capi) {
        engine = capi->getEngine(L, conArg, NULL);
    }
    if (!capi || !engine) {
        if (versionError) {
            return luaL_argerror(L, conArg, "auproc version mismatch");
        } else {
            return luaL_argerror(L, conArg, "expected connector object");
        }
    }
    
    int errReason = 0;
    const receiver_capi* receiverCapi = receiver_get_capi(L, recvArg, &errReason);
    if (!receiverCapi) {
        if (errReason == 1) {
            return luaL_argerror(L, recvArg, "receiver capi version mismatch");
        } else {
            return luaL_argerror(L, recvArg, "expected object with receiver capi");
        }
    }
    receiver_object* receiver = receiverCapi->toReceiver(L, recvArg);
    if (!receiver) {
        return luaL_argerror(L, recvArg, "expected object with receiver capi");
    }
    udata->receiverCapi = receiverCapi;
    udata->receiver     = receiver;
    receiverCapi->retainReceiver(receiver);
    
    udata->receiverWriter = receiverCapi->newWriter(16 * 1024, 1);
    if (!udata->receiverWriter) {
        return luaL_error(L, "out of memory");
    }
    const char* processorName = lua_pushfstring(L, "%s: %p", AUDIO_RECEIVER_CLASS_NAME, udata);   /* -> udata, name */
    
    auproc_con_reg conReg = {AUPROC_AUDIO, AUPROC_IN, NULL};
    auproc_con_reg_err regError = {0};
    auproc_processor* proc = capi->registerProcessor(L, conArg, 1, engine, processorName, udata, 
                                                         processCallback, NULL, engineClosedCallback, engineReleasedCallback,
                                                         &conReg, &regError);
    lua_pop(L, 1); /* -> udata */

    if (!proc)
    {
        if (regError.errorType == AUPROC_REG_ERR_CONNCTOR_INVALID) {
            return luaL_argerror(L, conArg, "invalid connector object");
        }
        else if (regError.errorType == AUPROC_REG_ERR_ENGINE_MISMATCH) 
        {
            const char* msg = lua_pushfstring(L, "connector belongs to other %s", 
                                                 capi->engine_category_name);
            return luaL_argerror(L, conArg, msg);
        }
        else if (regError.errorType == AUPROC_REG_ERR_ARG_INVALID
              || regError.errorType == AUPROC_REG_ERR_WRONG_DIRECTION
              || regError.errorType == AUPROC_REG_ERR_WRONG_CONNECTOR_TYPE)
        {
            return luaL_argerror(L, conArg, "expected AUDIO IN connector");
        }
        else {
            return luaL_error(L, "cannot register processor (err=%d)", regError.errorType);
        }
    }
    udata->processor       = proc;
    udata->activated       = false;
    udata->auprocCapi      = capi;
    udata->auprocEngine    = engine;
    udata->audioInConnector = conReg.connector;
    udata->audioMethods     = conReg.audioMethods;
    return 1;
}

/* ============================================================================================ */

static int AudioReceiver_release(lua_State* L)
{
    AudioReceiverUserData* udata = luaL_checkudata(L, 1, AUDIO_RECEIVER_CLASS_NAME);
    udata->closed  = true;
    udata->activated  = false;
    if (udata->auprocCapi) {
        udata->auprocCapi->unregisterProcessor(L, udata->auprocEngine, udata->processor);
        udata->processor    = NULL;
        udata->auprocCapi    = NULL;
        udata->auprocEngine  = NULL;
    }
    if (udata->receiver) {
        if (udata->receiverWriter) {
            udata->receiverCapi->freeWriter(udata->receiverWriter);
            udata->receiverWriter = NULL;
        }
        udata->receiverCapi->releaseReceiver(udata->receiver);
        udata->receiver     = NULL;
        udata->receiverCapi = NULL;
    }
    return 0;
}

/* ============================================================================================ */

static int AudioReceiver_toString(lua_State* L)
{
    AudioReceiverUserData* udata = luaL_checkudata(L, 1, AUDIO_RECEIVER_CLASS_NAME);

    lua_pushfstring(L, "%s: %p", AUDIO_RECEIVER_CLASS_NAME, udata);

    return 1;
}

/* ============================================================================================ */

static int AudioReceiver_activate(lua_State* L)
{
    AudioReceiverUserData* udata = checkAudioReceiverUdata(L, 1);
    if (!udata->activated) {    
        udata->auprocCapi->activateProcessor(L, udata->auprocEngine, udata->processor);
        udata->activated = true;
    }
    return 0;
}

/* ============================================================================================ */

static int AudioReceiver_deactivate(lua_State* L)
{
    AudioReceiverUserData* udata = checkAudioReceiverUdata(L, 1);
    if (udata->activated) {                                           
        udata->auprocCapi->deactivateProcessor(L, udata->auprocEngine, udata->processor);
        udata->activated = false;
    }
    return 0;
}

/* ============================================================================================ */

static const luaL_Reg AudioReceiverMethods[] = 
{
    { "activate",    AudioReceiver_activate },
    { "deactivate",  AudioReceiver_deactivate },
    { "close",       AudioReceiver_release },
    { NULL,          NULL } /* sentinel */
};

static const luaL_Reg AudioReceiverMetaMethods[] = 
{
    { "__tostring", AudioReceiver_toString },
    { "__gc",       AudioReceiver_release  },

    { NULL,       NULL } /* sentinel */
};

static const luaL_Reg ModuleFunctions[] = 
{
    { "new_audio_receiver", AudioReceiver_new },
    { NULL,                NULL } /* sentinel */
};

/* ============================================================================================ */

static void setupAudioReceiverMeta(lua_State* L)
{                                                          /* -> meta */
    lua_pushstring(L, AUDIO_RECEIVER_CLASS_NAME);        /* -> meta, className */
    lua_setfield(L, -2, "__metatable");                    /* -> meta */

    luaL_setfuncs(L, AudioReceiverMetaMethods, 0);     /* -> meta */
    
    lua_newtable(L);                                       /* -> meta, AudioReceiverClass */
    luaL_setfuncs(L, AudioReceiverMethods, 0);         /* -> meta, AudioReceiverClass */
    lua_setfield (L, -2, "__index");                       /* -> meta */
}


/* ============================================================================================ */

int auproc_audio_receiver_init_module(lua_State* L, int module)
{
    if (luaL_newmetatable(L, AUDIO_RECEIVER_CLASS_NAME)) {
        setupAudioReceiverMeta(L);
    }
    lua_pop(L, 1);
    
    lua_pushvalue(L, module);
        luaL_setfuncs(L, ModuleFunctions, 0);
    lua_pop(L, 1);

    return 0;
}

/* ============================================================================================ */

