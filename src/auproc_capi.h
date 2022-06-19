#ifndef AUPROC_CAPI_H
#define AUPROC_CAPI_H

#define AUPROC_CAPI_ID_STRING     "_capi_auproc"

#define AUPROC_CAPI_VERSION_MAJOR -3
#define AUPROC_CAPI_VERSION_MINOR  1
#define AUPROC_CAPI_VERSION_PATCH  0

#ifndef AUPROC_CAPI_IMPLEMENT_SET_CAPI
#  define AUPROC_CAPI_IMPLEMENT_SET_CAPI 0
#endif

#ifndef AUPROC_CAPI_IMPLEMENT_GET_CAPI
#  define AUPROC_CAPI_IMPLEMENT_GET_CAPI 0
#endif

#ifdef __cplusplus

extern "C" {

struct auproc_capi;
struct auproc_engine;
struct auproc_info;
struct auproc_connector;
struct auproc_processor;
struct auproc_midibuf;
struct auproc_midimeth;
struct auproc_audiometh;
struct auproc_con_reg;
struct auproc_con_reg_err;
struct auproc_midi_event;

#else /* __cplusplus */

typedef struct auproc_capi         auproc_capi;
typedef struct auproc_engine       auproc_engine;
typedef struct auproc_info         auproc_info;
typedef struct auproc_connector    auproc_connector;
typedef struct auproc_processor    auproc_processor;
typedef struct auproc_midibuf      auproc_midibuf;
typedef struct auproc_midimeth     auproc_midimeth;
typedef struct auproc_audiometh    auproc_audiometh;
typedef struct auproc_con_reg      auproc_con_reg;
typedef struct auproc_con_reg_err  auproc_con_reg_err;
typedef struct auproc_midi_event   auproc_midi_event;

typedef enum   auproc_reg_err_type  auproc_reg_err_type;
typedef enum   auproc_direction auproc_direction;
typedef enum   auproc_obj_type  auproc_obj_type;
typedef enum   auproc_con_type  auproc_con_type;

#endif /* ! __cplusplus */

enum auproc_direction
{
    AUPROC_NONE  = 0,
    AUPROC_IN    = 1,
    AUPROC_OUT   = 2
};

enum auproc_con_type
{
    AUPROC_AUDIO   = 1,
    AUPROC_MIDI    = 2,
};

enum auproc_obj_type
{
    AUPROC_TNONE       = 0,
    AUPROC_TENGINE     = 1,
    AUPROC_TCONNECTOR  = 2
};

enum auproc_reg_err_type 
{
    AUPROC_CAPI_REG_NO_ERROR = 0,
    
    /**
     * The invocation of registerProcessor has invalid parameters on the C side,
     * e.g. C pointers are NULL or invalid combination of flags. This error
     * cannot be fixed on the Lua side.
     */
    AUPROC_REG_ERR_CALL_INVALID = 1,
    
    /**
     * A given Lua object is not a auproc connector object.
     */
    AUPROC_REG_ERR_ARG_INVALID = 2,
    
    /**
     * A given auproc connector object is invalid, i.e. it was closed.
     */
    AUPROC_REG_ERR_CONNCTOR_INVALID = 3,
    
    /**
     * A given auproc connector object belongs to a different engine.
     */
    AUPROC_REG_ERR_ENGINE_MISMATCH = 4,
    
    /**
     * A given auproc connector object cannot fulfill the desired 
     * direction, IN / OUT.
     */
    AUPROC_REG_ERR_WRONG_DIRECTION = 5,
    
    /**
     * A given auproc connector object cannot fulfill the desired 
     * type requirements, AUDIO / MIDI.
     */
    AUPROC_REG_ERR_WRONG_CONNECTOR_TYPE = 6,
    
};

/**
 * Additional information about engine.
 */
struct auproc_info
{
    uint32_t sampleRate;
};

/**
 * Function pointers for handling audio connectors.
 */
struct auproc_audiometh
{
    /**
     * Use this in the processCallback to obtain the buffer of an AUDIO connector.
     * This function should only be called within the processCallback. The returned
     * pointer is only valid until the call to processCallback returns.
     */
    float* (*getAudioBuffer)(auproc_connector* connector, uint32_t nframes);
};


/**
 * Function pointers for handling midi connectors.
 */
struct auproc_midimeth
{
    /**
     * Use this in the processCallback to obtain the buffer of a MIDI connector.
     * This function should only be called within the processCallback. The returned
     * pointer is only valid until the call to processCallback returns.
     */
    auproc_midibuf* (*getMidiBuffer)(auproc_connector* connector, uint32_t nframes);
    
    /**
     * Clear an event buffer.
     *
     * This should be called at the beginning of each process cycle before calling 
     * reserveMidiEvent. This function may not be called on an input connector's buffer.
     */
    void (*clearBuffer)(auproc_midibuf* midibuf);

    /**
     * Get the number of midi events in the buffer.
     * 0 <= event_index < getEventCount(midibuf)
     */    
    uint32_t (*getEventCount)(auproc_midibuf* midibuf);

    /**
     * Get a MIDI event from an event buffer.
     *
     * event       - Event structure to store retrieved event in.
     * midibuf     - Midi buffer from which to retrieve event.
     * event_index - Index of event to retrieve. 
     *               0 <= event_index < getEventCount(midibuf)
     */    
    int (*getMidiEvent)(auproc_midi_event* event,
                        auproc_midibuf*    midibuf,
                        uint32_t           event_index);
    
    /**
     * The caller is responsible for placing the events in order, sorted 
     * by their sample offsets.
     *
     * time      - Sample index of the current process cycle at which 
     *             event should occur.
     * data_size - Length of event's raw data in bytes.
     * 
     * Returns pointer to the beginning of the reserved event's data 
     * buffer, or NULL on error (ie not enough space). 
     */
    unsigned char* (*reserveMidiEvent)(auproc_midibuf*  midibuf,
                                       uint32_t         time,
                                       size_t           data_size);
};

/**
 * MIDI event data in the MIDI event buffer.
 */
struct auproc_midi_event
{
    /**
     * Sample index of the current process cycle at which event is occurs.
     */
    uint32_t time;
    
    /**
     * Number of raw MIDI data bytes.
     */
    size_t size;
    
    /**
     * Pointer to raw MIDI data
     */
    unsigned char* buffer;
};


/**
 * Connector registration.
 */
struct auproc_con_reg
{
    /**
     * Connector type. Must be set beforce calling registerProcessor
     * Possible values 
     *     - AUPROC_AUDIO,
     *     - AUPROC_MIDI.
     */
    auproc_con_type  conType;
    
    /**
     * Connector direction. Must be set beforce calling registerProcessor
     * Possible values:
     *     - AUPROC_IN,
     *     - AUPROC_OUT.
     */
    auproc_direction conDirection;

    /**
     * Contains pointer to auproc connector object after calling registerProcessor.
     */
    auproc_connector* connector;

    /**
     * Contains pointer to audio methods after calling registerProcessor
     * if the connector is of type AUDIO.
     */
    const auproc_audiometh* audioMethods;

    /**
     * Contains pointer to midi methods after calling registerProcessor
     * if the connector is of type MIDI.
     */
    const auproc_midimeth* midiMethods;
};

/**
 * Contains error information for failed connector registration after
 * calling registerProcessor.
 */
struct auproc_con_reg_err
{
    /**
     * Error type.
     */
    auproc_reg_err_type errorType;

    /**
     * Contains the index offset of the connector, i.e. if there is an error with
     * the first connector at Lua stack index firstConnectorIndex the member 
     * conIndex has value 0. If the error is not associated to a given connector,
     * conIndex is set to -1.
     */
    int conIndex;
};

/**
 *  Auproc C API.
 */
struct auproc_capi
{
    int version_major;
    int version_minor;
    int version_patch;
    
    /**
     * May point to another (incompatible) version of this API implementation.
     * NULL if no such implementation exists.
     *
     * The usage of next_capi makes it possible to implement two or more
     * incompatible versions of the C API.
     *
     * An API is compatible to another API if both have the same major 
     * version number and if the minor version number of the first API is
     * greater or equal than the second one's.
     */
    void* next_capi;
    
    
    /**
     * Returns the auproc object type of the object at the given Lua stack
     * index.
     * May return: - AUPROC_TNONE      - no auproc object
     *             - AUPROC_TENGINE    - auproc engine object
     *             - AUPROC_TCONNECTOR - auproc connector object
     */
    auproc_obj_type (*getObjectType)(lua_State* L, int index);

    /**
     * Returns a valid pointer if the object at the given stack
     * index can give a valid auproc engine object, otherwise must 
     * return NULL. Objects of type AUPROC_TENGINE or
     * AUPROC_TCONNECTOR can be used to obtain a 
     * auproc_engine pointer.
     * If info is not NULL, additional information is written into
     * the audproc_info struct.
     * Raises a Lua error if engine was closed.
     */
    auproc_engine* (*getEngine)(lua_State* L, int index, auproc_info* info);

    /**
     * Returns true if engine was closed invalidated due to processing errors.
     * This may also call engineClosedCallback functions that were given
     * in calls to registerProcessor.
     */
    int (*isEngineClosed)(auproc_engine* engine);

    /**
     * Raises a Lua error if engine is invalid (e.g. because it was 
     * closed or invalidated due to processing errors).This may also 
     * call engineClosedCallback functions that were given
     * in calls to registerProcessor.
     */
    void (*checkEngineIsNotClosed)(lua_State* L, auproc_engine* engine);

    /**
     * Gives the connector type of the Lua object at the given Lua
     * stack index.
     * May return: - AUPROC_AUDIO
     *             - AUPROC_MIDI
     * Returns 0 if there is no auproc connector object at the given
     * stack index.
     */
    auproc_con_type (*getConnectorType)(lua_State* L, int index);


    /**
     * Gives the possible directions of the connector type of the Lua
     * object at given stack index before calling registerProcessor.
     * Possible values:
     *     - AUPROC_IN,
     *     - AUPROC_OUT,
     *
     * E.g. a process buffer may give AUPROC_OUT before 
     * calling registerProcessor. If this process buffer is registered
     * with AUPROC_OUT, the method getPossibleDirections will return 
     * AUPROC_IN afterwards, since a process buffer object
     * may only be used as input connector if it used as output
     * connector by another processor.
     */
    auproc_direction (*getPossibleDirections)(lua_State* L, int index);

    /**
     * Registers native procesor object to the auproc engine. Returns a pointer
     * to the created auproc processor object on success. Returns NULL on failure.
     * This method may also raise a Lua error.
     *
     * firstConnectorIndex - stack index of the first Lua connector object given on the
     *                       Lua stack.
     * connectorCount      - number of Lua connector objects on the stack
     * engine              - Auproc engine obtained by method getEngine (s.a.)
     * processorName       - name for error messages to identify the processor
     * processorData       - pointer to data that is reached into processor callbacks
     * processCallback     - realtime process callback, must return 0 in success. 
     *                       Return a value not equal to 0 for indicating a severe processing
     *                       error. The auproc engine will be invalidated in this case.
     * bufferSizeCallback  - adjust buffers for new buffer size, must return 0 in success. 
     *                       Return a value not equal to 0 for indicating a severe processing
     *                       error. The auproc engine will be invalidated in this case.
     * engineClosedCallback
     *                     - called if the auproc engine is closed. Audio processing is not
     *                       possible at this state, however the associated auproc objects 
     *                       can still be used by the caller of the Auproc C API for 
     *                       simple cases, e.g. for calling isEngineClosed.
     * engineReleasedCallback
     *                     - called if the auproc engine is released. All associated
     *                       auproc objects become invalid and should not be used by the caller 
     *                       of the auproc C API.
     * conRegList          - list with registration data for connectorCount Lua connector objects
     *                       on the stack at firstConnectorIndex. The members conDirection
     *                       and conType must match the corresponding Lua connector objects given 
     *                       on the Lua stack. The member conDirection must be AUPROC_IN
     *                       or AUPROC_OUT.
     * regError            - this method returns NULL on failure and gives additional error 
     *                       information in regError if regError != NULL. Member conIndex
     *                       contains the index offset of the connector, i.e. if there is an error with
     *                       the first connector at Lua stack index firstConnectorIndex the member 
     *                       conIndex has value 0. If the error is not associated to a given
     *                       connector conIndex is set to -1.
     */
    auproc_processor* (*registerProcessor)(lua_State* L, 
                                           int firstConnectorIndex, int connectorCount,
                                           auproc_engine* engine, 
                                           const char* processorName,
                                           void* processorData,
                                           int  (*processCallback)(uint32_t nframes, void* processorData),
                                           int  (*bufferSizeCallback)(uint32_t nframes, void* processorData),
                                           void (*engineClosedCallback)(void* processorData),
                                           void (*engineReleasedCallback)(void* processorData),
                                           auproc_con_reg* conRegList,
                                           auproc_con_reg_err* regError);

    /**
     * Unregisters native processor object. Audio processing is stopped for this processor
     * and all connectors are disconnected.
     * This method raises a Lua error if unregistering is not possible. Unregistering is not
     * possible, if this processor has registered for delivering input to a process buffer 
     * that is still used by other registered processors.
     * Raises a Lua error if engine was closed.
     */
    void (*unregisterProcessor)(lua_State* L,
                                auproc_engine* engine,
                                auproc_processor* processor);

    /**
     * Activates the processor. After this point the processCallback is called for realtime
     * processing.
     * This method raises a Lua error if activation is not possible. Activation is not
     * possible, if this processor has registered for receiving output from a process buffer 
     * that has no active processsor delivering input to.
     * Raises a Lua error if engine was closed.
     */
    void (*activateProcessor)(lua_State* L,
                              auproc_engine* engine,
                              auproc_processor* processor);

    /**
     * Deactivates the processor. After this point the processCallback is not called.
     * This method raises a Lua error if deactivation is not possible. Deactivation is not
     * possible, if this processor has registered for delivering input to a process buffer 
     * that is still used by other registered processors, that are activated.
     * Raises a Lua error if engine was closed.
     */
    void (*deactivateProcessor)(lua_State* L,
                                auproc_engine* engine,
                                auproc_processor* processor);

    /**
     * Returns the frame time at the start of the current process cycle.
     * This function should only be called within the processCallback.
     */
    uint32_t (*getProcessBeginFrameTime)(auproc_engine* engine);
    
    /**
     * General engine category name for error messages, 
     * e.g. "client" or "stream".
     */    
    const char* engine_category_name;
    
    /**
     * Log errror message. May be called from any thread, also in the 
     * processCallback.
     * Should only be called for severe errors. Default logs to stderr.
     *
     * engine  - may be NULL if message is not associated to a specific 
     *           engine.
     */                               
    void (*logError)(auproc_engine* engine,
                     const char* fmt, ...);
    
    /**
     * Log info message. May be called from any thread, also in the 
     * processCallback. Default is to discard these messages.
     *
     * engine  - may be NULL if message is not associated to a specific 
     *           engine.
     */                               
    void (*logInfo)(auproc_engine* engine,
                    const char* fmt, ...);
    
};


#if AUPROC_CAPI_IMPLEMENT_SET_CAPI
/**
 * Sets the Auproc C API into the metatable at the given index.
 * 
 * index: index of the table that is be used as metatable for objects 
 *        that are associated to the given capi.
 */
static int auproc_set_capi(lua_State* L, int index, const auproc_capi* capi)
{
    lua_pushlstring(L, AUPROC_CAPI_ID_STRING, strlen(AUPROC_CAPI_ID_STRING));             /* -> key */
    void** udata = (void**) lua_newuserdata(L, sizeof(void*) + strlen(AUPROC_CAPI_ID_STRING) + 1); /* -> key, value */
    *udata = (void*)capi;
    strcpy((char*)(udata + 1), AUPROC_CAPI_ID_STRING);    /* -> key, value */
    lua_rawset(L, (index < 0) ? (index - 2) : index);     /* -> */
    return 0;
}
#endif /* AUPROC_CAPI_IMPLEMENT_SET_CAPI */

#if AUPROC_CAPI_IMPLEMENT_GET_CAPI
/**
 * Gives the associated Auproc C API for the object at the given stack index.
 */
static const auproc_capi* auproc_get_capi(lua_State* L, int index, int* versionError)
{
    if (luaL_getmetafield(L, index, AUPROC_CAPI_ID_STRING) == LUA_TUSERDATA) /* -> _capi */
    {
        const void** udata = (const void**) lua_touserdata(L, -1);           /* -> _capi */

        if (   (lua_rawlen(L, -1) >= sizeof(void*) + strlen(AUPROC_CAPI_ID_STRING) + 1)
            && (memcmp((char*)(udata + 1), AUPROC_CAPI_ID_STRING, 
                       strlen(AUPROC_CAPI_ID_STRING) + 1) == 0))
        {
            const auproc_capi* capi = (const auproc_capi*) *udata;           /* -> _capi */
            while (capi) {
                if (   capi->version_major == AUPROC_CAPI_VERSION_MAJOR
                    && capi->version_minor >= AUPROC_CAPI_VERSION_MINOR)
                {                                                            /* -> _capi */
                    lua_pop(L, 1);                                           /* -> */
                    return capi;
                }
                capi = (const auproc_capi*) capi->next_capi;
            }
            if (versionError) {
                *versionError = 1;
            }
        }                                                                 /* -> _capi */
        lua_pop(L, 1);                                                    /* -> */
    }                                                                     /* -> */
    return NULL;
}
#endif /* AUPROC_CAPI_IMPLEMENT_GET_CAPI */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AUPROC_CAPI_H */
