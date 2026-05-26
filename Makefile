ifndef N64_INST
N64_INST := $(CURDIR)/libdragon
endif

REQUESTED_N64_INST := $(N64_INST)

ifeq ($(strip $(wildcard $(N64_INST)/mips64-elf/include/ktls.h) $(wildcard $(N64_INST)/include/ktls.h)),)
ifneq ($(wildcard $(CURDIR)/libdragon/mips64-elf/include/ktls.h),)
ifneq ($(origin N64_GCCPREFIX),command line)
N64_GCCPREFIX := $(REQUESTED_N64_INST)
endif
override N64_INST := $(CURDIR)/libdragon
endif
endif

ifeq ($(wildcard $(N64_INST)/bin/mips64-elf-gcc),)
ifneq ($(wildcard /opt/libdragon/bin/mips64-elf-gcc),)
N64_GCCPREFIX := /opt/libdragon
endif
endif
BUILD_DIR = build
SOURCE_DIR = .
DOOM_SRC = linuxdoom-1.10
N64_MKDFS_ROOT = filesystem

ifneq ($(wildcard $(N64_INST)/n64.mk),)
include $(N64_INST)/n64.mk
else
include $(N64_INST)/include/n64.mk
endif

N64_ROM_TITLE = "DOOM N64 WIP"

CFLAGS += -I$(DOOM_SRC)
ifeq ($(strip $(wildcard $(REQUESTED_N64_INST)/mips64-elf/include/ktls.h) $(wildcard $(REQUESTED_N64_INST)/include/ktls.h)),)
ifneq ($(wildcard $(CURDIR)/libdragon/include/ktls.h),)
CFLAGS += -I$(CURDIR)/libdragon/include
endif
endif
CFLAGS += -Wno-error
CFLAGS += -Wno-old-style-definition
CFLAGS += -Wno-error=enum-compare
CFLAGS += -Wno-error=sizeof-pointer-memaccess
CFLAGS += -Wno-error=pointer-sign
CFLAGS += -Wno-error=misleading-indentation
CFLAGS += -Wno-error=implicit-int
CFLAGS += -Wno-error=implicit-function-declaration
CFLAGS += -Wno-error=maybe-uninitialized
CFLAGS += -Wno-error=uninitialized

DOOM_COMMON_SRCS = \
	$(DOOM_SRC)/doomdef.c \
	$(DOOM_SRC)/doomstat.c \
	$(DOOM_SRC)/dstrings.c \
	$(DOOM_SRC)/tables.c \
	$(DOOM_SRC)/f_finale.c \
	$(DOOM_SRC)/f_wipe.c \
	$(DOOM_SRC)/d_main.c \
	$(DOOM_SRC)/d_net.c \
	$(DOOM_SRC)/d_items.c \
	$(DOOM_SRC)/g_game.c \
	$(DOOM_SRC)/m_menu.c \
	$(DOOM_SRC)/m_misc.c \
	$(DOOM_SRC)/m_argv.c \
	$(DOOM_SRC)/m_bbox.c \
	$(DOOM_SRC)/m_fixed.c \
	$(DOOM_SRC)/m_swap.c \
	$(DOOM_SRC)/m_cheat.c \
	$(DOOM_SRC)/m_random.c \
	$(DOOM_SRC)/am_map.c \
	$(DOOM_SRC)/p_ceilng.c \
	$(DOOM_SRC)/p_doors.c \
	$(DOOM_SRC)/p_enemy.c \
	$(DOOM_SRC)/p_floor.c \
	$(DOOM_SRC)/p_inter.c \
	$(DOOM_SRC)/p_lights.c \
	$(DOOM_SRC)/p_map.c \
	$(DOOM_SRC)/p_maputl.c \
	$(DOOM_SRC)/p_plats.c \
	$(DOOM_SRC)/p_pspr.c \
	$(DOOM_SRC)/p_setup.c \
	$(DOOM_SRC)/p_sight.c \
	$(DOOM_SRC)/p_spec.c \
	$(DOOM_SRC)/p_switch.c \
	$(DOOM_SRC)/p_mobj.c \
	$(DOOM_SRC)/p_telept.c \
	$(DOOM_SRC)/p_tick.c \
	$(DOOM_SRC)/p_saveg.c \
	$(DOOM_SRC)/p_user.c \
	$(DOOM_SRC)/r_bsp.c \
	$(DOOM_SRC)/r_data.c \
	$(DOOM_SRC)/r_draw.c \
	$(DOOM_SRC)/r_main.c \
	$(DOOM_SRC)/r_plane.c \
	$(DOOM_SRC)/r_segs.c \
	$(DOOM_SRC)/r_sky.c \
	$(DOOM_SRC)/r_things.c \
	$(DOOM_SRC)/w_wad.c \
	$(DOOM_SRC)/wi_stuff.c \
	$(DOOM_SRC)/v_video.c \
	$(DOOM_SRC)/st_lib.c \
	$(DOOM_SRC)/st_stuff.c \
	$(DOOM_SRC)/hu_stuff.c \
	$(DOOM_SRC)/hu_lib.c \
	$(DOOM_SRC)/s_sound.c \
	$(DOOM_SRC)/z_zone.c \
	$(DOOM_SRC)/info.c \
	$(DOOM_SRC)/sounds.c

