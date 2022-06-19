#include "midi_sender.h"

#define AUPROC_CAPI_IMPLEMENT_GET_CAPI 1
#include "auproc_capi.h"

#define SENDER_CAPI_IMPLEMENT_GET_CAPI 1
#include "sender_capi.h"

/* ============================================================================================ */

static const char* const MIDI_SENDER_CLASS_NAME = "auproc.midi_sender";

static const char* ERROR_INVALID_MIDI_SENDER = "invalid auproc.midi_sender";

/* ============================================================================================ */

typedef struct MidiSenderUserData MidiSenderUserData;

struct MidiSenderUserData
{
    const char*        className;
    auproc_processor*  processor;

    bool               closed;
    bool               activated;
    
    const auproc_capi*  auprocCapi;
    auproc_engine*      auprocEngine;
    
    auproc_connector*      midiOutConnector;
    const auproc_midimeth* midiMethods;

    const sender_capi* senderCapi;
    sender_object*     sender;
    sender_reader*     senderReader;
    
    bool               hasNextEvent;
    uint32_t           nextEventFrame;
    sender_capi_value  senderValue;

    const void*        nextEventBytes;
    size_t             nextEventBytesCount;
};

/* ============================================================================================ */

static void setupMidiSenderMeta(lua_State* L);

static int pushMidiSenderMeta(lua_State* L)
{
    if (luaL_newmetatable(L, MIDI_SENDER_CLASS_NAME)) {
        setupMidiSenderMeta(L);
    }
    return 1;
}

/* ============================================================================================ */

static MidiSenderUserData* checkMidiSenderUdata(lua_State* L, int arg)
{
    MidiSenderUserData* udata = luaL_checkudata(L, arg, MIDI_SENDER_CLASS_NAME);
    if (udata->auprocCapi) {
        udata->auprocCapi->checkEngineIsNotClosed(L, udata->auprocEngine);
    }
    if (udata->closed) {
        luaL_error(L, ERROR_INVALID_MIDI_SENDER);
        return NULL;
    }
    return udata;
}

/* ============================================================================================ */

static int processCallback(uint32_t nframes, void* processorData)
{
    MidiSenderUserData* udata     = (MidiSenderUserData*) processorData;
    const auproc_capi*   auprocCapi = udata->auprocCapi;
    
    const auproc_midimeth* methods = udata->midiMethods;
    auproc_midibuf*        outBuf  = methods->getMidiBuffer(udata->midiOutConnector, nframes);
    
    methods->clearBuffer(outBuf);
    
    const sender_capi* senderCapi  =  udata->senderCapi;
    sender_object*     sender      =  udata->sender;
    sender_reader*     reader      =  udata->senderReader;
    sender_capi_value* senderValue = &udata->senderValue;

    uint32_t f0 = auprocCapi->getProcessBeginFrameTime(udata->auprocEngine);
    uint32_t f1 = f0 + nframes;

nextEvent:
    if (!udata->nextEventBytes) {
        int rc = senderCapi->nextMessageFromSender(sender, reader,
                                                   false /* nonblock */, 0 /* timeout */,
                                                   NULL /* errorHandler */, NULL /* errorHandlerData */);
        if (rc == 0) {
            senderCapi->nextValueFromReader(reader, senderValue);
            if (senderValue->type != SENDER_CAPI_TYPE_NONE) {
                bool hasT = false;
                uint32_t t = f0;
                if (senderValue->type == SENDER_CAPI_TYPE_INTEGER) {
                    hasT = true;
                    t = senderValue->intVal;
                } else if (senderValue->type == SENDER_CAPI_TYPE_NUMBER) {
                    hasT = true;
                    t = senderValue->numVal;
                }
                if (hasT) {
                    senderCapi->nextValueFromReader(reader, senderValue);
                }
                if (senderValue->type == SENDER_CAPI_TYPE_ARRAY) {
                    sender_array_type type = senderValue->arrayVal.type;
                    if (type == SENDER_UCHAR || type == SENDER_SCHAR) {
                        udata->nextEventFrame      = t;
                        udata->nextEventBytes      = senderValue->arrayVal.data;
                        udata->nextEventBytesCount = senderValue->arrayVal.elementCount;
                    }
                }
                else if (senderValue->type == SENDER_CAPI_TYPE_STRING) {
                        udata->nextEventFrame      = t;
                        udata->nextEventBytes      = senderValue->strVal.ptr;
                        udata->nextEventBytesCount = senderValue->strVal.len;
                } 
                if (!udata->nextEventBytes) {
                    senderCapi->clearReader(reader);
                }
            }
        }
    }

    if (udata->nextEventBytes) {
        uint32_t f  = udata->nextEventFrame;
        if (f < f1) {
            if (f >= f0) {
                unsigned char* data = methods->reserveMidiEvent(outBuf, f-f0, udata->nextEventBytesCount);
                if (data) {
                    memcpy(data, udata->nextEventBytes, udata->nextEventBytesCount);
                }
                f0 = f;
            }
            senderCapi->clearReader(reader);
            udata->nextEventBytes = NULL;
            goto nextEvent;
        }
    }
    return 0;
}

