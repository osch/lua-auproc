#include "audio_mixer.h"

#define AUPROC_CAPI_IMPLEMENT_GET_CAPI 1
#include "auproc_capi.h"

#define SENDER_CAPI_IMPLEMENT_GET_CAPI 1
#include "sender_capi.h"

/* ============================================================================================ */

static const char* const AUDIO_MIXER_CLASS_NAME = "auproc.audio_mixer";

static const char* ERROR_INVALID_AUDIO_MIXER = "invalid auproc.audio_mixer";

/* ============================================================================================ */

typedef struct InputConnection InputConnection;
typedef struct AudioMixerUserData AudioMixerUserData;

struct InputConnection 
{
    auproc_connector*       connector;
    const auproc_audiometh* methods;
    float                   factor;
};

struct AudioMixerUserData
{
    const char*        className;
    auproc_processor*  processor;

    auproc_con_reg*         connectorRegs;
    InputConnection*        inpConnections;
    int                     inpConnectionsCount;
    auproc_connector*       outConnector;
    const auproc_audiometh* outMethods;

    bool               closed;
    bool               activated;
    
    const auproc_capi*   auprocCapi;
    auproc_engine*       auprocEngine;
    
    const sender_capi*   senderCapi;
    sender_object*       sender;
    sender_reader*       senderReader;
};

/* ============================================================================================ */

static void setupAudioMixerMeta(lua_State* L);

static int pushAudioMixerMeta(lua_State* L)
{
    if (luaL_newmetatable(L, AUDIO_MIXER_CLASS_NAME)) {
        setupAudioMixerMeta(L);
    }
    return 1;
}

/* ============================================================================================ */

static AudioMixerUserData* checkAudioMixerUdata(lua_State* L, int arg)
{
    AudioMixerUserData* udata = luaL_checkudata(L, arg, AUDIO_MIXER_CLASS_NAME);
    if (udata->auprocCapi) {
        udata->auprocCapi->checkEngineIsNotClosed(L, udata->auprocEngine);
    }
    if (udata->closed) {
        luaL_error(L, ERROR_INVALID_AUDIO_MIXER);
        return NULL;
    }
    return udata;
}

/* ============================================================================================ */

static int processCallback(uint32_t nframes, void* processorData)
{
    AudioMixerUserData* udata  = (AudioMixerUserData*) processorData;
    InputConnection*    inputs = udata->inpConnections;
    int                 n      = udata->inpConnectionsCount;

    if (udata->sender)
    {
        const sender_capi*   senderCapi = udata->senderCapi;
        sender_reader*       reader     = udata->senderReader;

    nextMsg:;
        int rc = senderCapi->nextMessageFromSender(udata->sender, reader,
                                                   true /* nonblock */, 0 /* timeout */,
                                                   NULL /* errorHandler */, NULL /* errorHandlerData */);
        if (rc == 0) {
            sender_capi_value  senderValue;
        nextValues:
            senderCapi->nextValueFromReader(reader, &senderValue);
            if (senderValue.type != SENDER_CAPI_TYPE_NONE) {
                bool hasValue = false;
                int inputIndex;
                if (senderValue.type == SENDER_CAPI_TYPE_INTEGER) {
                    hasValue = true;
                    inputIndex = senderValue.intVal;
                } else if (senderValue.type == SENDER_CAPI_TYPE_NUMBER) {
                    hasValue = true;
                    inputIndex = (int)senderValue.numVal;
                }
                if (hasValue) {
                    senderCapi->nextValueFromReader(reader, &senderValue);
                    hasValue = false;
                    lua_Number factor;
                    if (senderValue.type == SENDER_CAPI_TYPE_INTEGER) {
                        hasValue = true;
                        factor = senderValue.intVal;
                    } else if (senderValue.type == SENDER_CAPI_TYPE_NUMBER) {
                        hasValue = true;
                        factor = senderValue.numVal;
                    }
                    if (hasValue) {
                        inputs[inputIndex - 1].factor = factor;
                        goto nextValues;
                    }
                }
            }
            goto nextMsg;
        }
    }
    {
        const auproc_capi*      capi       = udata->auprocCapi;
        const auproc_audiometh* outMethods = udata->outMethods;
            
        float* outBuf    = outMethods->getAudioBuffer(udata->outConnector, nframes);
        float* outputEnd = outBuf + nframes;
        {
            float   factor   = inputs[0].factor;
            float*  firstIn  = inputs[0].methods->getAudioBuffer(inputs[0].connector, nframes);
            float*  output   = outBuf;
            while (output < outputEnd) {
                *(output++) = *(firstIn++) * factor;
            }
            
        }
        for (int i = 1; i < n; ++i) {
            float   factor   = inputs[i].factor;
            float*  input    = inputs[i].methods->getAudioBuffer(inputs[i].connector, nframes);
            float*  output   = outBuf;
            while (output < outputEnd) {
                *(output++) += *(input++) * factor;
            }
        }
    }
    return 0;
}

