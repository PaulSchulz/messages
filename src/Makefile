.PHONY: all run install clean

all: gschemas.compiled messages router config-parse

gschemas.compiled: org.mawsonlakes.messages.gschema.xml
	glib-compile-schemas .

messages: messages.c
	gcc `pkg-config --cflags gtk+-3.0` -o $@ $< `pkg-config --libs gtk+-3.0`

router: router.c
	gcc `pkg-config --cflags gtk+-3.0` -o $@ $< `pkg-config --libs gtk+-3.0` -lncurses

# Development and testing targets
config-parse: config-parse.c
	gcc `pkg-config --cflags gtk+-3.0 json-glib-1.0` -o $@ $< `pkg-config --libs gtk+-3.0 json-glib-1.0` -lncurses

# Helpful targets
run: gschemas.compiled  messages
	GSETTINGS_SCHEMA_DIR=. ./messages

install:
	sudo cp org.mawsonlakes.messages.gschema.xml /usr/share/glib-2.0/schemas/
	sudo glib-compile-schemas /usr/share/glib-2.0/schemas/

clean:
	-rm gschemas.compiled
	-rm messages
	-rm router
	-rm config-parse
