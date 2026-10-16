# The converter's core on a computer (see x2conv.c).
#
#   make -C tools/x2conv            # build ./x2conv
#   make -C tools/x2conv SANITIZE=1 # with the address and UB sanitizers
#
# The sources are the firmware's own (rp/src), compiled unchanged.

CC ?= cc
SRC := ../../rp/src
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -I $(SRC)/include
ifeq ($(SANITIZE),1)
CFLAGS += -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
endif
UNITS := $(SRC)/stx.c $(SRC)/x2game.c $(SRC)/lzhuf.c $(SRC)/crc32.c $(SRC)/x2level.c $(SRC)/x2global.c

x2conv: x2conv.c $(UNITS) $(SRC)/include/stx.h $(SRC)/include/x2game.h $(SRC)/include/lzhuf.h $(SRC)/include/crc32.h $(SRC)/include/x2io.h $(SRC)/include/x2level.h $(SRC)/include/x2global.h
	$(CC) $(CFLAGS) -o $@ x2conv.c $(UNITS)

clean:
	rm -f x2conv

.PHONY: clean