/* ============================================================================================ */

static void engineClosedCallback(void* processorData)
{
    AudioMixerUserData* udata = (AudioMixerUserData*) processorData;
 
    udata->closed    = true;
    udata->activated = false;
}

static void engineReleasedCallback(void* processorData)
{
    AudioMixerUserData* udata = (AudioMixerUserData*) processorData;
 
    udata->closed      = true;
    udata->activated   = false;
    udata->auprocCapi   = NULL;
    udata->auprocEngine = NULL;
}

/* ============================================================================================ */

static int AudioMixer_new(lua_State* L)
{
    const int firstArg = 1;
    const int lastArg  = lua_gettop(L);
    
    AudioMixerUserData* udata = lua_newuserdata(L, sizeof(AudioMixerUserData));
    memset(udata, 0, sizeof(AudioMixerUserData));
    udata->className = AUDIO_MIXER_CLASS_NAME;
    pushAudioMixerMeta(L);                                /* -> udata, meta */
    lua_setmetatable(L, -2);                                /* -> udata */
    int versionError = 0;
    const auproc_capi* capi = auproc_get_capi(L, firstArg, &versionError);
    auproc_engine* engine = NULL;
    if (capi) {
        engine = capi->getEngine(L, firstArg, NULL);
    }
    if (!capi || !engine) {
        if (versionError) {
            return luaL_argerror(L, firstArg, "auproc capi version mismatch");
        } else {
            return luaL_argerror(L, firstArg, "expected connector object");
        }
    }
    const int firstConArg = (capi->getObjectType(L, firstArg) == AUPROC_TENGINE) ? firstArg + 1 : firstArg;
    int lastConArg = lastArg;
    for (int i = firstConArg; i <= lastArg; ++i) {
        if (!capi->getConnectorType(L, i)) {
            lastConArg = i - 1;
            break;
        }
    }
    const int outConArg = lastConArg;
    const int senderArg = lastConArg + 1;

    if (firstConArg + 1 > lastConArg) {
        luaL_argerror(L, firstConArg, "expected at least two auproc connector objects");
    } 

    if (senderArg <= lastArg)
    {
        int errReason = 0;
        const sender_capi* senderCapi = sender_get_capi(L, senderArg, &errReason);
        sender_object*     sender     = senderCapi ? senderCapi->toSender(L, senderArg) : NULL;

        if (!senderCapi || !sender) {
            if (errReason == 1) {
                return luaL_argerror(L, senderArg, "sender capi version mismatch");
            } else {
                return luaL_argerror(L, senderArg, "expected sender capi object");
            }
        }
        
        udata->senderCapi = senderCapi;
        udata->sender     = sender;
        senderCapi->retainSender(sender);
        
        udata->senderReader = senderCapi->newReader(16 * 1024, 1);
        if (!udata->senderReader) {
            return luaL_error(L, "out of memory");
        }
    }
    const char* processorName = lua_pushfstring(L, "%s: %p", AUDIO_MIXER_CLASS_NAME, udata);   /* -> udata, name */

    const int conCount = lastConArg - firstConArg + 1;
    auproc_con_reg*  conRegs        = malloc(sizeof(auproc_con_reg)  * conCount);
    InputConnection* inpConnections = malloc(sizeof(InputConnection) * (conCount - 1));
    if (!conRegs || !inpConnections) {
        if (conRegs)        free(conRegs);
        if (inpConnections) free(inpConnections);
        return luaL_error(L, "out of memory");
    }
    memset(conRegs,        0, sizeof(auproc_con_reg)  * conCount);
    memset(inpConnections, 0, sizeof(InputConnection) * (conCount - 1));
    udata->connectorRegs       = conRegs;
    udata->inpConnections      = inpConnections;
    udata->inpConnectionsCount = conCount - 1;

    const auproc_con_reg inConReg  = {AUPROC_AUDIO, AUPROC_IN,  NULL};
    const auproc_con_reg outConReg = {AUPROC_AUDIO, AUPROC_OUT, NULL};
    
    for (int i = 0; i < conCount - 1; ++i) {
        conRegs[i] = inConReg;
    }
    conRegs[conCount - 1] = outConReg;
    
    auproc_con_reg_err regError = {0};
    auproc_processor* proc = capi->registerProcessor(L, firstConArg, conCount, engine, processorName, udata, 
                                                         processCallback, NULL, engineClosedCallback, engineReleasedCallback,
                                                         conRegs, &regError);
    lua_pop(L, 1); /* -> udata */

    if (!proc)
    {
        if (regError.conIndex >= 0) 
        {
            int errArg = firstConArg + regError.conIndex;
         
            if (regError.errorType == AUPROC_REG_ERR_CONNCTOR_INVALID) 
            {
                return luaL_argerror(L, errArg, "invalid connector object");
            }
            if (   regError.errorType == AUPROC_REG_ERR_ENGINE_MISMATCH) 
            {
                const char* msg = lua_pushfstring(L, "connector belongs to other %s", 
                                                     capi->engine_category_name);
                return luaL_argerror(L, errArg, msg);
            }
            if (regError.errorType == AUPROC_REG_ERR_ARG_INVALID
             || regError.errorType == AUPROC_REG_ERR_WRONG_CONNECTOR_TYPE)
            {
                if (errArg < lastConArg) {
                    return luaL_argerror(L, errArg, "expected AUDIO IN connector");
                } 
                if (errArg == lastConArg) {
                    return luaL_argerror(L, errArg, "expected AUDIO OUT connector");
                }
            }
            if (regError.errorType == AUPROC_REG_ERR_WRONG_DIRECTION)
            {
                if (errArg < lastConArg) {
                    return luaL_argerror(L, errArg, "given connector is not readable");
                } 
                if (errArg == lastConArg) {
                    return luaL_argerror(L, errArg, "given connector is not writable");
                }
            }
        }
        return luaL_error(L, "cannot register processor (err=%d)", regError.errorType);
    }
 
    udata->processor   = proc;
    udata->activated   = false;
    udata->auprocCapi   = capi;
    udata->auprocEngine = engine;
    
    for (int i = 0; i < conCount - 1; ++i) {
        udata->inpConnections[i].connector = conRegs[i].connector;
        udata->inpConnections[i].factor    = 1.0;
        udata->inpConnections[i].methods   = conRegs[i].audioMethods;
    }
    udata->outConnector = conRegs[conCount - 1].connector;
    udata->outMethods   = conRegs[conCount - 1].audioMethods;

    return 1;
}

