#include "midi_receiver.h"

#define AUPROC_CAPI_IMPLEMENT_GET_CAPI 1
#include "auproc_capi.h"

#define RECEIVER_CAPI_IMPLEMENT_GET_CAPI 1
#include "receiver_capi.h"

/* ============================================================================================ */

static const char* const MIDI_RECEIVER_CLASS_NAME = "auproc.midi_receiver";

static const char* ERROR_INVALID_MIDI_RECEIVER = "invalid auproc.midi_receiver";

/* ============================================================================================ */

typedef struct MidiReceiverUserData MidiReceiverUserData;

struct MidiReceiverUserData
{
    const char*           className;
    auproc_processor* processor;
    
    bool               closed;
    bool               activated;
    
    const auproc_capi*  auprocCapi;
    auproc_engine*      auprocEngine;

    auproc_connector*      midiInConnector;
    const auproc_midimeth* midiMethods;
    
    const receiver_capi* receiverCapi;
    receiver_object*     receiver;
    receiver_writer*     receiverWriter;
};

/* ============================================================================================ */

static void setupMidiReceiverMeta(lua_State* L);

static int pushMidiReceiverMeta(lua_State* L)
{
    if (luaL_newmetatable(L, MIDI_RECEIVER_CLASS_NAME)) {
        setupMidiReceiverMeta(L);
    }
    return 1;
}

/* ============================================================================================ */

static MidiReceiverUserData* checkMidiReceiverUdata(lua_State* L, int arg)
{
    MidiReceiverUserData* udata        = luaL_checkudata(L, arg, MIDI_RECEIVER_CLASS_NAME);
    const auproc_capi*    auprocCapi   = udata->auprocCapi;
    auproc_engine*        auprocEngine = udata->auprocEngine;

    if (auprocCapi) {
        auprocCapi->checkEngineIsNotClosed(L, auprocEngine);
    }
    if (udata->closed) {
        luaL_error(L, ERROR_INVALID_MIDI_RECEIVER);
        return NULL;
    }
    return udata;
}

/* ============================================================================================ */

static int processCallback(uint32_t nframes, void* processorData)
{
    MidiReceiverUserData* udata        = (MidiReceiverUserData*) processorData;
    const auproc_capi*    auprocCapi   = udata->auprocCapi;
    auproc_engine*        auprocEngine = udata->auprocEngine;

    const auproc_midimeth* methods = udata->midiMethods;
    auproc_midibuf*        inBuf   = methods->getMidiBuffer(udata->midiInConnector, nframes);
    
    
    auproc_midi_event in_event;
    uint32_t  event_index = 0;
    uint32_t  event_count = methods->getEventCount(inBuf);
    
    const receiver_capi* receiverCapi = udata->receiverCapi;
    receiver_object*     receiver     = udata->receiver;
    receiver_writer*     writer       = udata->receiverWriter;

    uint32_t t0 = auprocCapi->getProcessBeginFrameTime(auprocEngine);
    if (receiver) {
        for (int i = 0; i < event_count; ++i) {
            methods->getMidiEvent(&in_event, inBuf, i);
            size_t s = in_event.size;
            if (s > 0) {
                int rc = receiverCapi->addIntegerToWriter(writer, t0 + in_event.time);
                unsigned char* data = NULL;
                if (rc == 0) {
                    data = receiverCapi->addArrayToWriter(writer, RECEIVER_UCHAR, in_event.size);
                }
                if (data) {
                    memcpy(data, in_event.buffer, in_event.size);
                    rc = receiverCapi->msgToReceiver(receiver, writer, false /* clear */, false /* nonblock */, 
                                                     NULL /* error handler */, NULL /* error handler data */);
                } 
                if (!data || rc != 0) {
                    receiverCapi->clearWriter(writer);
                }
            }
        }
    }
    
    return 0;
}

/* ============================================================================================ */

static void engineClosedCallback(void* processorData)
{
    MidiReceiverUserData* udata = (MidiReceiverUserData*) processorData;
 
    udata->closed     = true;
    udata->activated  = false;
}

static void engineReleasedCallback(void* processorData)
{
    MidiReceiverUserData* udata = (MidiReceiverUserData*) processorData;
 
    udata->closed      = true;
    udata->activated   = false;
    udata->auprocCapi   = NULL;
    udata->auprocEngine = NULL;
}

