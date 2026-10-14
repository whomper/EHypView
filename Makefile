# EHypView
#
#   make            build the host tools (hypdump) into build/host
#   make test       build and run the test suite on the host
#   make asan       the same, with AddressSanitizer/UBSan
#   make m68k-test  run the suite on big-endian 68020 code under QEMU
#                   (needs gcc-m68k-linux-gnu and qemu-user-static)
#   make atari      build the Atari programs (68020-60: Falcon, TT) into
#                   build/hatari (needs gcc-m68k-linux-gnu)
#   make hatari-test run EHVCHECK.PRG in Hatari (Falcon, EmuTOS) and compare
#                   its report with the host build's (needs hatari)
#   make gui-test   drive EHYPVIEW.PRG in Hatari with the keyboard

CC      ?= cc
CFLAGS  ?= -O2 -g
WARN     = -std=c99 -Wall -Wextra -pedantic -Wshadow -Wstrict-prototypes
CPPFLAGS = -Isrc/core -Isrc/view

CROSS   ?= m68k-atari-mint-
XCFLAGS  = -O2 -fomit-frame-pointer

# portable core (also built for the Atari) + the host platform layer
CORE_PORT = src/core/lh5.c src/core/hypfile.c src/core/page.c src/core/charset.c \
            src/core/ref.c src/core/inf.c src/core/pool.c src/core/fsys.c \
            src/core/index.c src/core/scan.c src/core/resolve.c src/core/plat.c \
            src/core/search.c
CORE_SRC  = $(CORE_PORT) src/core/plat_posix.c
VIEW_SRC  = src/view/view.c src/view/library.c src/view/fontpref.c

HOST     = build/host
CORE_OBJ = $(patsubst src/core/%.c,$(HOST)/%.o,$(CORE_SRC)) $(HOST)/view.o $(HOST)/library.o $(HOST)/fontpref.o

all: $(HOST)/hypdump $(HOST)/test_core $(HOST)/test_index $(HOST)/test_view $(HOST)/test_library \
     $(HOST)/test_menu $(HOST)/test_fonts

