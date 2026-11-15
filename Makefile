# git shared - build
#
#   make            compila build/git-shared (build/git-shared.exe su Windows)
#   make test       compila ed esegue i test in test/
#   make install    copia l'eseguibile in $(PREFIX) (default: ~/bin)
#   make clean      cancella build/
#
# Su Windows serve MinGW-w64 GCC; i comandi delle ricette (mkdir, cp, rm, sh)
# sono quelli di Git Bash.

CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
           -O2 -g
SRC_DIR := src
BUILD   := build
PREFIX  ?= $(HOME)/bin

ifeq ($(OS),Windows_NT)
EXE      := .exe
PLAT_SRC := $(SRC_DIR)/proc_win.c
# printf conforme a C99 (%zu, ...) invece di quello di msvcrt
CPPFLAGS += -D__USE_MINGW_ANSI_STDIO=1
LDLIBS   += -lshell32
else
EXE      :=
PLAT_SRC := $(SRC_DIR)/proc_posix.c
endif

ALL_PLAT := $(SRC_DIR)/proc_win.c $(SRC_DIR)/proc_posix.c
SRC      := $(filter-out $(ALL_PLAT),$(wildcard $(SRC_DIR)/*.c)) $(PLAT_SRC)
OBJ      := $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(SRC))
BIN      := $(BUILD)/git-shared$(EXE)

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: $(SRC_DIR)/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD):
	mkdir -p $@

-include $(OBJ:.o=.d)

test: $(BIN)
	sh test/run.sh

install: $(BIN)
	mkdir -p "$(PREFIX)"
	cp $(BIN) "$(PREFIX)/"

clean:
	rm -rf $(BUILD)

.PHONY: all test install clean
