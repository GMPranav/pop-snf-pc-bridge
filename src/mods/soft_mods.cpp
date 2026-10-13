#include "soft_mods.h"
#include "elf32_loader.h"
#include <cstdio>
#include <cstring>

static soft_mods_config_t s_soft_mods_cfg = {
    true // clean_main_menu enabled by default
};

void soft_mods_init(const soft_mods_config_t *cfg) {
    if (cfg) {
        s_soft_mods_cfg = *cfg;
    }
}

void soft_mods_set_config(const soft_mods_config_t *cfg) {
    if (cfg) {
        s_soft_mods_cfg = *cfg;
    }
}

const soft_mods_config_t* soft_mods_get_config(void) {
    return &s_soft_mods_cfg;
}

// ---------------------------------------------------------------------------
// Soft-Mod: Clean Main Menu
// Hides mobile-specific freemium buttons ("Get More Coins" & "Get More Games")
// from the main menu, restoring an authentic PC title screen presentation.
// ---------------------------------------------------------------------------

static uint32_t s_cached_hud = 0;
static uint32_t s_cached_cnt = 0;
static uint32_t s_cached_tags = 0;
static uint32_t s_cached_elems_addr = 0;
static uint32_t s_cached_target_elems[32];
static uint32_t s_cached_target_count = 0;

static void mod_clean_main_menu_apply(elf32_image_t *img, uint32_t p0_hud) {
    if (!p0_hud || !img || !img->mem) return;

    // ShiVa3D HUDTree layout:
    // +0x10: TagEntry* tags_array (each entry: uint32_t unk, const char *name)
    // +0x14: uint32_t element_count
    // +0x1C: HUDElement** elements_array
    uint32_t cnt = *(uint32_t*)(img->mem + p0_hud + 0x14);
    uint32_t tags = *(uint32_t*)(img->mem + p0_hud + 0x10);
    uint32_t elems = *(uint32_t*)(img->mem + p0_hud + 0x1c);

    if (!tags || !elems || cnt == 0 || cnt > 2000) return;

    // Rebuild cache when HUD tree structure or address changes
    if (p0_hud != s_cached_hud || cnt != s_cached_cnt || tags != s_cached_tags || elems != s_cached_elems_addr) {
        s_cached_hud = p0_hud;
        s_cached_cnt = cnt;
        s_cached_tags = tags;
        s_cached_elems_addr = elems;
        s_cached_target_count = 0;

        for (uint32_t i = 0; i < cnt && s_cached_target_count < 32; ++i) {
            uint32_t tag_str_ptr = *(uint32_t*)(img->mem + tags + i * 8 + 4);
            if (tag_str_ptr && tag_str_ptr < img->mem_size) {
                const char *tag = (const char*)(img->mem + tag_str_ptr);
                if (strstr(tag, "GetMoreCoins") || strstr(tag, "GetMoreGames") || strstr(tag, "GiftThisApp")) {
                    uint32_t elem = *(uint32_t*)(img->mem + elems + i * 4);
                    if (elem && elem + 8 < img->mem_size) {
                        s_cached_target_elems[s_cached_target_count++] = elem;
                    }
                }
            }
        }
    }

    // Clear bit 0 (visible) and bit 1 (active/clickable) on each cached target element
    for (uint32_t i = 0; i < s_cached_target_count; ++i) {
        uint32_t elem = s_cached_target_elems[i];
        if (elem && elem + 8 < img->mem_size) {
            *(uint32_t*)(img->mem + elem + 4) &= ~3;
        }
    }
}

void soft_mods_apply_hud(elf32_image_t *img, uint32_t p0_hud, uint32_t p0_scene) {
    (void)p0_scene; // Available for future scene-specific mods

    if (s_soft_mods_cfg.clean_main_menu) {
        mod_clean_main_menu_apply(img, p0_hud);
    }
}
