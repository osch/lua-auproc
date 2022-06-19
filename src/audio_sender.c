#include "audio_sender.h"

#define AUPROC_CAPI_IMPLEMENT_GET_CAPI 1
#include "auproc_capi.h"

#define SENDER_CAPI_IMPLEMENT_GET_CAPI 1
#include "sender_capi.h"

/* ============================================================================================ */

static const char* const AUDIO_SENDER_CLASS_NAME = "auproc.audio_sender";

static const char* ERROR_INVALID_AUDIO_SENDER = "invalid auproc.audio_sender";

/* ============================================================================================ */

typedef struct AudioSenderUserData AudioSenderUserData;

struct AudioSenderUserData
{
    const char*        className;
    auproc_processor*  processor;

    bool               closed;
    bool               activated;
    
    const auproc_capi*  auprocCapi;
    auproc_engine*      auprocEngine;
    
    auproc_connector*       audioOutConnector;
    const auproc_audiometh* audioMethods;

    const sender_capi* senderCapi;
    sender_object*     sender;
    sender_reader*     senderReader;

    bool               hasNextEvent;
    uint32_t           eventStartFrame;
    uint32_t           eventEndFrame;
    const float*       eventData;
    
    float* uuu;
};

/* ============================================================================================ */

static void setupAudioSenderMeta(lua_State* L);

static int pushAudioSenderMeta(lua_State* L)
{
    if (luaL_newmetatable(L, AUDIO_SENDER_CLASS_NAME)) {
        setupAudioSenderMeta(L);
    }
    return 1;
}

/* ============================================================================================ */

static AudioSenderUserData* checkAudioSenderUdata(lua_State* L, int arg)
{
    AudioSenderUserData* udata = luaL_checkudata(L, arg, AUDIO_SENDER_CLASS_NAME);
    if (udata->auprocCapi) {
        udata->auprocCapi->checkEngineIsNotClosed(L, udata->auprocEngine);
    }
    if (udata->closed) {
        luaL_error(L, ERROR_INVALID_AUDIO_SENDER);
        return NULL;
    }
    return udata;
}

/* ============================================================================================ */

static int processCallback(uint32_t nframes, void* processorData)
{
    AudioSenderUserData*    udata      = (AudioSenderUserData*) processorData;
    const auproc_capi*      auprocCapi = udata->auprocCapi;
    const auproc_audiometh* methods    = udata->audioMethods;
    
    float*  outBuf  = methods->getAudioBuffer(udata->audioOutConnector, nframes);
    udata->uuu = outBuf;
    
    memset(outBuf, 0, sizeof(float) * nframes);
    
    const sender_capi* senderCapi = udata->senderCapi;
    sender_object*     sender     = udata->sender;
    sender_reader*     reader     = udata->senderReader;

    uint32_t f0 = auprocCapi->getProcessBeginFrameTime(udata->auprocEngine);
    uint32_t f1 = f0 + nframes;

nextEvent:
    if (!udata->hasNextEvent) {
        int rc = senderCapi->nextMessageFromSender(sender, reader,
                                                   false /* nonblock */, 0 /* timeout */,
                                                   NULL /* errorHandler */, NULL /* errorHandlerData */);
        if (rc == 0) {
            sender_capi_value senderValue;
            senderCapi->nextValueFromReader(reader, &senderValue);
            if (senderValue.type != SENDER_CAPI_TYPE_NONE) {
                bool hasT = false;
                uint32_t t = f0;
                if (senderValue.type == SENDER_CAPI_TYPE_INTEGER) {
                    hasT = true;
                    t = senderValue.intVal;
                } else if (senderValue.type == SENDER_CAPI_TYPE_NUMBER) {
                    hasT = true;
                    t = senderValue.numVal;
                }
                if (hasT) {
                    senderCapi->nextValueFromReader(reader, &senderValue);
                }
                if (  senderValue.type == SENDER_CAPI_TYPE_ARRAY
                  &&  senderValue.arrayVal.type == SENDER_FLOAT
                  &&  senderValue.arrayVal.elementSize == sizeof(float))
                {
                    udata->hasNextEvent = true;
                    udata->eventStartFrame = t;
                    udata->eventEndFrame   = udata->eventStartFrame + senderValue.arrayVal.elementCount;
                    udata->eventData       = senderValue.arrayVal.data;
                } else {
                    senderCapi->clearReader(reader);
                }
            }
        }
    }

    if (udata->hasNextEvent) {
        uint32_t s  = udata->eventStartFrame;
        if (s < f0) {
            udata->eventData += (f0 - s);
            s = f0;
        }
        if (s < f1) {
            uint32_t e = udata->eventEndFrame;
            if (e > f1) {
                e = f1;
            }
            if (e > s) {
                memcpy(outBuf + (s - f0), udata->eventData, (e - s) * sizeof(float));
            }
            if (e < udata->eventEndFrame) {
                udata->eventData += (e - s);
                udata->eventStartFrame = e;
            } else {
                if (e > s) {
                    outBuf += (e - f0);
                    f0 = e;
                }
                senderCapi->clearReader(reader);
                udata->hasNextEvent = false;
                goto nextEvent;
            }
        }
    }
    return 0;
}

