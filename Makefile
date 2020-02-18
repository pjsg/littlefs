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
override CFLAGS += -Wextra -Wshadow -Wjump-misses-init -Wundef
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
	./scripts/test.py $(TFLAGS) $@

.PHONY: test_afl

test_afl: afl/test afl/test_afl

afl/test_afl: afl/*.c bd/lfs_rambd.c lfs*c
	afl-gcc afl/test_afl.c -I. bd/lfs_rambd.c lfs.c lfs_util.c -std=gnu99 -o afl/test_afl

afl/test: afl/*.c bd/lfs_rambd.c lfs*c
	$(CC) -g afl/test_afl.c -I. bd/lfs_rambd.c lfs.c lfs_util.c -std=gnu99 -o afl/test

run_afl: test_afl
	AFL_SKIP_CPUFREQ=true  afl-fuzz -i afltests/ -o ${FINDINGS}/ afl/test_afl

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
