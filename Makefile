# git shared - build
#
#   make            compila build/git-shared (build/git-shared.exe su Windows)
#   make test       compila ed esegue i test in test/ (serve sh: Git Bash)
#   make install    copia l'eseguibile in $(PREFIX)
#   make clean      cancella build/
#
# PREFIX di default: ~/bin su Linux, %USERPROFILE%\bin su Windows.
# Su Windows serve MinGW-w64 GCC. make funziona da Git Bash, PowerShell e cmd.

CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
           -O2 -g
SRC_DIR := src
BUILD   := build

ifeq ($(OS),Windows_NT)
EXE      := .exe
PLAT     := win
# printf conforme a C99 (%zu, ...) invece di quello di msvcrt
CPPFLAGS += -D__USE_MINGW_ANSI_STDIO=1
LDLIBS   += -lshell32 -lbcrypt
# HOME esiste solo in Git Bash; USERPROFILE sempre. Barre in avanti:
# vanno bene sia per sh sia, dopo la conversione di winpath, per cmd.
PREFIX   ?= $(subst \,/,$(USERPROFILE))/bin
# MinGW make esegue le ricette con sh se lo trova nel PATH (Git Bash),
# altrimenti con cmd.exe (PowerShell, cmd), anche se $(SHELL) dice sh.exe.
# In cmd "echo $0" stampa "$0" cosi' com'e': e' il modo per riconoscerlo.
ifeq ($(shell echo $$0),$$0)
CMD_SHELL := 1
endif
else
EXE      :=
PLAT     := posix
PREFIX   ?= $(HOME)/bin
endif

# Comandi di file system per la shell in uso.
winpath = $(subst /,\,$1)
ifdef CMD_SHELL
MKDIR = if not exist "$(call winpath,$1)" mkdir "$(call winpath,$1)"
CP    = copy /Y "$(call winpath,$1)" "$(call winpath,$2)" >NUL
RMDIR = if exist "$(call winpath,$1)" rmdir /S /Q "$(call winpath,$1)"
else
MKDIR = mkdir -p "$1"
CP    = cp "$1" "$2"
RMDIR = rm -rf "$1"
endif

# Codice per piattaforma: i file *_win.c e *_posix.c (§6.2).
SRC      := $(filter-out %_win.c %_posix.c,$(wildcard $(SRC_DIR)/*.c)) \
            $(wildcard $(SRC_DIR)/*_$(PLAT).c)
OBJ      := $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(SRC))
BIN      := $(BUILD)/git-shared$(EXE)

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: $(SRC_DIR)/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD):
	$(call MKDIR,$@)

-include $(OBJ:.o=.d)

test: $(BIN)
ifdef CMD_SHELL
	@echo make test richiede sh: lanciarlo da Git Bash
	@exit 1
else
	sh test/run.sh
endif

install: $(BIN)
	$(call MKDIR,$(PREFIX))
	$(call CP,$(BIN),$(PREFIX)/git-shared$(EXE))
	@echo installato in $(PREFIX)

clean:
	$(call RMDIR,$(BUILD))

.PHONY: all test install clean
