CC := gcc
LINKFLAGS :=
CFLAGS := -g -Wall -O0 -std=c99
SOURCE_FILES := $(shell find -type f -name "*.c" | sed "s|./||")
OBJECT_FILES := $(SOURCE_FILES:%=bin/%.o)

SUBDIRS := $(find -type d)
EXE_TARGET := shsh

.PHONY: directories all clean

all: $(EXE_TARGET)

directories: 
	mkdir -p bin $(SUBDIRS)

clean:
	rm -rf bin/

$(EXE_TARGET): $(OBJECT_FILES) | directories
	$(info Source files: $(SOURCE_FILES))
	$(info Object files: $(OBJECT_FILES))
	$(CC) -o $@ $^ $(LINKFLAGS)

bin/%.o: %
	$(info compiling file $@)
	$(CC) -o $@ -c $^ $(CFLAGS)
