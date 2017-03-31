OPTS    := -O2
CFLAGS  += -Iinclude/ -Wall -Werror -Wextra -fdata-sections -ffunction-sections -fPIC -fno-exceptions -std=gnu99
LCFLAGS	+= -fvisibility=hidden
LDFLAGS += -Wl,--gc-sections -Wl,--export-dynamic -Wl,-O1 -Wl,--discard-all -ljson-c -llibre
SFLAGS  := -R .comment -R .gnu.version -R .gnu.version_r -R .note -R .note.ABI-tag

CC      ?= cc
STRIP   ?= strip

COMMON_SOURCES	:= $(wildcard common/*.c)
COMMON_OBJECTS	:= $(patsubst %.c,%.o,$(COMMON_SOURCES))
MODULES_OBJECTS	:= $(patsubst %.c,%.o,$(wildcard modules/*.c))

LIBS	:= babel.so dhcpleases.so dummy.so fileoutput.so resources.so system.so
TARGETS := node-agent

all: $(COMMON_OBJECTS) $(LIBS) $(TARGETS)

%.o: %.c
	$(CC) $(CFLAGS) $(OPTS) -c -o $@ $<

%.so: modules/%.o
	$(CC) $(CFLAGS) $(LCFLAGS) $(LDFLAGS) $(OPTS) -shared -o $@ $^

$(TARGETS): $(COMMON_OBJECTS)
	@$(eval LDFLAGS += -ldl)
	#@$(eval CFLAGS += -s)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OPTS) -o $@ $@.c $^

clean:
	rm -f $(COMMON_OBJECTS)
	rm -f $(MODULES_OBJECTS)
	rm -f $(LIBS)
	rm -f $(TARGETS)
