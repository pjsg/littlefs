TARGET = lfs.a
ifneq ($(wildcard test.c main.c),)
override TARGET = lfs
endif

CC ?= gcc
AR ?= ar
SIZE ?= size

SRC += $(wildcard *.c bd/*.c)
OBJ := $(SRC:.c=.o)
DEP := $(SRC:.c=.d)
ASM := $(SRC:.c=.s)

ifdef DEBUG
override CFLAGS += -O0 -g3
else
override CFLAGS += -Os
endif
ifdef WORD
override CFLAGS += -m$(WORD)
endif
ifdef TRACE
override CFLAGS += -DLFS_YES_TRACE
endif
override CFLAGS += -I.
override CFLAGS += -std=c99 -Wall -pedantic
override CFLAGS += -Wextra -Wshadow -Wundef
# Remove missing-field-initializers because of GCC bug
override CFLAGS += -Wno-missing-field-initializers

ifdef VERBOSE
override TFLAGS += -v
endif

FINDINGS ?= /dev/shm/findings


all: $(TARGET)

asm: $(ASM)

size: $(OBJ)
	$(SIZE) -t $^

test:
	./scripts/test.py $(TFLAGS)
.SECONDEXPANSION:
test%: tests/test$$(firstword $$(subst \#, ,%)).toml
	./scripts/test.py $@ $(TFLAGS)

.PHONY: test_afl

test_afl: afl/test afl/test_afl

afl/test_afl: afl/test_afl.o bd/lfs_mmapbd.o lfs.c lfs_util.o bd/lfs_testbd.o bd/*.h lfs.h
	afl-gcc afl/test_afl.o -I. bd/lfs_testbd.o bd/lfs_mmapbd.o lfs.c afl/get_started.c lfs_util.o -std=gnu99 -o afl/test_afl

afl/test: afl/*.c bd/lfs_mmapbd.c lfs*c bd/lfs_testbd.c bd/*.h lfs.h
	$(CC) $(CFLAGS) -g afl/test_afl.c -I. bd/lfs_testbd.c bd/lfs_mmapbd.c afl/get_started.c lfs.c lfs_util.c -o afl/test

run_afl: test_afl
	scripts/run_afl $(TAFLAGS)
	@echo Started fuzzing run -- use screen -r to view status

-include $(DEP)

lfs: $(OBJ)
	$(CC) $(CFLAGS) $^ $(LFLAGS) -o $@

%.a: $(OBJ)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) -c -MMD $(CFLAGS) $< -o $@

%.s: %.c
	$(CC) -S $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET)
	rm -f $(OBJ)
	rm -f $(DEP)
	rm -f $(ASM)
	rm -f tests/*.toml.*
