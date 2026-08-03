CROSS ?=

CC      := $(CROSS)gcc
CXX     := $(CROSS)g++
AR      := $(CROSS)ar
STRIP   := $(CROSS)strip

TARGET  := libnest-mqtt-preload.so
VERSION ?= 0.2.0

DEPS_STAMP := third_party/.versions
MQTT_C_SRCS := third_party/mqtt-c/src/mqtt.c
MQTT_C_OBJS := $(MQTT_C_SRCS:.c=.o)
DEPS_FILES := $(MQTT_C_SRCS) third_party/mqtt-c/include/mqtt.h \
              third_party/picojson/picojson.h

CPPFLAGS := -Isrc -Ithird_party/mqtt-c/include -Ithird_party/picojson \
            -DNEST_MQTT_VERSION=\"$(VERSION)\" -DMQTTC_PAL_FILE=mqtt_pal_nest.h
CSTD     ?= -std=gnu99
CFLAGS   := -Wall -Wextra -O2 -fPIC $(CSTD)
# The Nest cross-toolchain predates the final C++11 option spelling.
# The project and both dependencies remain C++03-compatible.
CXXSTD   ?=
CXXFLAGS := -Wall -Wextra -O2 -fPIC $(CXXSTD) -fvisibility=hidden
LDFLAGS  := -shared -Wl,-soname,$(TARGET) -Wl,--no-undefined
LDLIBS   := -ldl -lpthread

C_SRCS   := $(wildcard src/*.c) $(MQTT_C_SRCS)
CPP_SRCS := $(wildcard src/*.cpp)
OBJS     := $(C_SRCS:.c=.o) $(CPP_SRCS:.cpp=.o)

all: $(TARGET)

deps: $(DEPS_STAMP)

$(DEPS_STAMP): scripts/fetch-deps.sh
	./scripts/fetch-deps.sh

$(DEPS_FILES): $(DEPS_STAMP)
	@test -f "$@" || (echo "dependency file missing: $@; run make clean-deps deps" >&2; exit 1)

$(OBJS): | $(DEPS_STAMP)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) $(OBJS) -o "$@" $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c "$<" -o "$@"

strip: $(TARGET)
	$(STRIP) --strip-unneeded "$(TARGET)"

check-exports: $(TARGET)
	@$(CROSS)nm -D "$(TARGET)" | grep -E 'nlLogWithComponent|_ZN8nlWakeUp5SleepEv' || \
	  (echo "required preload exports are missing" >&2; exit 1)

host-test: $(DEPS_FILES)
	$(CXX) $(CPPFLAGS) $(CXXSTD) -Wall -Wextra -O2 \
		tests/test_json.cpp src/json_flatten.cpp -o tests/test_json
	./tests/test_json

clean:
	$(RM) $(OBJS) $(TARGET) tests/test_json

clean-deps:
	$(RM) -r third_party/mqtt-c third_party/picojson $(DEPS_STAMP)

.PHONY: all deps strip check-exports host-test clean clean-deps
