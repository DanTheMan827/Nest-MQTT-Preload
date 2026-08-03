CROSS ?=

CC      := $(CROSS)gcc
CXX     := $(CROSS)g++
AR      := $(CROSS)ar
STRIP   := $(CROSS)strip

TARGET  := libnest-mqtt-preload.so
VERSION ?= 0.1.0

CPPFLAGS := -Isrc -DNEST_MQTT_VERSION=\"$(VERSION)\"
CFLAGS   := -Wall -Wextra -O2 -fPIC
CXXFLAGS := -Wall -Wextra -O2 -fPIC -std=gnu++11 -fvisibility=hidden
LDFLAGS  := -shared -Wl,-soname,$(TARGET) -Wl,--no-undefined
LDLIBS   := -ldl -lpthread

C_SRCS   := $(wildcard src/*.c)
CPP_SRCS := $(wildcard src/*.cpp)
OBJS     := $(C_SRCS:.c=.o) $(CPP_SRCS:.cpp=.o)

all: $(TARGET)

toolchain: toolchain/bootstrap.sh
	${BASH} toolchain/bootstrap.sh

toolchain/bootstrap.sh:
	git submodule update --init --recursive
	
	[ -f "$@" ] || git submodule update --init --recursive --force toolchain
	[ -f "$@" ] && chmod +x "$@"

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

host-test:
	$(CXX) -Isrc -std=gnu++11 -Wall -Wextra -O2 tests/test_json.cpp src/json_flatten.cpp -o tests/test_json
	./tests/test_json

clean:
	$(RM) $(OBJS) $(TARGET) tests/test_json

.PHONY: all strip check-exports host-test clean
