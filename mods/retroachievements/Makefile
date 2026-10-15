# Builds the game-side half of the mod (MIPS code) into build/mod.elf.
# RecompModTool then turns it into bk_retroachievements.nrm (see build.sh).

BUILD_DIR := build
TARGET    := $(BUILD_DIR)/mod.elf

MOD_CC ?= clang
MOD_LD ?= ld.lld

CFLAGS   := -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg -mno-check-zero-division \
            -fomit-frame-pointer -ffast-math -fno-builtin-memset -funsigned-char -fno-builtin \
            -Wall -Wextra -Wno-incompatible-library-redeclaration -Wno-unused-parameter -Wno-return-type -Werror=implicit-function-declaration
CPPFLAGS := -nostdinc -D_LANGUAGE_C -DMIPS -Iinclude
LDFLAGS  := -nostdlib -T mod.ld -Map $(BUILD_DIR)/mod.map --unresolved-symbols=ignore-all --emit-relocs -e 0 --no-nmagic

C_SRCS := $(wildcard src/*.c)
C_OBJS := $(addprefix $(BUILD_DIR)/, $(C_SRCS:.c=.o))
C_DEPS := $(C_OBJS:.o=.d)

$(TARGET): $(C_OBJS) mod.ld | $(BUILD_DIR)
	$(MOD_LD) $(C_OBJS) $(LDFLAGS) -o $@

$(BUILD_DIR) $(BUILD_DIR)/src:
	mkdir -p $@

$(BUILD_DIR)/src/%.o: src/%.c | $(BUILD_DIR)/src
	$(MOD_CC) $(CFLAGS) $(CPPFLAGS) $< -MMD -MF $(@:.o=.d) -c -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(C_DEPS)

.PHONY: clean
