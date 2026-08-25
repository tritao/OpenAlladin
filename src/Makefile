ROOT ?= $(abspath $(CURDIR)/..)
BUILD_DIR ?= $(ROOT)/build/engine
SDL2_CONFIG ?= $(ROOT)/build/deps/sdl2/sysroot/usr/bin/sdl2-config
SDL2_LIBDIR ?= $(ROOT)/build/deps/sdl2/sysroot/usr/lib/x86_64-linux-gnu
CXX ?= c++

CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic
CXXFLAGS += $(shell $(SDL2_CONFIG) --cflags)
CXXFLAGS += -I$(ROOT)/build/deps/sdl2/sysroot/usr/include/x86_64-linux-gnu
LDFLAGS += -L$(SDL2_LIBDIR) -Wl,-rpath,$(SDL2_LIBDIR)
LDLIBS += $(SDL2_LIBDIR)/libSDL2.a -lm -l:libasound.so.2 -ldl -lpthread -l:libpulse.so.0 -l:libsamplerate.so.0 \
	-l:libX11.so.6 -l:libXext.so.6 -l:libXcursor.so.1 -l:libXi.so.6 -l:libXfixes.so.3 -l:libXrandr.so.2 \
	-l:libXss.so.1 -l:libdrm.so.2 -l:libgbm.so.1 -l:libwayland-egl.so.1 -l:libwayland-client.so.0 \
	-l:libwayland-cursor.so.0 -l:libxkbcommon.so.0 -l:libdecor-0.so.0

TARGET := $(ROOT)/build/openaladdin
OBJECTS := $(BUILD_DIR)/engine.o $(BUILD_DIR)/main.o

.PHONY: all clean

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/engine.o: engine.cpp engine.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I. -MMD -MP -c $< -o $@

$(BUILD_DIR)/main.o: main.cpp engine.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I. -MMD -MP -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

-include $(OBJECTS:.o=.d)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
