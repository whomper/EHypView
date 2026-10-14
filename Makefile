# EHypView
#
#   make            build the host tools (hypdump) into build/host
#   make test       build and run the test suite on the host
#   make asan       the same, with AddressSanitizer/UBSan
#   make m68k-test  run the suite on big-endian 68020 code under QEMU
#                   (needs gcc-m68k-linux-gnu and qemu-user-static)
#   make atari      cross-compile the core library for 68020-60 (Falcon/TT)
#   make firebee    cross-compile the core library for ColdFire (FireBee)
#
# The cross targets need the m68k-atari-mint toolchain
# (http://vincent.riviere.free.fr/soft/m68k-atari-mint/).

CC      ?= cc
CFLAGS  ?= -O2 -g
WARN     = -std=c99 -Wall -Wextra -pedantic -Wshadow -Wstrict-prototypes
CPPFLAGS = -Isrc/core

CROSS   ?= m68k-atari-mint-
XCFLAGS  = -O2 -fomit-frame-pointer

CORE_SRC = src/core/lh5.c src/core/hypfile.c src/core/page.c src/core/charset.c \
           src/core/ref.c src/core/inf.c src/core/pool.c src/core/fsys.c \
           src/core/index.c src/core/scan.c src/core/resolve.c

HOST     = build/host
CORE_OBJ = $(patsubst src/core/%.c,$(HOST)/%.o,$(CORE_SRC))

all: $(HOST)/hypdump $(HOST)/test_core $(HOST)/test_index

$(HOST)/%.o: src/core/%.c src/core/*.h | $(HOST)
	$(CC) $(CPPFLAGS) $(WARN) $(CFLAGS) -c -o $@ $<

$(HOST)/libehv.a: $(CORE_OBJ)
	rm -f $@
	ar rcs $@ $^

$(HOST)/hypdump: tools/hypdump.c $(HOST)/libehv.a
	$(CC) $(CPPFLAGS) $(WARN) $(CFLAGS) -o $@ $< $(HOST)/libehv.a

$(HOST)/test_core: tests/test_core.c $(HOST)/libehv.a
	$(CC) $(CPPFLAGS) $(WARN) $(CFLAGS) -o $@ $< $(HOST)/libehv.a

$(HOST)/test_index: tests/test_index.c $(HOST)/libehv.a
	$(CC) $(CPPFLAGS) $(WARN) $(CFLAGS) -o $@ $< $(HOST)/libehv.a

$(HOST):
	mkdir -p $@

test: all
	$(HOST)/test_core tests/fixtures
	$(HOST)/test_index tests/fixtures
	sh tests/run_golden.sh $(HOST)/hypdump

asan:
	$(MAKE) test HOST=build/asan CFLAGS="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"

# --- big-endian check: Linux/m68k binaries run under QEMU --------------

M68K_CC = m68k-linux-gnu-gcc
M68K    = build/m68k-linux

m68k-test: $(CORE_SRC) src/core/*.h tests/test_core.c tests/test_index.c tools/hypdump.c
	mkdir -p $(M68K)
	$(M68K_CC) $(CPPFLAGS) $(WARN) -O2 -static -o $(M68K)/test_core tests/test_core.c $(CORE_SRC)
	$(M68K_CC) $(CPPFLAGS) $(WARN) -O2 -static -o $(M68K)/test_index tests/test_index.c $(CORE_SRC)
	$(M68K_CC) $(CPPFLAGS) $(WARN) -O2 -static -o $(M68K)/hypdump tools/hypdump.c $(CORE_SRC)
	printf '#!/bin/sh\nexec qemu-m68k-static "%s" "$$@"\n' "$(CURDIR)/$(M68K)/hypdump" > $(M68K)/hypdump.sh
	chmod +x $(M68K)/hypdump.sh
	qemu-m68k-static $(M68K)/test_core tests/fixtures
	qemu-m68k-static $(M68K)/test_index tests/fixtures
	sh tests/run_golden.sh $(M68K)/hypdump.sh

# --- Atari cross builds (library only for now) -----------------------

build/%/libehv.a: $(CORE_SRC) src/core/*.h
	mkdir -p build/$*
	cd build/$* && $(CROSS)gcc $(ARCH_$*) $(XCFLAGS) $(WARN) -I../../src/core -c $(addprefix ../../,$(CORE_SRC))
	$(CROSS)ar rcs $@ $(patsubst src/core/%.c,build/$*/%.o,$(CORE_SRC))

ARCH_m68020 = -m68020-60
ARCH_coldfire = -mcpu=5475

atari: build/m68020/libehv.a
firebee: build/coldfire/libehv.a

clean:
	rm -rf build

.PHONY: all test asan m68k-test atari firebee clean
