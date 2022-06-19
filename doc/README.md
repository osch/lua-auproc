# Lua Auproc Documentation

<!-- ---------------------------------------------------------------------------------------- -->
##   Contents
<!-- ---------------------------------------------------------------------------------------- -->

   * [Overview](#overview)
   * [Module Functions](#module-functions)
        * [auproc.new_audio_mixer()](#auproc_new_audio_mixer)
        * [auproc.new_midi_mixer()](#auproc_new_midi_mixer)
        * [auproc.new_midi_receiver()](#auproc_new_midi_receiver)
        * [auproc.new_midi_sender()](#auproc_new_midi_sender)
        * [auproc.new_audio_sender()](#auproc_new_audio_sender)
        * [auproc.new_audio_receiver()](#auproc_new_audio_receiver)
   * [Connector Objects](#connector-objects)
   * [Processor Objects](#processor-objects)
        * [processor:activate()](#processor_activate)
        * [processor:deactivate()](#processor_deactivate)
        * [processor:close()](#processor_close)

<!-- ---------------------------------------------------------------------------------------- -->
##   Overview
<!-- ---------------------------------------------------------------------------------------- -->
   
This package provides basic [Lua] audio processor objects to be used for realtime audio and 
midi processing. The provided processor objects can be used in conjunction with Lua packages
that are implementing the [Auproc C API]  e.g. [LJACK](https://github.com/osch/lua-ljack)
or [lrtaudio](https://github.com/osch/lua-lrtaudio).

<!-- ---------------------------------------------------------------------------------------- -->
##   Module Functions
<!-- ---------------------------------------------------------------------------------------- -->

* <a id="auproc_new_audio_mixer">**`  auproc.new_audio_mixer(audioIn[, audioIn]*, audioOut, mixCtrl)
  `**</a>

  Returns a new audio mixer object. The audio mixer object is a 
  [processor object](#processor-objects).
  
  * *audioIn*  - one or more [connector objects](#connector-objects) of type *AUDIO IN*.
  * *audioOut* - [connector object](#connector-objects) of type *AUDIO OUT*.
  * *mixCtrl*  - optional sender object for controlling the mixer, must implement 
                 the [Sender C API], e.g. a [mtmsg] buffer.
  
  The mixer can be controlled by sending messages with the given *mixCtrl* object to the mixer.
  Each message should contain subsequent pairs of numbers: the first number, an integer, 
  is the number of the *audioIn*  connector (1 means *first connector*), the second number 
  of each pair, a float, is the amplification factor that is applied to the corresponding 
  input connector given by the first number of the pair.
  
  
  See also [ljack/example06.lua](https://github.com/osch/lua-ljack/blob/master/examples/example06.lua).

<!-- ---------------------------------------------------------------------------------------- -->

* <a id="auproc_new_midi_mixer">**`  auproc.new_midi_mixer(midiIn[, midiIn]*, midiOut, mixCtrl)
  `**</a>

  * *midiIn*   - one or more [connector objects](#connector-objects) of type *MIDI IN*.
  * *midiOut*  - [connector object](#connector-objects) of type *MIDI OUT*.
  * *mixCtrl*  - optional sender object for controlling the mixer, must implement 
                 the [Sender C API], e.g. a [mtmsg] buffer.
  
  The mixer can be controlled by sending messages with the given *mixCtrl* object to the mixer.
  Each message should contain subsequent triplets of integers: the first integer 
  is the number of the *midiIn*  connector (1 means *first connector*), the second integer 
  of each triplet is the source channel (1-16) that is to be mapped and the third integer is 
  the new channel number (1-16) that the source channel events are mapped to or may be 
  0 to discard events for the given source channel.
  
  See also [ljack/example07.lua](https://github.com/osch/lua-ljack/blob/master/examples/example07.lua).

<!-- ---------------------------------------------------------------------------------------- -->

* <a id="auproc_new_midi_receiver">**`  auproc.new_midi_receiver(midiIn, receiver)
  `**</a>

  Returns a new midi receiver object. The midi receiver object is a 
  [processor object](#processor-objects).
  
  * *midiIn* - [connector object](#connector-objects) of type *MIDI IN*.
               
  * *receiver* - receiver object for midi events, must implement the [Receiver C API], 
                 e.g. a [mtmsg] buffer.
  
  The receiver object receivers for each midi event a message with two arguments:
    - the time of the midi event as integer value in frame time
    - the midi event bytes, an [carray] of  8-bit integer values.
    
  The midi receiver object is subject to garbage collection. The given port object is owned by the
  midi receiver object, i.e. the port object is not garbage collected as long as the midi receiver 
  object is not garbage collected.
    
  See also [ljack/example03.lua](https://github.com/osch/lua-ljack/blob/master/examples/example03.lua).

<!-- ---------------------------------------------------------------------------------------- -->

* <a id="auproc_new_midi_sender">**`  auproc.new_midi_sender(midiOut, sender)
  `**</a>
  
  Returns a new midi sender object. The midi sender object is a 
  [processor object](#processor-objects).
  
  * *midiOut*   - [connector object](#connector-objects) of type *MIDI OUT*.
               
  * *sender* - sender object for midi events, must implement the [Sender C API], 
               e.g. a [mtmsg] buffer.
  
  The sender object should send for each midi event a message with two arguments:
    - optional the frame time of the midi event as integer value. If this value is not given,
      the midi event is sent as soon as possible. If this value refers to a frame time in the
      past, the event is discarded.
    - the midi event bytes as [carray] of  8-bit integer values.
    
  The caller is responsible for sending the events in order, i.e. for increasing the frame 
  time, i.e. the frame time of the subsequent midi event must be equal or larger then the frame 
  time of the preceding midi event.

  The midi sender object is subject to garbage collection. The given connector object is owned 
  by the midi sender object, i.e. the connector object is not garbage collected as long as the 
  midi sender object is not garbage collected.

  See also [ljack/example04.lua](https://github.com/osch/lua-ljack/blob/master/examples/example04.lua).

<!-- ---------------------------------------------------------------------------------------- -->

* <a id="auproc_new_audio_sender">**`  auproc.new_audio_sender(audioOut, sender)
  `**</a>

  Returns a new audio sender object. The audio sender object is a 
  [processor object](#processor-objects).

  * *audioOut*   - [connector object](#connector-objects) of type *AUDIO OUT*.
                    
  * *sender* - sender object for sample data, must implement the [Sender C API], e.g. a [mtmsg] buffer.

  The sender object should send for each chunk of sample data a message with one or two arguments:
    - optional the frame time of the sample data as integer value in frame time. If this 
      value is not given, the samples are played as soon as possible.
    - chunk of sample data, an [carray] of 32-bit float values.
 
  The caller is responsible for sending the events in order, i.e. for increasing the frame 
  time. The chunks of sample data may not overlap, i.e. the frame time of subsequent sample 
  data chunks must be equal or larger then the frame time of the preceding sample data chunk
  plus the length of the preceding chunk.

  The audio sender object is subject to garbage collection. The given connector object is owned 
  by the audio sender object, i.e. the connector object is not garbage collected as long as the 
  audio sender object is not garbage collected.

  See also [ljack/example05.lua](https://github.com/osch/lua-ljack/blob/master/examples/example05.lua).

<!-- ---------------------------------------------------------------------------------------- -->

* <a id="auproc_new_audio_receiver">**`  auproc.new_audio_receiver(audioIn, receiver)
  `**</a>

  Returns a new audio receiver object. The audio receiver object is a 
  [processor object](#processor-objects).

  * *audioIn* - [connector object](#connector-objects) of type *AUDIO IN*.
               
  * *receiver* - receiver object for audio samples, must implement the [Receiver C API], 
                 e.g. a [mtmsg] buffer.
  
  The receiver object receivers for each audio sample chunk a message with two arguments:
    - the time of the audio event as integer value in frame time.
    - the audio sample bytes, an [carray] of 32-bit float values.
    
  The audio receiver object is subject to garbage collection. The given connector object is owned by the
  audio receiver object, i.e. the connector object is not garbage collected as long as the audio receiver 
  object is not garbage collected.
    
  See also [ljack/example08.lua](https://github.com/osch/lua-ljack/blob/master/examples/example08.lua).

<!-- ---------------------------------------------------------------------------------------- -->
##   Connector Objects
<!-- ---------------------------------------------------------------------------------------- -->

Connector objects have to be provided by the package that implements the [Auproc C API].
They can be used to connect [processor objects](#processor-objects) with each other.

Connector objects can be of type AUDIO or MIDI and can be used for either INPUT or OUTPUT direction.

<!-- ---------------------------------------------------------------------------------------- -->
##   Processor Objects
<!-- ---------------------------------------------------------------------------------------- -->

Processor objects are Lua objects for processing realtime audio data. They must be implemented
in C using the [Auproc C API].

This packages includes the following procesor objects. The implementations can be seen as examples
on how to implement procesor objects using the [Auproc C API].

  * [audio mixer](#auproc_new_audio_mixer),       implementation: [audio_mixer.c](../src/audio_mixer.c).
  * [midi mixer](#auproc_new_midi_mixer),         implementation: [midi_mixer.c](../src/midi_mixer.c).
  * [midi reveicer](#auproc_new_midi_receiver),   implementation: [midi_receiver.c](../src/midi_receiver.c).
  * [midi sender](#auproc_new_midi_sender),       implementation: [midi_sender.c](../src/midi_sender.c).
  * [audio sender](#auproc_new_audio_sender),     implementation: [audio_sender.c](../src/audio_sender.c).
  * [audio receiver](#auproc_new_audio_receiver), implementation: [audio_receiver.c](../src/audio_receiver.c).

The above builtin processor objects are implementing the following methods:
  
<!-- ---------------------------------------------------------------------------------------- -->

* <a id="processor_activate">**`       processor:activate()
  `** </a>
  
  Activates the processor object. A activated processor object is taking part in realtime audio
  processing, i.e. the associated *processCallback* is periodically called in the realtime 
  thread.
  
<!-- ---------------------------------------------------------------------------------------- -->

* <a id="processor_deactivate">**`     processor:deactivate()
  `** </a>
  
  Deactivates the processor object. A deactivated processor object can be activated again 
  by calling [processor:activate()](#processor_activate).
  

<!-- ---------------------------------------------------------------------------------------- -->

* <a id="processor_close">**`     processor:close()
  `** </a>
  
  Closes the processor object. A closed processor object is invalid and cannot be used
  furthermore.


<!-- ---------------------------------------------------------------------------------------- -->


End of document.

<!-- ---------------------------------------------------------------------------------------- -->

[Lua]:                      https://www.lua.org
[auproc]:                   https://luarocks.org/modules/osch/auproc
[auproc_cairo]:             https://luarocks.org/modules/osch/auproc_cairo
[auproc_opengl]:            https://luarocks.org/modules/osch/auproc_opengl
[mtmsg]:                    https://github.com/osch/lua-mtmsg#mtmsg
[carray]:                   https://github.com/osch/lua-carray
[light userdata]:           https://www.lua.org/manual/5.4/manual.html#2.1
[Receiver C API]:           https://github.com/lua-capis/lua-receiver-capi
[Sender C API]:             https://github.com/lua-capis/lua-sender-capi
[Auproc C API]:             https://github.com/lua-capis/lua-auproc-capi
