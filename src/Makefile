.PHONY: all run clean

all: gschemas.compiled messages router

gschemas.compiled: org.mawsonlakes.messages.gschema.xml
	glib-compile-schemas .

messages: messages.c
	gcc `pkg-config --cflags gtk+-3.0` -o $@ $< `pkg-config --libs gtk+-3.0`

router: router.c
	gcc `pkg-config --cflags gtk+-3.0` -o $@ $< `pkg-config --libs gtk+-3.0`

# Helpful targets
run: gschemas.compiled  messages
	GSETTINGS_SCHEMA_DIR=. ./messages

clean:
	-rm gschemas.compiled
	-rm messages
	-rm router