/* ============================================================================================ */

static void engineClosedCallback(void* processorData)
{
    AudioSenderUserData* udata = (AudioSenderUserData*) processorData;
 
    udata->closed    = true;
    udata->activated = false;
}

static void engineReleasedCallback(void* processorData)
{
    AudioSenderUserData* udata = (AudioSenderUserData*) processorData;
 
    udata->closed       = true;
    udata->activated    = false;
    udata->auprocCapi    = NULL;
    udata->auprocEngine  = NULL;
}

/* ============================================================================================ */

static int AudioSender_new(lua_State* L)
{
    const int conArg  = 1;
    const int sndrArg = 2;
    AudioSenderUserData* udata = lua_newuserdata(L, sizeof(AudioSenderUserData));
    memset(udata, 0, sizeof(AudioSenderUserData));
    udata->className = AUDIO_SENDER_CLASS_NAME;
    pushAudioSenderMeta(L);                                /* -> udata, meta */
    lua_setmetatable(L, -2);                              /* -> udata */
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
    const sender_capi* senderCapi = sender_get_capi(L, sndrArg, &errReason);
    if (!senderCapi) {
        if (errReason == 1) {
            return luaL_argerror(L, sndrArg, "sender capi version mismatch");
        } else {
            return luaL_argerror(L, sndrArg, "expected object with sender capi");
        }
    }
    sender_object* sender = senderCapi->toSender(L, sndrArg);
    if (!sender) {
        return luaL_argerror(L, sndrArg, "expected object with sender capi");
    }
    udata->senderCapi = senderCapi;
    udata->sender     = sender;
    senderCapi->retainSender(sender);
    
    udata->senderReader = senderCapi->newReader(16 * 1024, 1);
    if (!udata->senderReader) {
        return luaL_error(L, "out of memory");
    }
    const char* processorName = lua_pushfstring(L, "%s: %p", AUDIO_SENDER_CLASS_NAME, udata);   /* -> udata, name */
    
    auproc_con_reg conReg = {AUPROC_AUDIO, AUPROC_OUT, NULL};
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
            return luaL_argerror(L, conArg, "expected AUDIO OUT connector");
        }
        else {
            return luaL_error(L, "cannot register processor (err=%d)", regError.errorType);
        }
    }
 
    udata->processor        = proc;
    udata->activated        = false;
    udata->auprocCapi       = capi;
    udata->auprocEngine     = engine;
    udata->audioOutConnector = conReg.connector;
    udata->audioMethods      = conReg.audioMethods;
    return 1;
}

/* ============================================================================================ */

