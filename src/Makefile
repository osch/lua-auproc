.PHONY: default auproc
default: auproc

BUILD_DATE  := $(shell date "+%Y-%m-%dT%H:%M:%S")

LNX_GCC_RUN := gcc -shared -fPIC -O2 -g  -Werror=return-type
WIN_GCC_RUN := gcc -shared -fPIC -O2
MAC_GCC_RUN := MACOSX_DEPLOYMENT_TARGET=10.8 gcc -O2 -bundle -undefined dynamic_lookup -all_load

LNX_COPTS   :=
WIN_COPTS   := -I/mingw64/include/lua5.1 
MAC_COPTS   := -I/usr/local/opt/lua/include/lua5.3 

LNX_LOPTS   := -g
WIN_LOPTS   := -lkernel32
MAC_LOPTS   := -lpthread

LNX_SO_EXT  := so
WIN_SO_EXT  := dll
MAC_SO_EXT  := so

GCC_RUN     :=
SO_EXT      :=
COPTS       :=
LOPTS       :=

# platforms: LNX, WIN, MAC
# (may be set in sandbox.mk)

PLATFORM    := LNX
LUA_VERSION := 5.4

-include sandbox.mk

GCC_RUN       := $(or $(GCC_RUN),       $($(PLATFORM)_GCC_RUN))
SO_EXT        := $(or $(SO_EXT),        $($(PLATFORM)_SO_EXT))
COPTS         := $(or $(COPTS),         $($(PLATFORM)_COPTS))
LOPTS         := $(or $(LOPTS),         $($(PLATFORM)_LOPTS))

auproc:
	@mkdir -p build/lua$(LUA_VERSION)/
	$(GCC_RUN) $(COPTS) \
	    -D AUPROC_VERSION=Makefile"-$(BUILD_DATE)" \
	    main.c \
	    auproc_compat.c  \
	    audio_sender.c audio_receiver.c audio_mixer.c  \
	     midi_sender.c  midi_receiver.c  midi_mixer.c  \
	    $(LOPTS) \
	    -o build/lua$(LUA_VERSION)/auproc.$(SO_EXT)
	    

