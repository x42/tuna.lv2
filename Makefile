#!/usr/bin/make -f

#OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only
OPTIMIZATIONS ?= -msse -msse2 -mfpmath=sse -O3 -DNDEBUG
PREFIX ?= /usr/local
CFLAGS ?= -g -Wall -Wno-unused-function
LIBDIR ?= lib
STRIP  ?= strip

EXTERNALUI?=yes
BUILDGTK?=no
KXURI?=yes
RW?=robtk/
###############################################################################
LV2DIR ?= $(PREFIX)/$(LIBDIR)/lv2

BUILDDIR=build/
APPBLD=x42/
BUNDLE=tuna.lv2

LV2NAME=tuna
LV2GUI=tunaUI_gl
LV2GTK=tunaUI_gtk

tuna_VERSION?=$(shell git describe --tags HEAD | sed 's/-g.*$$//;s/^v//' || echo "LV2")
#########

LV2UIREQ=
GLUICFLAGS=-I.
GTKUICFLAGS=-I.

UNAME=$(shell uname)
ifeq ($(UNAME),Darwin)
  LV2LDFLAGS=-dynamiclib
  LIB_EXT=.dylib
  EXE_EXT=
  UI_TYPE=ui:CocoaUI
  PUGL_SRC=$(RW)pugl/pugl_osx.m
  PKG_GL_LIBS=
  GLUILIBS=-framework Cocoa -framework OpenGL -framework CoreFoundation
  BUILDGTK=no
  STRIPFLAGS=-u -r -arch all -s $(RW)lv2syms
else
  LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic
  LIB_EXT=.so
  EXE_EXT=
  UI_TYPE=ui:X11UI
  PUGL_SRC=$(RW)pugl/pugl_x11.c
  PKG_GL_LIBS=glu gl
  GLUILIBS=-lX11
  GLUICFLAGS+=`pkg-config --cflags glu`
  STRIPFLAGS= -s
endif

ifneq ($(XWIN),)
  CC=$(XWIN)-gcc
  CXX=$(XWIN)-g++
  LV2LDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic -Wl,--as-needed -lpthread
  LIB_EXT=.dll
  EXE_EXT=.exe
  PUGL_SRC=$(RW)pugl/pugl_win.cpp
  PKG_GL_LIBS=
  GLUILIBS=-lws2_32 -lwinmm -lopengl32 -lglu32 -lgdi32 -lcomdlg32 -lpthread
  BUILDGTK=no
  GLUICFLAGS=-I.
  override LDFLAGS += -static-libgcc -static-libstdc++
endif

ifeq ($(EXTERNALUI), yes)
  ifeq ($(KXURI), yes)
    UI_TYPE=kx:Widget
    LV2UIREQ+=lv2:requiredFeature kx:Widget;
    override CFLAGS += -DXTERNAL_UI
  else
    LV2UIREQ+=lv2:requiredFeature ui:external;
    override CFLAGS += -DXTERNAL_UI
    UI_TYPE=ui:external
  endif
endif

ifeq ($(BUILDOPENGL)$(BUILDGTK), nono)
  $(error at least one of gtk or openGL needs to be enabled)
endif

targets=$(BUILDDIR)$(LV2NAME)$(LIB_EXT)

ifneq ($(BUILDOPENGL), no)
targets+=$(BUILDDIR)$(LV2GUI)$(LIB_EXT)
endif

ifneq ($(BUILDGTK), no)
targets+=$(BUILDDIR)$(LV2GTK)$(LIB_EXT)
PKG_GTK_LIBS=glib-2.0 gtk+-2.0
else
PKG_GTK_LIBS=
endif

###############################################################################
# check for build-dependencies

ifeq ($(shell pkg-config --exists lv2 || echo no), no)
  $(error "LV2 SDK was not found")
endif

ifeq ($(shell pkg-config --atleast-version=1.4 lv2 || echo no), no)
  $(error "LV2 SDK needs to be version 1.4 or later")
endif

ifeq ($(shell pkg-config --exists pango cairo $(PKG_GTK_LIBS) $(PKG_GL_LIBS) || echo no), no)
  $(error "This plugin requires cairo pango $(PKG_GTK_LIBS) $(PKG_GL_LIBS)")
endif

# TODO jack-wrapper (not enabled by default, yet
# need jack and lv2 > =1.4.2 (idle API)
#
#ifeq ($(shell pkg-config --exists jack || echo no), no)
#  $(warning *** libjack from http://jackaudio.org is required)
#  $(error   Please install libjack-dev or libjack-jackd2-dev)
#endif