static int AudioSender_release(lua_State* L)
{
    AudioSenderUserData* udata = luaL_checkudata(L, 1, AUDIO_SENDER_CLASS_NAME);
    udata->closed    = true;
    udata->activated = false;
    if (udata->auprocCapi) {
        udata->auprocCapi->unregisterProcessor(L, udata->auprocEngine, udata->processor);
        udata->processor   = NULL;
        udata->auprocCapi   = NULL;
        udata->auprocEngine = NULL;
    }
    if (udata->sender) {
        if (udata->senderReader) {
            udata->senderCapi->freeReader(udata->senderReader);
            udata->senderReader = NULL;
        }
        udata->senderCapi->releaseSender(udata->sender);
        udata->sender     = NULL;
        udata->senderCapi = NULL;
    }
    return 0;
}

/* ============================================================================================ */

static int AudioSender_toString(lua_State* L)
{
    AudioSenderUserData* udata = luaL_checkudata(L, 1, AUDIO_SENDER_CLASS_NAME);

    lua_pushfstring(L, "%s: %p", AUDIO_SENDER_CLASS_NAME, udata);

    return 1;
}

/* ============================================================================================ */

static int AudioSender_activate(lua_State* L)
{
    AudioSenderUserData* udata = checkAudioSenderUdata(L, 1);
    if (!udata->activated) {    
        udata->auprocCapi->activateProcessor(L, udata->auprocEngine, udata->processor);
        udata->activated = true;
    }
    return 0;
}

/* ============================================================================================ */

static int AudioSender_deactivate(lua_State* L)
{
    AudioSenderUserData* udata = checkAudioSenderUdata(L, 1);
    if (udata->activated) {                                           
        udata->auprocCapi->deactivateProcessor(L, udata->auprocEngine, udata->processor);
        udata->activated = false;
    }
    return 0;
}

/* ============================================================================================ */

static int AudioSender_active(lua_State* L)
{
    AudioSenderUserData* udata = checkAudioSenderUdata(L, 1);
    lua_pushboolean(L, udata->activated);
    return 1;
}

/* ============================================================================================ */

static const luaL_Reg AudioSenderMethods[] = 
{
    { "activate",    AudioSender_activate },
    { "deactivate",  AudioSender_deactivate },
    { "close",       AudioSender_release },
    { "active",      AudioSender_active },
    { NULL,          NULL } /* sentinel */
};

static const luaL_Reg AudioSenderMetaMethods[] = 
{
    { "__tostring", AudioSender_toString },
    { "__gc",       AudioSender_release  },

    { NULL,       NULL } /* sentinel */
};

static const luaL_Reg ModuleFunctions[] = 
{
    { "new_audio_sender", AudioSender_new },
    { NULL,              NULL } /* sentinel */
};

/* ============================================================================================ */

static void setupAudioSenderMeta(lua_State* L)
{                                                          /* -> meta */
    lua_pushstring(L, AUDIO_SENDER_CLASS_NAME);        /* -> meta, className */
    lua_setfield(L, -2, "__metatable");                    /* -> meta */

    luaL_setfuncs(L, AudioSenderMetaMethods, 0);     /* -> meta */
    
    lua_newtable(L);                                       /* -> meta, AudioSenderClass */
    luaL_setfuncs(L, AudioSenderMethods, 0);         /* -> meta, AudioSenderClass */
    lua_setfield (L, -2, "__index");                       /* -> meta */
}


/* ============================================================================================ */

int auproc_audio_sender_init_module(lua_State* L, int module)
{
    if (luaL_newmetatable(L, AUDIO_SENDER_CLASS_NAME)) {
        setupAudioSenderMeta(L);
    }
    lua_pop(L, 1);
    
    lua_pushvalue(L, module);
        luaL_setfuncs(L, ModuleFunctions, 0);
    lua_pop(L, 1);

    return 0;
}

/* ============================================================================================ */

