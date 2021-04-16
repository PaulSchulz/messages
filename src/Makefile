all: gschemas.compiled messages

gschemas.compiled: org.mawsonlakes.messages.gschema.xml
	glib-compile-schemas

messages: messages.c
	gcc `pkg-config --cflags gtk+-3.0` -o $@ $< `pkg-config --libs gtk+-3.0`
