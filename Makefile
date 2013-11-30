#!/usr/bin/make -f

#OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only
OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -O3
PREFIX ?= /usr/local
CFLAGS ?= -Wall -Wno-unused-function
LIBDIR ?= lib

override CFLAGS += -g $(OPTIMIZATIONS)
BUILDDIR=build/
###############################################################################
LIB_EXT=.so

LV2DIR ?= $(PREFIX)/$(LIBDIR)/lv2
LOADLIBES=-lm

LV2NAME=tuna
BUNDLE=tuna.lv2

#########

LV2UIREQ=
GLUICFLAGS=-I.
GTKUICFLAGS=-I.

LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic
LIB_EXT=.so

targets=$(BUILDDIR)$(LV2NAME)$(LIB_EXT)

# check for build-dependencies
ifeq ($(shell pkg-config --exists lv2 || echo no), no)
  $(error "LV2 SDK was not found")
endif

ifeq ($(shell pkg-config --exists fftw3f || echo no), no)
  $(error "fftw3f library was not found")
endif

override CFLAGS +=-fPIC
override CFLAGS += `pkg-config --cflags lv2 fftw3f`

LOADLIBES+=`pkg-config --libs fftw3f`

# build target definitions
default: all

all: $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(targets)

$(BUILDDIR)manifest.ttl: lv2ttl/manifest.ttl.in Makefile
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/g" \
	    lv2ttl/manifest.ttl.in > $(BUILDDIR)manifest.ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@LIB_EXT@/$(LIB_EXT)/g;s/@URI_SUFFIX@//g" \
	    lv2ttl/manifest.lv2.in >> $(BUILDDIR)manifest.ttl

$(BUILDDIR)$(LV2NAME).ttl: lv2ttl/$(LV2NAME).ttl.in lv2ttl/$(LV2NAME).lv2.in Makefile
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/g" \
	    lv2ttl/$(LV2NAME).ttl.in > $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@URI_SUFFIX@//g;s/@NAME_SUFFIX@//g" \
	  lv2ttl/$(LV2NAME).lv2.in >> $(BUILDDIR)$(LV2NAME).ttl

$(BUILDDIR)$(LV2NAME)$(LIB_EXT): lv2.c spectr.c fft.c
	@mkdir -p $(BUILDDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c99\
	  -o $(BUILDDIR)$(LV2NAME)$(LIB_EXT) lv2.c \
	  -shared $(LV2LDFLAGS) $(LDFLAGS) $(LOADLIBES)

# install/uninstall/clean target definitions

install: all
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m755 $(targets) $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m644 $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(DESTDIR)$(LV2DIR)/$(BUNDLE)

uninstall:
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/manifest.ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME).ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME)$(LIB_EXT)
	-rmdir $(DESTDIR)$(LV2DIR)/$(BUNDLE)

clean:
	rm -f $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(BUILDDIR)$(LV2NAME)$(LIB_EXT)
	-test -d $(BUILDDIR) && rmdir $(BUILDDIR) || true

.PHONY: clean all install uninstall