/* ============================================================================================ */

static int AudioMixer_release(lua_State* L)
{
    AudioMixerUserData* udata = luaL_checkudata(L, 1, AUDIO_MIXER_CLASS_NAME);
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
    if (udata->connectorRegs) {
        free(udata->connectorRegs);
        udata->connectorRegs = NULL;
    }
    if (udata->inpConnections) {
        free(udata->inpConnections);
        udata->inpConnections = NULL;
        udata->inpConnectionsCount = 0;
    }
    return 0;
}

/* ============================================================================================ */

static int AudioMixer_toString(lua_State* L)
{
    AudioMixerUserData* udata = luaL_checkudata(L, 1, AUDIO_MIXER_CLASS_NAME);

    lua_pushfstring(L, "%s: %p", AUDIO_MIXER_CLASS_NAME, udata);

    return 1;
}

/* ============================================================================================ */

static int AudioMixer_activate(lua_State* L)
{
    AudioMixerUserData* udata = checkAudioMixerUdata(L, 1);
    if (!udata->activated) {    
        udata->auprocCapi->activateProcessor(L, udata->auprocEngine, udata->processor);
        udata->activated = true;
    }
    return 0;
}

/* ============================================================================================ */

static int AudioMixer_deactivate(lua_State* L)
{
    AudioMixerUserData* udata = checkAudioMixerUdata(L, 1);
    if (udata->activated) {                                           
        udata->auprocCapi->deactivateProcessor(L, udata->auprocEngine, udata->processor);
        udata->activated = false;
    }
    return 0;
}

/* ============================================================================================ */

static const luaL_Reg AudioMixerMethods[] = 
{
    { "activate",    AudioMixer_activate },
    { "deactivate",  AudioMixer_deactivate },
    { "close",       AudioMixer_release },
    { NULL,          NULL } /* sentinel */
};

static const luaL_Reg AudioMixerMetaMethods[] = 
{
    { "__tostring", AudioMixer_toString },
    { "__gc",       AudioMixer_release  },

    { NULL,       NULL } /* sentinel */
};

static const luaL_Reg ModuleFunctions[] = 
{
    { "new_audio_mixer", AudioMixer_new },
    { NULL,                NULL } /* sentinel */
};

/* ============================================================================================ */

static void setupAudioMixerMeta(lua_State* L)
{                                                          /* -> meta */
    lua_pushstring(L, AUDIO_MIXER_CLASS_NAME);       /* -> meta, className */
    lua_setfield(L, -2, "__metatable");                    /* -> meta */

    luaL_setfuncs(L, AudioMixerMetaMethods, 0);       /* -> meta */
    
    lua_newtable(L);                                       /* -> meta, AudioMixerClass */
    luaL_setfuncs(L, AudioMixerMethods, 0);           /* -> meta, AudioMixerClass */
    lua_setfield (L, -2, "__index");                       /* -> meta */
}


/* ============================================================================================ */

int auproc_audio_mixer_init_module(lua_State* L, int module)
{
    if (luaL_newmetatable(L, AUDIO_MIXER_CLASS_NAME)) {
        setupAudioMixerMeta(L);
    }
    lua_pop(L, 1);
    
    lua_pushvalue(L, module);
        luaL_setfuncs(L, ModuleFunctions, 0);
    lua_pop(L, 1);

    return 0;
}

/* ============================================================================================ */