$(HOST)/%.o: src/core/%.c src/core/*.h | $(HOST)
	$(CC) $(CPPFLAGS) $(WARN) $(CFLAGS) -c -o $@ $<

$(HOST)/%.o: src/view/%.c src/view/*.h src/core/*.h | $(HOST)
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

$(HOST)/test_view: tests/test_view.c $(HOST)/libehv.a
	$(CC) $(CPPFLAGS) $(WARN) $(CFLAGS) -o $@ $< $(HOST)/libehv.a

$(HOST)/test_library: tests/test_library.c $(HOST)/libehv.a
	$(CC) $(CPPFLAGS) $(WARN) $(CFLAGS) -o $@ $< $(HOST)/libehv.a

$(HOST)/test_fonts: tests/test_fonts.c $(HOST)/libehv.a
	$(CC) $(CPPFLAGS) $(WARN) $(CFLAGS) -o $@ $< $(HOST)/libehv.a

# the menu tree is plain data: built and checked on the host too
$(HOST)/test_menu: tests/test_menu.c src/gem/menu.c src/gem/menu.h atari/gem.h | $(HOST)
	$(CC) -Isrc/gem -Iatari $(WARN) $(CFLAGS) -o $@ tests/test_menu.c src/gem/menu.c

$(HOST):
	mkdir -p $@

test: all
	$(HOST)/test_core tests/fixtures
	$(HOST)/test_index tests/fixtures
	$(HOST)/test_view tests/fixtures
	$(HOST)/test_library tests/fixtures
	$(HOST)/test_menu
	$(HOST)/test_fonts
	sh tests/run_golden.sh $(HOST)/hypdump

asan:
	$(MAKE) test HOST=build/asan CFLAGS="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"

# --- big-endian check: Linux/m68k binaries run under QEMU --------------

M68K_CC = m68k-linux-gnu-gcc
M68K    = build/m68k-linux

m68k-test: $(CORE_SRC) $(VIEW_SRC) src/core/*.h src/gem/menu.c tests/test_menu.c tests/test_fonts.c tests/test_core.c tests/test_index.c tests/test_view.c tests/test_library.c tools/hypdump.c
	mkdir -p $(M68K)
	$(M68K_CC) $(CPPFLAGS) $(WARN) -O2 -static -o $(M68K)/test_core tests/test_core.c $(CORE_SRC)
	$(M68K_CC) $(CPPFLAGS) $(WARN) -O2 -static -o $(M68K)/test_index tests/test_index.c $(CORE_SRC)
	$(M68K_CC) $(CPPFLAGS) $(WARN) -O2 -static -o $(M68K)/test_view tests/test_view.c $(CORE_SRC) $(VIEW_SRC)
	$(M68K_CC) $(CPPFLAGS) $(WARN) -O2 -static -o $(M68K)/test_library tests/test_library.c $(CORE_SRC) $(VIEW_SRC)
	$(M68K_CC) -Isrc/gem -Iatari $(WARN) -O2 -static -o $(M68K)/test_menu tests/test_menu.c src/gem/menu.c
	$(M68K_CC) $(CPPFLAGS) $(WARN) -O2 -static -o $(M68K)/test_fonts tests/test_fonts.c $(CORE_SRC) $(VIEW_SRC)
	$(M68K_CC) $(CPPFLAGS) $(WARN) -O2 -static -o $(M68K)/hypdump tools/hypdump.c $(CORE_SRC)
	printf '#!/bin/sh\nexec qemu-m68k-static "%s" "$$@"\n' "$(CURDIR)/$(M68K)/hypdump" > $(M68K)/hypdump.sh
	chmod +x $(M68K)/hypdump.sh
	qemu-m68k-static $(M68K)/test_core tests/fixtures
	qemu-m68k-static $(M68K)/test_index tests/fixtures
	qemu-m68k-static $(M68K)/test_view tests/fixtures
	qemu-m68k-static $(M68K)/test_library tests/fixtures
	qemu-m68k-static $(M68K)/test_menu
	qemu-m68k-static $(M68K)/test_fonts
	sh tests/run_golden.sh $(M68K)/hypdump.sh

# --- Atari programs ----------------------------------------------------
#
# Built like ECal: a stock m68k ELF compiler (gcc-m68k-linux-gnu), no
# MiNTLib (atari/ is a small freestanding runtime), and tools/elf2tos.py
# writes the TOS program header and relocation table.

XCC      = m68k-linux-gnu-gcc
GCCINC  := $(shell $(XCC) -print-file-name=include 2>/dev/null)
XCPU    ?= -m68020-60
XFLAGS   = $(XCPU) -O2 -fomit-frame-pointer -ffreestanding -fno-builtin \
           -fno-pic -fno-pie -fno-tree-loop-distribute-patterns -fno-store-merging \
           -fno-asynchronous-unwind-tables -Wall -Wextra -Werror=implicit-function-declaration \
           -DEHV_TOS -nostdinc -isystem $(GCCINC) -Iatari/include -Iatari -Isrc/core
XLDFLAGS = $(XCPU) -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,atari/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

ATARI    = build/atari
HATARI_DIR = build/hatari
RUNTIME  = crt0 nf libc tos plat_tos gem
RT_OBJ   = $(addprefix $(ATARI)/,$(addsuffix .o,$(RUNTIME)))
XCORE_OBJ = $(patsubst src/core/%.c,$(ATARI)/core_%.o,$(CORE_PORT))
XHDRS    = $(wildcard src/core/*.h atari/*.h atari/include/*.h)

$(ATARI)/%.o: atari/%.S | $(ATARI)
	$(XCC) $(XCPU) -c -o $@ $<
$(ATARI)/%.o: atari/%.c $(XHDRS) | $(ATARI)
	$(XCC) $(XFLAGS) -std=gnu99 -Wno-sign-compare -Wno-unused-parameter -c -o $@ $<
$(ATARI)/core_%.o: src/core/%.c $(XHDRS) | $(ATARI)
	$(XCC) $(XFLAGS) -std=c99 -pedantic -Wshadow -c -o $@ $<
$(ATARI)/view.o: src/view/view.c src/view/view.h $(XHDRS) | $(ATARI)
	$(XCC) $(XFLAGS) -Isrc/view -std=c99 -pedantic -Wshadow -c -o $@ $<
$(ATARI)/library.o: src/view/library.c src/view/library.h $(XHDRS) | $(ATARI)
	$(XCC) $(XFLAGS) -Isrc/view -std=c99 -pedantic -Wshadow -c -o $@ $<
$(ATARI)/fontpref.o: src/view/fontpref.c src/view/fontpref.h $(XHDRS) | $(ATARI)
	$(XCC) $(XFLAGS) -Isrc/view -std=c99 -pedantic -Wshadow -c -o $@ $<
$(ATARI)/%.o: src/gem/%.c src/gem/*.h src/view/*.h $(XHDRS) | $(ATARI)
	$(XCC) $(XFLAGS) -Isrc/view -Isrc/gem -std=gnu99 -c -o $@ $<
$(ATARI)/ehvcheck.o: tests/ehvcheck.c $(XHDRS) | $(ATARI)
	$(XCC) $(XFLAGS) -std=gnu99 -c -o $@ $<
$(ATARI):
	mkdir -p $@

$(ATARI)/ehvcheck.elf: $(RT_OBJ) $(XCORE_OBJ) $(ATARI)/ehvcheck.o atari/link.ld
	$(XCC) $(XLDFLAGS) -o $@ $(RT_OBJ) $(XCORE_OBJ) $(ATARI)/ehvcheck.o -lgcc

GEM_OBJ  = $(ATARI)/view.o $(ATARI)/library.o $(ATARI)/fontpref.o $(ATARI)/ehypview.o \
           $(ATARI)/libwin.o $(ATARI)/menu.o $(ATARI)/fontsel.o
$(ATARI)/ehypview.elf: $(RT_OBJ) $(XCORE_OBJ) $(GEM_OBJ) atari/link.ld
	$(XCC) $(XLDFLAGS) -o $@ $(RT_OBJ) $(XCORE_OBJ) $(GEM_OBJ) -lgcc

# one program, two names: it sees at start-up whether TOS loaded it as
# an accessory or started it as an application (PRGFLAGS 7: fast load,
# program and memory in TT/Alt RAM when there is some)
$(HATARI_DIR)/EHYPVIEW.PRG: $(ATARI)/ehypview.elf tools/elf2tos.py
	mkdir -p $(HATARI_DIR)
	PRGFLAGS=7 python3 tools/elf2tos.py $< $@
$(HATARI_DIR)/EHYPVIEW.ACC: $(HATARI_DIR)/EHYPVIEW.PRG
	cp $< $@

# test folder for the Atari check program: 8+3 names, one level of nesting
define make_fixt
	rm -rf $(1)/FIXT && mkdir -p $(1)/FIXT/SUB/DEEP $(1)/FIXT/IMAGES
	cp tests/fixtures/basic.hyp $(1)/FIXT/BASIC.HYP
	cp tests/fixtures/basic.ref $(1)/FIXT/BASIC.REF
	cp tests/fixtures/big.hyp $(1)/FIXT/BIG.HYP
	cp tests/fixtures/all.ref $(1)/FIXT/ALL.REF
	cp tests/fixtures/other.hyp $(1)/FIXT/SUB/DEEP/OTHER.HYP
	cp tests/fixtures/images.hyp $(1)/FIXT/IMAGES/IMAGES.HYP
	printf 'HDOC\0\0\0\100\0\005\003\002garbage' > $(1)/FIXT/BROKEN.HYP
endef

$(HATARI_DIR)/EHVCHECK.PRG: $(ATARI)/ehvcheck.elf tools/elf2tos.py
	mkdir -p $(HATARI_DIR)
	python3 tools/elf2tos.py $< $@
	$(call make_fixt,$(HATARI_DIR))

$(HOST)/ehvcheck: tests/ehvcheck.c $(HOST)/libehv.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(HOST)/libehv.a

atari: $(HATARI_DIR)/EHVCHECK.PRG $(HATARI_DIR)/EHYPVIEW.PRG $(HATARI_DIR)/EHYPVIEW.ACC

# drive EHYPVIEW.PRG in Hatari with the keyboard (screenshots in build/gui)
gui-test: atari
	sh tests/emutos.sh
	python3 tests/run_gui.py $(HATARI_DIR) build/emutos/etos512us.img build/gui

# run EHVCHECK.PRG in Hatari (Falcon, EmuTOS) and compare with the host
hatari-test: atari $(HOST)/ehvcheck
	$(call make_fixt,build/hostcheck)
	sh tests/run_hatari.sh $(HOST)/ehvcheck build/hostcheck/FIXT $(HATARI_DIR)

clean:
	rm -rf build

.PHONY: all test asan m68k-test atari hatari-test gui-test clean
