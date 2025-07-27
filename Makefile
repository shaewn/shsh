CC := gcc
AR := ar
LINKFLAGS := -Lbin -lutil
CFLAGS := -g -Wall -O0 -std=gnu11 -Iutil

SHELL_SOURCE_FILES := $(shell find shell -type f -name "*.c")
SHELL_OBJECT_FILES := $(SHELL_SOURCE_FILES:%=bin/%.o)

UTIL_SOURCE_FILES := $(shell find util -type f -name "*.c")
UTIL_OBJECT_FILES := $(UTIL_SOURCE_FILES:%=bin/%.o)

SUBDIRS := bin/shell bin/util
EXE_TARGET := shsh

.PHONY: directories all clean

all: $(EXE_TARGET)

directories: 
	mkdir -p bin $(SUBDIRS)

clean:
	rm -rf bin/

$(EXE_TARGET): $(SHELL_OBJECT_FILES) | directories bin/libutil.a
	$(info linking target $(EXE_TARGET))
	$(CC) -o $@ $^ $(LINKFLAGS) 

bin/libutil.a: $(UTIL_OBJECT_FILES) | directories
	$(info linking $@)
	$(AR) rcs $@ $^

bin/%.o: % | directories
	$(info compiling file $@)
	$(CC) -o $@ -c $^ $(CFLAGS)
