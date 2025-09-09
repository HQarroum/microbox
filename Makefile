CC = gcc
CFLAGS = -W -Wall -I. -D_GNU_SOURCE
LDFLAGS = -lm -lseccomp

SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)
TARGET = microbox

.PHONY: all clean install uninstall debug re

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

debug: CFLAGS += -g -DDEBUG -O0
debug: clean $(TARGET)

re: clean all

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install -D $(TARGET) $(DESTDIR)/usr/local/bin/$(TARGET)
	install -D -m 644 README.md $(DESTDIR)/usr/local/share/doc/$(TARGET)/README.md

uninstall:
	rm -f $(DESTDIR)/usr/local/bin/$(TARGET)
	rm -rf $(DESTDIR)/usr/local/share/doc/$(TARGET)