ifneq ($(MAKECMDGOALS), submodules)
  ifeq ($(wildcard $(RW)robtk.mk),)
    $(warning This plugin needs https://github.com/x42/robtk)
    $(info set the RW environment variale to the location of the robtk headers)
    ifeq ($(wildcard .git),.git)
      $(info or run 'make submodules' to initialize robtk as git submodule)
    endif
    $(error robtk not found)
  endif
endif

# check for LV2 idle thread
ifeq ($(shell pkg-config --atleast-version=1.4.2 lv2 && echo yes), yes)
  GLUICFLAGS+=-DHAVE_IDLE_IFACE
  GTKUICFLAGS+=-DHAVE_IDLE_IFACE
  LV2UIREQ+=lv2:requiredFeature ui:idleInterface; lv2:extensionData ui:idleInterface;
endif

# check for lv2_atom_forge_object  new in 1.8.1 deprecates lv2_atom_forge_blank
ifeq ($(shell pkg-config --atleast-version=1.8.1 lv2 && echo yes), yes)
  override CFLAGS += -DHAVE_LV2_1_8
endif

# add library dependent flags and libs
override CFLAGS +=-fPIC $(OPTIMIZATIONS) -DVERSION="\"$(tuna_VERSION)\""
override CFLAGS += `pkg-config --cflags lv2`

ifneq ($(shell test -f fftw-3.3.4/.libs/libfftw3f.a || echo no), no)
  LV2CFLAGS=$(CFLAGS) -Ifftw-3.3.4/api
  LOADLIBES=fftw-3.3.4/.libs/libfftw3f.a -lm
  FFTW=-Ifftw-3.3.4/api fftw-3.3.4/.libs/libfftw3f.a -lm
else
  ifeq ($(shell pkg-config --exists fftw3f || echo no), no)
    $(error "fftw3f library was not found")
  endif
  FFTWA=`pkg-config --variable=libdir fftw3f`/libfftw3f.a
  ifeq ($(shell test -f $(FFTWA) || echo no), no)
    FFTWA=`pkg-config --libs fftw3f`
  endif
  ifeq ($(shell pkg-config --atleast-version=99.99.99 fftw3f || echo no), no)
  # https://github.com/FFTW/fftw3/issues/16
  $(warning "**********************************************************")
  $(warning "           the fftw3 library is not thread-safe           ")
  $(warning "**********************************************************")
  $(info "These plugins may cause crashes when used in a plugin-host")
  $(info "where libfftw3f symbols are mapped in the global namespace.")
  $(info "Neither these plugins nor the host has control over possible")
  $(info "other plugins calling the fftw planner simultaneously.")
  $(info "Consider statically linking these plugins against a custom build")
  $(info "of libfftw3f.a built with -fvisibility=hidden to avoid this issue.")
  $(warning "")
  $(warning "**********************************************************")
  $(warning "     run   ./static_fft.sh    prior to make to do so.     ")
  $(warning "**********************************************************")
  $(warning "")
  endif
  LV2CFLAGS=$(CFLAGS) `pkg-config --cflags fftw3f`
  $(eval LOADLIBES=$(FFTWA) -lm)
  $(eval FFTW=`pkg-config --cflags fftw3f` $(FFTWA) -lm)
endif

GTKUICFLAGS+=`pkg-config --cflags gtk+-2.0 cairo pango`
GTKUILIBS+=`pkg-config --libs gtk+-2.0 cairo pango`

GLUICFLAGS+=`pkg-config --cflags cairo pango`
GLUILIBS+=`pkg-config $(PKG_UI_FLAGS) --libs cairo pango pangocairo $(PKG_GL_LIBS)`

ifneq ($(XWIN),)
GLUILIBS+=-lpthread -lusp10
endif

GLUICFLAGS+=$(LIC_CFLAGS)
GLUILIBS+=$(LIC_LOADLIBES)

GLUICFLAGS+=-DUSE_GUI_THREAD
ifeq ($(GLTHREADSYNC), yes)
  GLUICFLAGS+=-DTHREADSYNC
endif

ROBGL+= Makefile
ROBGTK += Makefile


###############################################################################
# build target definitions
default: all

submodule_pull:
	-test -d .git -a .gitmodules -a -f Makefile.git && $(MAKE) -f Makefile.git submodule_pull

submodule_update:
	-test -d .git -a .gitmodules -a -f Makefile.git && $(MAKE) -f Makefile.git submodule_update

submodule_check:
	-test -d .git -a .gitmodules -a -f Makefile.git && $(MAKE) -f Makefile.git submodule_check

submodules:
	-test -d .git -a .gitmodules -a -f Makefile.git && $(MAKE) -f Makefile.git submodules

all: submodule_check $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(targets)

jackapps: \
	$(APPBLD)x42-tuna \
	$(APPBLD)x42-tuna-collection\
	$(APPBLD)x42-tuna-fft

$(BUILDDIR)manifest.ttl: lv2ttl/manifest.gl.ttl.in lv2ttl/manifest.gtk.ttl.in lv2ttl/manifest.lv2.ttl.in lv2ttl/manifest.ttl.in Makefile
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@LIB_EXT@/$(LIB_EXT)/g" \
	    lv2ttl/manifest.ttl.in > $(BUILDDIR)manifest.ttl
ifneq ($(BUILDOPENGL), no)
	sed "s/@INSTANCE@/one/g;s/@LV2NAME@/$(LV2NAME)/g;s/@LIB_EXT@/$(LIB_EXT)/g;s/@URI_SUFFIX@//g" \
	    lv2ttl/manifest.lv2.ttl.in >> $(BUILDDIR)manifest.ttl
	sed "s/@INSTANCE@/two/g;s/@LV2NAME@/$(LV2NAME)/g;s/@LIB_EXT@/$(LIB_EXT)/g;s/@URI_SUFFIX@//g" \
	    lv2ttl/manifest.lv2.ttl.in >> $(BUILDDIR)manifest.ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@LIB_EXT@/$(LIB_EXT)/g;s/@UI_TYPE@/$(UI_TYPE)/;s/@LV2GUI@/$(LV2GUI)/g" \
	    lv2ttl/manifest.gl.ttl.in >> $(BUILDDIR)manifest.ttl
endif
ifneq ($(BUILDGTK), no)
	sed "s/@INSTANCE@/one/g;s/@LV2NAME@/$(LV2NAME)/g;s/@LIB_EXT@/$(LIB_EXT)/g;s/@URI_SUFFIX@/_gtk/g" \
	    lv2ttl/manifest.lv2.ttl.in >> $(BUILDDIR)manifest.ttl
	sed "s/@INSTANCE@/two/g;s/@LV2NAME@/$(LV2NAME)/g;s/@LIB_EXT@/$(LIB_EXT)/g;s/@URI_SUFFIX@/_gtk/g" \
	    lv2ttl/manifest.lv2.ttl.in >> $(BUILDDIR)manifest.ttl
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@LIB_EXT@/$(LIB_EXT)/g;s/@LV2GTK@/$(LV2GTK)/g" \
	    lv2ttl/manifest.gtk.ttl.in >> $(BUILDDIR)manifest.ttl
endif

$(BUILDDIR)$(LV2NAME).ttl: lv2ttl/$(LV2NAME).ttl.in lv2ttl/$(LV2NAME).lv2.ttl.in lv2ttl/$(LV2NAME).gui.ttl.in Makefile
	@mkdir -p $(BUILDDIR)
	sed "s/@LV2NAME@/$(LV2NAME)/g" \
	    lv2ttl/$(LV2NAME).ttl.in > $(BUILDDIR)$(LV2NAME).ttl
ifneq ($(BUILDOPENGL), no)
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@UI_URI_SUFFIX@/_gl/;s/@UI_TYPE@/$(UI_TYPE)/;s/@UI_REQ@/$(LV2UIREQ)/;s/@URI_SUFFIX@//g" \
	    lv2ttl/$(LV2NAME).gui.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@INSTANCE@/one/g;s/@LV2NAME@/$(LV2NAME)/g;s/@URI_SUFFIX@//g;s/@NAME_SUFFIX@//g;s/@UI@/ui_gl/g" \
	  lv2ttl/$(LV2NAME).lv2.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@INSTANCE@/two/g;s/@LV2NAME@/$(LV2NAME)/g;s/@URI_SUFFIX@//g;s/@NAME_SUFFIX@/[Spectrum]/g;s/@UI@/ui_gl/g" \
	  lv2ttl/$(LV2NAME).lv2.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
endif
ifneq ($(BUILDGTK), no)
	sed "s/@LV2NAME@/$(LV2NAME)/g;s/@UI_URI_SUFFIX@/_gtk/;s/@UI_TYPE@/ui:GtkUI/;s/@UI_REQ@//;s/@URI_SUFFIX@/_gtk/g" \
	    lv2ttl/$(LV2NAME).gui.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@INSTANCE@/one/g;s/@LV2NAME@/$(LV2NAME)/g;s/@URI_SUFFIX@/_gtk/g;s/@NAME_SUFFIX@/ GTK/g;s/@UI@/ui_gtk/g" \
	  lv2ttl/$(LV2NAME).lv2.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
	sed "s/@INSTANCE@/two/g;s/@LV2NAME@/$(LV2NAME)/g;s/@URI_SUFFIX@/_gtk/g;s/@NAME_SUFFIX@/ [Spectrum]GTK/g;s/@UI@/ui_gtk/g" \
	  lv2ttl/$(LV2NAME).lv2.ttl.in >> $(BUILDDIR)$(LV2NAME).ttl
endif


$(BUILDDIR)$(LV2NAME)$(LIB_EXT): src/tuna.c src/spectr.c src/fft.c src/tuna.h
	@mkdir -p $(BUILDDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LV2CFLAGS) -std=c99 \
	  -o $(BUILDDIR)$(LV2NAME)$(LIB_EXT) src/tuna.c \
	  -shared $(LV2LDFLAGS) $(LDFLAGS) $(LOADLIBES)
	$(STRIP) $(STRIPFLAGS) $(BUILDDIR)$(LV2NAME)$(LIB_EXT)

JACKCFLAGS=-I. $(CFLAGS) $(CXXFLAGS) $(LIC_CFLAGS)
JACKCFLAGS+=`pkg-config --cflags jack lv2 pango pangocairo $(PKG_GL_LIBS)`
JACKLIBS=-lm $(GLUILIBS) $(LIC_LOADLIBES)

$(eval x42_tuna_JACKSRC = src/tuna.c $(value FFTW))
x42_tuna_JACKGUI = gui/tuna.c
x42_tuna_LV2HTTL = lv2ttl/tuna1.h
x42_tuna_JACKDESC = lv2ui_descriptor
$(APPBLD)x42-tuna$(EXE_EXT): src/tuna.c src/spectr.c src/fft.c src/tuna.h \
	        $(x42_tuna_JACKGUI) $(x42_tuna_LV2HTTL)

$(eval x42_tuna_fft_JACKSRC = src/tuna.c $(value FFTW))
x42_tuna_fft_JACKGUI = gui/tuna.c
x42_tuna_fft_LV2HTTL = lv2ttl/tuna2.h
x42_tuna_fft_JACKDESC = lv2ui_descriptor
$(APPBLD)x42-tuna-fft$(EXE_EXT): src/tuna.c src/spectr.c src/fft.c src/tuna.h \
	        $(x42_tuna_JACKGUI) $(x42_tuna_LV2HTTL)


$(eval x42_tuna_collection_JACKSRC = -DX42_MULTIPLUGIN src/tuna.c $(APPBLD)x42-tuna.o $(value FFTW))
x42_tuna_collection_LV2HTTL = lv2ttl/plugins.h
$(APPBLD)x42-tuna-collection$(EXE_EXT): src/tuna.c src/spectr.c src/fft.c src/tuna.h \
	$(APPBLD)x42-tuna.o lv2ttl/tuna1.h lv2ttl/tuna2.h lv2ttl/plugins.h


-include $(RW)robtk.mk

$(BUILDDIR)$(LV2GTK)$(LIB_EXT): gui/tuna.c src/tuna.h
$(BUILDDIR)$(LV2GUI)$(LIB_EXT): gui/tuna.c src/tuna.h

###############################################################################
# install/uninstall/clean target definitions

install: all
	install -d $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m755 $(targets) $(DESTDIR)$(LV2DIR)/$(BUNDLE)
	install -m644 $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl $(DESTDIR)$(LV2DIR)/$(BUNDLE)

uninstall:
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/manifest.ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME).ttl
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2NAME)$(LIB_EXT)
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2GUI)$(LIB_EXT)
	rm -f $(DESTDIR)$(LV2DIR)/$(BUNDLE)/$(LV2GTK)$(LIB_EXT)
	-rmdir $(DESTDIR)$(LV2DIR)/$(BUNDLE)

clean:
	rm -f $(BUILDDIR)manifest.ttl $(BUILDDIR)$(LV2NAME).ttl \
	  $(BUILDDIR)$(LV2NAME)$(LIB_EXT) \
	  $(BUILDDIR)$(LV2GUI)$(LIB_EXT)  \
	  $(BUILDDIR)$(LV2GTK)$(LIB_EXT)
	rm -rf $(BUILDDIR)*.dSYM
	rm -rf $(APPBLD)x42-*
	-test -d $(APPBLD) && rmdir $(APPBLD) || true
	-test -d $(BUILDDIR) && rmdir $(BUILDDIR) || true

distclean: clean
	rm -f cscope.out cscope.files tags

.PHONY: clean all install uninstall distclean jackapps \
        submodule_check submodules submodule_update submodule_pull