DOOM_PLATFORM_SRCS = \
	$(DOOM_SRC)/i_main_n64.c \
	$(DOOM_SRC)/i_wad_browser_n64.c \
	$(DOOM_SRC)/i_system_n64.c \
	$(DOOM_SRC)/i_video_n64.c \
	$(DOOM_SRC)/i_sound_n64.c \
	$(DOOM_SRC)/i_net_n64.c

DOOM_SRCS = $(DOOM_COMMON_SRCS) $(DOOM_PLATFORM_SRCS)
OBJS = $(DOOM_SRCS:%.c=$(BUILD_DIR)/%.o)

MUSIC_ASSETS_XM_LOWER = $(wildcard assets/music/*.xm)
MUSIC_ASSETS_XM_UPPER = $(wildcard assets/music/*.XM)
MUSIC_ASSETS_YM_LOWER = $(wildcard assets/music/*.ym)
MUSIC_ASSETS_YM_UPPER = $(wildcard assets/music/*.YM)

MUSIC_ASSETS_CONV = \
	$(addprefix filesystem/music/,$(notdir $(MUSIC_ASSETS_XM_LOWER:%.xm=%.xm64))) \
	$(addprefix filesystem/music/,$(notdir $(MUSIC_ASSETS_XM_UPPER:%.XM=%.xm64))) \
	$(addprefix filesystem/music/,$(notdir $(MUSIC_ASSETS_YM_LOWER:%.ym=%.ym64))) \
	$(addprefix filesystem/music/,$(notdir $(MUSIC_ASSETS_YM_UPPER:%.YM=%.ym64)))

all: doom.z64
.PHONY: all clean check-wad

$(BUILD_DIR)/doom.elf: $(OBJS)

doom.z64: $(BUILD_DIR)/doom.dfs

check-wad:
	@if [ ! -f filesystem/doom.wad ]; then \
		echo "Missing IWAD: place your legally-owned DOOM.WAD at filesystem/doom.wad"; \
		exit 1; \
	fi

filesystem/music/%.xm64: assets/music/%.xm
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	@$(N64_AUDIOCONV) -o filesystem/music "$<"

filesystem/music/%.xm64: assets/music/%.XM
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	@$(N64_AUDIOCONV) -o filesystem/music "$<"

filesystem/music/%.ym64: assets/music/%.ym
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	@$(N64_AUDIOCONV) -o filesystem/music "$<"

filesystem/music/%.ym64: assets/music/%.YM
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	@$(N64_AUDIOCONV) -o filesystem/music "$<"

$(BUILD_DIR)/doom.dfs: check-wad
$(BUILD_DIR)/doom.dfs: $(MUSIC_ASSETS_CONV)

clean:
	rm -rf $(BUILD_DIR) *.z64 *.v64

-include $(wildcard $(BUILD_DIR)/**/*.d)
-include $(wildcard $(BUILD_DIR)/*.d)