/* ============================================================================================ */

static int MidiReceiver_new(lua_State* L)
{
    const int conArg = 1;
    const int recvArg = 2;
    MidiReceiverUserData* udata = lua_newuserdata(L, sizeof(MidiReceiverUserData));
    memset(udata, 0, sizeof(MidiReceiverUserData));
    udata->className = MIDI_RECEIVER_CLASS_NAME;
    pushMidiReceiverMeta(L);                                /* -> udata, meta */
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
    const char* processorName = lua_pushfstring(L, "%s: %p", MIDI_RECEIVER_CLASS_NAME, udata);   /* -> udata, name */
    
    auproc_con_reg conReg = {AUPROC_MIDI, AUPROC_IN, NULL};
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
            return luaL_argerror(L, conArg, "expected MIDI IN connector");
        }
        else {
            return luaL_error(L, "cannot register processor (err=%d)", regError.errorType);
        }
    }
    udata->processor       = proc;
    udata->activated       = false;
    udata->auprocCapi      = capi;
    udata->auprocEngine    = engine;
    udata->midiInConnector = conReg.connector;
    udata->midiMethods     = conReg.midiMethods;
    return 1;
}

/* ============================================================================================ */

static int MidiReceiver_release(lua_State* L)
{
    MidiReceiverUserData* udata = luaL_checkudata(L, 1, MIDI_RECEIVER_CLASS_NAME);
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

static int MidiReceiver_toString(lua_State* L)
{
    MidiReceiverUserData* udata = luaL_checkudata(L, 1, MIDI_RECEIVER_CLASS_NAME);

    lua_pushfstring(L, "%s: %p", MIDI_RECEIVER_CLASS_NAME, udata);

    return 1;
}

/* ============================================================================================ */

static int MidiReceiver_activate(lua_State* L)
{
    MidiReceiverUserData* udata = checkMidiReceiverUdata(L, 1);
    if (!udata->activated) {    
        udata->auprocCapi->activateProcessor(L, udata->auprocEngine, udata->processor);
        udata->activated = true;
    }
    return 0;
}

/* ============================================================================================ */

static int MidiReceiver_deactivate(lua_State* L)
{
    MidiReceiverUserData* udata = checkMidiReceiverUdata(L, 1);
    if (udata->activated) {                                           
        udata->auprocCapi->deactivateProcessor(L, udata->auprocEngine, udata->processor);
        udata->activated = false;
    }
    return 0;
}

/* ============================================================================================ */

static const luaL_Reg MidiReceiverMethods[] = 
{
    { "activate",    MidiReceiver_activate },
    { "deactivate",  MidiReceiver_deactivate },
    { "close",       MidiReceiver_release },
    { NULL,          NULL } /* sentinel */
};

static const luaL_Reg MidiReceiverMetaMethods[] = 
{
    { "__tostring", MidiReceiver_toString },
    { "__gc",       MidiReceiver_release  },

    { NULL,       NULL } /* sentinel */
};

static const luaL_Reg ModuleFunctions[] = 
{
    { "new_midi_receiver", MidiReceiver_new },
    { NULL,                NULL } /* sentinel */
};

/* ============================================================================================ */

static void setupMidiReceiverMeta(lua_State* L)
{                                                          /* -> meta */
    lua_pushstring(L, MIDI_RECEIVER_CLASS_NAME);        /* -> meta, className */
    lua_setfield(L, -2, "__metatable");                    /* -> meta */

    luaL_setfuncs(L, MidiReceiverMetaMethods, 0);     /* -> meta */
    
    lua_newtable(L);                                       /* -> meta, MidiReceiverClass */
    luaL_setfuncs(L, MidiReceiverMethods, 0);         /* -> meta, MidiReceiverClass */
    lua_setfield (L, -2, "__index");                       /* -> meta */
}


/* ============================================================================================ */

int auproc_midi_receiver_init_module(lua_State* L, int module)
{
    if (luaL_newmetatable(L, MIDI_RECEIVER_CLASS_NAME)) {
        setupMidiReceiverMeta(L);
    }
    lua_pop(L, 1);
    
    lua_pushvalue(L, module);
        luaL_setfuncs(L, ModuleFunctions, 0);
    lua_pop(L, 1);

    return 0;
}

/* ============================================================================================ */