/* ============================================================================================ */

static void engineClosedCallback(void* processorData)
{
    MidiSenderUserData* udata = (MidiSenderUserData*) processorData;
 
    udata->closed    = true;
    udata->activated = false;
}

static void engineReleasedCallback(void* processorData)
{
    MidiSenderUserData* udata = (MidiSenderUserData*) processorData;
 
    udata->closed       = true;
    udata->activated    = false;
    udata->auprocCapi    = NULL;
    udata->auprocEngine  = NULL;
}

/* ============================================================================================ */

static int MidiSender_new(lua_State* L)
{
    const int conArg  = 1;
    const int sndrArg = 2;
    MidiSenderUserData* udata = lua_newuserdata(L, sizeof(MidiSenderUserData));
    memset(udata, 0, sizeof(MidiSenderUserData));
    udata->className = MIDI_SENDER_CLASS_NAME;
    pushMidiSenderMeta(L);                                /* -> udata, meta */
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
    const char* processorName = lua_pushfstring(L, "%s: %p", MIDI_SENDER_CLASS_NAME, udata);   /* -> udata, name */
    
    auproc_con_reg conReg = {AUPROC_MIDI, AUPROC_OUT, NULL};
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
            return luaL_argerror(L, conArg, "expected MIDI OUT connector");
        }
        else {
            return luaL_error(L, "cannot register processor (err=%d)", regError.errorType);
        }
    }
 
    udata->processor        = proc;
    udata->activated        = false;
    udata->auprocCapi       = capi;
    udata->auprocEngine     = engine;
    udata->midiOutConnector = conReg.connector;
    udata->midiMethods      = conReg.midiMethods;
    return 1;
}

/* ============================================================================================ */

static int MidiSender_release(lua_State* L)
{
    MidiSenderUserData* udata = luaL_checkudata(L, 1, MIDI_SENDER_CLASS_NAME);
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

static int MidiSender_toString(lua_State* L)
{
    MidiSenderUserData* udata = luaL_checkudata(L, 1, MIDI_SENDER_CLASS_NAME);

    lua_pushfstring(L, "%s: %p", MIDI_SENDER_CLASS_NAME, udata);

    return 1;
}

/* ============================================================================================ */

static int MidiSender_activate(lua_State* L)
{
    MidiSenderUserData* udata = checkMidiSenderUdata(L, 1);
    if (!udata->activated) {    
        udata->auprocCapi->activateProcessor(L, udata->auprocEngine, udata->processor);
        udata->activated = true;
    }
    return 0;
}

/* ============================================================================================ */

static int MidiSender_deactivate(lua_State* L)
{
    MidiSenderUserData* udata = checkMidiSenderUdata(L, 1);
    if (udata->activated) {                                           
        udata->auprocCapi->deactivateProcessor(L, udata->auprocEngine, udata->processor);
        udata->activated = false;
    }
    return 0;
}

/* ============================================================================================ */

static const luaL_Reg MidiSenderMethods[] = 
{
    { "activate",    MidiSender_activate },
    { "deactivate",  MidiSender_deactivate },
    { "close",       MidiSender_release },
    { NULL,          NULL } /* sentinel */
};

static const luaL_Reg MidiSenderMetaMethods[] = 
{
    { "__tostring", MidiSender_toString },
    { "__gc",       MidiSender_release  },

    { NULL,       NULL } /* sentinel */
};

static const luaL_Reg ModuleFunctions[] = 
{
    { "new_midi_sender", MidiSender_new },
    { NULL,              NULL } /* sentinel */
};

/* ============================================================================================ */

static void setupMidiSenderMeta(lua_State* L)
{                                                          /* -> meta */
    lua_pushstring(L, MIDI_SENDER_CLASS_NAME);        /* -> meta, className */
    lua_setfield(L, -2, "__metatable");                    /* -> meta */

    luaL_setfuncs(L, MidiSenderMetaMethods, 0);     /* -> meta */
    
    lua_newtable(L);                                       /* -> meta, MidiSenderClass */
    luaL_setfuncs(L, MidiSenderMethods, 0);         /* -> meta, MidiSenderClass */
    lua_setfield (L, -2, "__index");                       /* -> meta */
}


/* ============================================================================================ */

int auproc_midi_sender_init_module(lua_State* L, int module)
{
    if (luaL_newmetatable(L, MIDI_SENDER_CLASS_NAME)) {
        setupMidiSenderMeta(L);
    }
    lua_pop(L, 1);
    
    lua_pushvalue(L, module);
        luaL_setfuncs(L, ModuleFunctions, 0);
    lua_pop(L, 1);

    return 0;
}

/* ============================================================================================ */

