#include "soft_mods.h"
#include "elf32_loader.h"
#include <cstdio>
#include <cstring>

static soft_mods_config_t s_soft_mods_cfg = {
    true, // clean_main_menu enabled by default
    true, // clean_shop enabled by default
    true, // clean_level_complete enabled by default
    true  // clean_touch_hud enabled by default
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
// Soft-Mod Code Patches (Applied once after ELF image is loaded in memory)
// ---------------------------------------------------------------------------

void soft_mods_apply_patches(elf32_image_t *img) {
    if (!img || !img->mem) return;

    if (s_soft_mods_cfg.clean_shop) {
        // 1. Suppress fnShowShop: suppresses "Need more coins" pack popup
        // when clicking an item with insufficient balance or pressing coin purchase.
        uint32_t sym_fnShowShop = elf32_lookup_symbol(img, "_ZN13aiStoreAndInv10fnShowShopEv");
        if (sym_fnShowShop && sym_fnShowShop + 4 <= img->mem_size) {
            // ARM opcode: bx lr (0xE12FFF1E)
            *(uint32_t*)(img->mem + sym_fnShowShop) = 0xE12FFF1E;
        }

        // 2. Suppress onShowOfferAds in aiShopPrices
        uint32_t sym_onShowOfferAds = elf32_lookup_symbol(img, "_ZN12aiShopPrices14onShowOfferAdsEiPKN4S3DX10AIVariableEPS1_");
        if (sym_onShowOfferAds && sym_onShowOfferAds + 4 <= img->mem_size) {
            // ARM opcode: bx lr (0xE12FFF1E)
            *(uint32_t*)(img->mem + sym_onShowOfferAds) = 0xE12FFF1E;
        }

        // 3. Suppress ad confirmation check in MainAI::onShowConfirmation (+0x90)
        uint32_t sym_onShowConfirmation = elf32_lookup_symbol(img, "_ZN6MainAI18onShowConfirmationEiPKN4S3DX10AIVariableEPS1_");
        if (sym_onShowConfirmation && sym_onShowConfirmation + 0x90 + 4 <= img->mem_size) {
            // Replace conditional bne with unconditional branch b #0x216550 (0xEA00003A)
            *(uint32_t*)(img->mem + sym_onShowConfirmation + 0x90) = 0xEA00003A;
        }

        // 4. Suppress ad prompt when exiting store in aiStoreAndInv::fnLeaveStore (+0x2D0)
        uint32_t sym_fnLeaveStore = elf32_lookup_symbol(img, "_ZN13aiStoreAndInv12fnLeaveStoreEv");
        if (sym_fnLeaveStore && sym_fnLeaveStore + 0x2D0 + 4 <= img->mem_size) {
            // Replace conditional beq with unconditional branch b #0x2d2c94 (0xEA000013)
            *(uint32_t*)(img->mem + sym_fnLeaveStore + 0x2D0) = 0xEA000013;
        }

        printf("[Soft-Mods] Clean Shop code patches applied successfully.\n");
    }

    if (s_soft_mods_cfg.clean_level_complete) {
        // Neutralize Facebook wall post and social API callbacks in MainAI and aiPlugin with ARM bx lr (0xE12FFF1E)
        static const char *fb_symbols[] = {
            "_ZN6MainAI12onFBWallPostEiPKN4S3DX10AIVariableEPS1_",
            "_ZN8aiPlugin12onFBWallPostEiPKN4S3DX10AIVariableEPS1_",
            "_ZN6MainAI17onInviteFBFriendsEiPKN4S3DX10AIVariableEPS1_",
            "_ZN8aiPlugin10onFBInviteEiPKN4S3DX10AIVariableEPS1_",
            "_ZN6MainAI15onConnectedToFBEiPKN4S3DX10AIVariableEPS1_",
            "_ZN8aiPlugin11onFBConnectEiPKN4S3DX10AIVariableEPS1_",
            "_ZN8aiPlugin12onFBAddScoreEiPKN4S3DX10AIVariableEPS1_",
            "_ZN8aiPlugin14onFBScoresListEiPKN4S3DX10AIVariableEPS1_",
            "_ZN6MainAI13onShowFriendsEiPKN4S3DX10AIVariableEPS1_",
            "_ZN14aiLeaderboards13onShowFriendsEiPKN4S3DX10AIVariableEPS1_",
            "_ZN14aiLeaderboards13onHideFriendsEiPKN4S3DX10AIVariableEPS1_"
        };

        for (size_t i = 0; i < sizeof(fb_symbols) / sizeof(fb_symbols[0]); ++i) {
            uint32_t sym = elf32_lookup_symbol(img, fb_symbols[i]);
            if (sym && sym + 4 <= img->mem_size) {
                *(uint32_t*)(img->mem + sym) = 0xE12FFF1E;
            }
        }

        printf("[Soft-Mods] Clean Level Complete code patches applied successfully.\n");
    }
}

// ---------------------------------------------------------------------------
// Soft-Mod: Clean Main Menu
// Hides mobile-specific freemium buttons ("Get More Coins" & "Get More Games")
// from the main menu, restoring an authentic PC title screen presentation.
// ---------------------------------------------------------------------------

static uint32_t s_cached_hud_mm = 0;
static uint32_t s_cached_cnt_mm = 0;
static uint32_t s_cached_tags_mm = 0;
static uint32_t s_cached_elems_addr_mm = 0;
static uint32_t s_cached_target_elems_mm[32];
static uint32_t s_cached_target_count_mm = 0;

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
    if (p0_hud != s_cached_hud_mm || cnt != s_cached_cnt_mm || tags != s_cached_tags_mm || elems != s_cached_elems_addr_mm) {
        s_cached_hud_mm = p0_hud;
        s_cached_cnt_mm = cnt;
        s_cached_tags_mm = tags;
        s_cached_elems_addr_mm = elems;
        s_cached_target_count_mm = 0;

        for (uint32_t i = 0; i < cnt && s_cached_target_count_mm < 32; ++i) {
            uint32_t tag_str_ptr = *(uint32_t*)(img->mem + tags + i * 8 + 4);
            if (tag_str_ptr && tag_str_ptr < img->mem_size) {
                const char *tag = (const char*)(img->mem + tag_str_ptr);
                if (strstr(tag, "GetMoreCoins") || strstr(tag, "GetMoreGames") || strstr(tag, "GiftThisApp")) {
                    uint32_t elem = *(uint32_t*)(img->mem + elems + i * 4);
                    if (elem && elem + 8 < img->mem_size) {
                        s_cached_target_elems_mm[s_cached_target_count_mm++] = elem;
                    }
                }
            }
        }
    }

    // Clear bit 0 (visible) and bit 1 (active/clickable) on each cached target element
    for (uint32_t i = 0; i < s_cached_target_count_mm; ++i) {
        uint32_t elem = s_cached_target_elems_mm[i];
        if (elem && elem + 8 < img->mem_size) {
            *(uint32_t*)(img->mem + elem + 4) &= ~3;
        }
    }
}

// ---------------------------------------------------------------------------
// Soft-Mod: Clean Shop
// Hides the "+" coin button in coincounter_container and disables its interaction.
// Also neutralizes any residual freemium shop overlay canvases.
// ---------------------------------------------------------------------------

static uint32_t s_cached_hud_shop = 0;
static uint32_t s_cached_cnt_shop = 0;
static uint32_t s_cached_tags_shop = 0;
static uint32_t s_cached_elems_addr_shop = 0;
static uint32_t s_cached_target_elems_shop[32];
static uint32_t s_cached_target_count_shop = 0;
static uint32_t s_cached_nonclick_elems_shop[32];
static uint32_t s_cached_nonclick_count_shop = 0;

static void mod_clean_shop_apply(elf32_image_t *img, uint32_t p0_hud) {
    if (!p0_hud || !img || !img->mem) return;

    uint32_t cnt = *(uint32_t*)(img->mem + p0_hud + 0x14);
    uint32_t tags = *(uint32_t*)(img->mem + p0_hud + 0x10);
    uint32_t elems = *(uint32_t*)(img->mem + p0_hud + 0x1c);

    if (!tags || !elems || cnt == 0 || cnt > 2000) return;

    if (p0_hud != s_cached_hud_shop || cnt != s_cached_cnt_shop || tags != s_cached_tags_shop || elems != s_cached_elems_addr_shop) {
        s_cached_hud_shop = p0_hud;
        s_cached_cnt_shop = cnt;
        s_cached_tags_shop = tags;
        s_cached_elems_addr_shop = elems;
        s_cached_target_count_shop = 0;
        s_cached_nonclick_count_shop = 0;

        for (uint32_t i = 0; i < cnt && s_cached_target_count_shop < 32; ++i) {
            uint32_t tag_str_ptr = *(uint32_t*)(img->mem + tags + i * 8 + 4);
            if (tag_str_ptr && tag_str_ptr < img->mem_size) {
                const char *tag = (const char*)(img->mem + tag_str_ptr);
                if (strstr(tag, "FreeCoins") ||
                    strstr(tag, "freecoin_container") ||
                    strstr(tag, "GetBestCoins") ||
                    strstr(tag, "CoinImage3") ||
                    strstr(tag, "Shop.Canvas") ||
                    strstr(tag, "Shop.InputBlocker") ||
                    strstr(tag, "Shop.DealFreeCoins") ||
                    strstr(tag, "POPUPHUD.PopUpConfirmation")) {
                    uint32_t elem = *(uint32_t*)(img->mem + elems + i * 4);
                    if (elem && elem + 8 < img->mem_size) {
                        s_cached_target_elems_shop[s_cached_target_count_shop++] = elem;
                    }
                } else if (strstr(tag, "PlayerCoins")) {
                    uint32_t elem = *(uint32_t*)(img->mem + elems + i * 4);
                    if (elem && elem + 8 < img->mem_size && s_cached_nonclick_count_shop < 32) {
                        s_cached_nonclick_elems_shop[s_cached_nonclick_count_shop++] = elem;
                    }
                }
            }
        }
    }

    // Clear bit 0 (visible) and bit 1 (active/clickable) on each target element to hide them completely
    for (uint32_t i = 0; i < s_cached_target_count_shop; ++i) {
        uint32_t elem = s_cached_target_elems_shop[i];
        if (elem && elem + 8 < img->mem_size) {
            *(uint32_t*)(img->mem + elem + 4) &= ~3;
        }
    }

    // Clear bit 1 (active/clickable) on elements that remain visible but non-interactive
    for (uint32_t i = 0; i < s_cached_nonclick_count_shop; ++i) {
        uint32_t elem = s_cached_nonclick_elems_shop[i];
        if (elem && elem + 8 < img->mem_size) {
            *(uint32_t*)(img->mem + elem + 4) &= ~2;
        }
    }
}

// ---------------------------------------------------------------------------
// Soft-Mod: Clean Level Complete
// Hides Facebook social share buttons ("SHARE / GET 25 FREE COINS", Facebook logo,
// and background card) from the level completion screens and other social widgets.
// ---------------------------------------------------------------------------

static uint32_t s_cached_hud_lc = 0;
static uint32_t s_cached_cnt_lc = 0;
static uint32_t s_cached_tags_lc = 0;
static uint32_t s_cached_elems_addr_lc = 0;
static uint32_t s_cached_target_elems_lc[128];
static uint32_t s_cached_target_count_lc = 0;
static uint32_t s_cached_achievements_elem = 0;
static uint32_t s_cached_achievements_text_elem = 0;
static uint32_t s_cached_credits_elem = 0;
static uint32_t s_cached_credits_text_elem = 0;

static void mod_clean_level_complete_apply(elf32_image_t *img, uint32_t p0_hud) {
    if (!p0_hud || !img || !img->mem) return;

    uint32_t cnt = *(uint32_t*)(img->mem + p0_hud + 0x14);
    uint32_t tags = *(uint32_t*)(img->mem + p0_hud + 0x10);
    uint32_t elems = *(uint32_t*)(img->mem + p0_hud + 0x1c);

    if (!tags || !elems || cnt == 0 || cnt > 2000) return;

    if (p0_hud != s_cached_hud_lc || cnt != s_cached_cnt_lc || tags != s_cached_tags_lc || elems != s_cached_elems_addr_lc) {
        if (cnt < 50) return; // Ignore empty / secondary player HUDs

        s_cached_hud_lc = p0_hud;
        s_cached_cnt_lc = cnt;
        s_cached_tags_lc = tags;
        s_cached_elems_addr_lc = elems;
        s_cached_target_count_lc = 0;
        s_cached_achievements_elem = 0;
        s_cached_achievements_text_elem = 0;
        s_cached_credits_elem = 0;
        s_cached_credits_text_elem = 0;

        for (uint32_t i = 0; i < cnt && s_cached_target_count_lc < 128; ++i) {
            uint32_t tag_str_ptr = *(uint32_t*)(img->mem + tags + i * 8 + 4);
            if (tag_str_ptr && tag_str_ptr < img->mem_size) {
                const char *tag = (const char*)(img->mem + tag_str_ptr);
                if (strstr(tag, "Facebook") ||
                    strstr(tag, "facebook") ||
                    strstr(tag, "fb_background") ||
                    strstr(tag, "LCMainMenuFB") ||
                    strstr(tag, "freecoin_container") ||
                    strstr(tag, "FreeCoins") ||
                    strstr(tag, "Friends") ||
                    strstr(tag, "friends") ||
                    strstr(tag, "ShareButton") ||
                    strstr(tag, "ShareAchievement") ||
                    strstr(tag, "GameCenter")) {
                    uint32_t elem = *(uint32_t*)(img->mem + elems + i * 4);
                    if (elem && elem + 8 < img->mem_size) {
                        s_cached_target_elems_lc[s_cached_target_count_lc++] = elem;
                    }
                } else if (strstr(tag, "AchievementsText")) {
                    uint32_t elem = *(uint32_t*)(img->mem + elems + i * 4);
                    if (elem && elem + 0x18 < img->mem_size) {
                        s_cached_achievements_text_elem = elem;
                    }
                } else if (strstr(tag, "Achievements")) {
                    uint32_t elem = *(uint32_t*)(img->mem + elems + i * 4);
                    if (elem && elem + 0x18 < img->mem_size) {
                        float y = *(float*)(img->mem + elem + 0x0C);
                        // Extras menu Achievements button has initial y near 0.1700f
                        if (y > 0.10f && y < 0.25f) {
                            s_cached_achievements_elem = elem;
                        }
                    }
                } else if (strstr(tag, "CreditsButton")) {
                    uint32_t elem = *(uint32_t*)(img->mem + elems + i * 4);
                    if (elem && elem + 0x18 < img->mem_size) {
                        s_cached_credits_elem = elem;
                    }
                } else if (strstr(tag, "CreditsText")) {
                    uint32_t elem = *(uint32_t*)(img->mem + elems + i * 4);
                    if (elem && elem + 0x18 < img->mem_size) {
                        s_cached_credits_text_elem = elem;
                    }
                }
            }
        }
    }

    // Clear bit 0 (visible) and bit 1 (active/clickable) on each target element to hide them completely
    for (uint32_t i = 0; i < s_cached_target_count_lc; ++i) {
        uint32_t elem = s_cached_target_elems_lc[i];
        if (elem && elem + 8 < img->mem_size) {
            *(uint32_t*)(img->mem + elem + 4) &= ~3;
        }
    }

    // Shift Achievements and Credits buttons up one slot in Extras menu
    if (s_cached_achievements_elem && s_cached_achievements_elem + 0x10 < img->mem_size) {
        *(float*)(img->mem + s_cached_achievements_elem + 0x0C) = 0.4920f;
    }
    if (s_cached_achievements_text_elem && s_cached_achievements_text_elem + 0x10 < img->mem_size) {
        *(float*)(img->mem + s_cached_achievements_text_elem + 0x0C) = 0.4906f;
    }
    if (s_cached_credits_elem && s_cached_credits_elem + 0x10 < img->mem_size) {
        *(float*)(img->mem + s_cached_credits_elem + 0x0C) = 0.1700f;
    }
    if (s_cached_credits_text_elem && s_cached_credits_text_elem + 0x10 < img->mem_size) {
        *(float*)(img->mem + s_cached_credits_text_elem + 0x0C) = 0.1666f;
    }
}

// ---------------------------------------------------------------------------
// Soft-Mod: Clean Touch Controls (Potion flask & Stealth Walk silhouette)
// Hides floating mobile touch UI buttons during gameplay for an authentic PC display.
// Keyboard (E / Q) and Gamepad (R1 / L1) controls remain fully functional.
// ---------------------------------------------------------------------------

static uint32_t s_cached_hud_touch = 0;
static uint32_t s_cached_cnt_touch = 0;
static uint32_t s_cached_tags_touch = 0;
static uint32_t s_cached_elems_addr_touch = 0;
static uint32_t s_cached_target_elems_touch[32];
static uint32_t s_cached_target_count_touch = 0;

static void mod_clean_touch_hud_apply(elf32_image_t *img, uint32_t p0_hud) {
    if (!p0_hud || !img || !img->mem) return;

    uint32_t cnt = *(uint32_t*)(img->mem + p0_hud + 0x14);
    uint32_t tags = *(uint32_t*)(img->mem + p0_hud + 0x10);
    uint32_t elems = *(uint32_t*)(img->mem + p0_hud + 0x1c);

    if (!tags || !elems || cnt == 0 || cnt > 2000) return;

    // Rescan if HUD address/count changed, OR if we haven't found the controls yet
    // (since HUDGesture is instantiated as a template with "Controls." prefix after level start).
    if (p0_hud != s_cached_hud_touch || cnt != s_cached_cnt_touch ||
        tags != s_cached_tags_touch || elems != s_cached_elems_addr_touch ||
        s_cached_target_count_touch == 0) {

        s_cached_hud_touch = p0_hud;
        s_cached_cnt_touch = cnt;
        s_cached_tags_touch = tags;
        s_cached_elems_addr_touch = elems;
        s_cached_target_count_touch = 0;

        for (uint32_t i = 0; i < cnt && s_cached_target_count_touch < 32; ++i) {
            uint32_t tag_str_ptr = *(uint32_t*)(img->mem + tags + i * 8 + 4);
            if (tag_str_ptr && tag_str_ptr < img->mem_size) {
                const char *tag = (const char*)(img->mem + tag_str_ptr);
                if (strstr(tag, "Controls.Potion") ||
                    strstr(tag, "Controls.RS_RT") ||
                    strstr(tag, "Controls.Shield") ||
                    strstr(tag, "Controls.WalkButtonBlink") ||
                    strstr(tag, "Controls.Joystick") ||
                    strstr(tag, "Controls.StickThumb") ||
                    strstr(tag, "Controls.ButtonA") ||
                    strstr(tag, "Controls.ButtonB") ||
                    strstr(tag, "RS_RT") ||
                    (cnt < 50 && (strstr(tag, "Potion") || strstr(tag, "potion") || strstr(tag, "Shield") || strstr(tag, "shield")))) {
                    uint32_t elem = *(uint32_t*)(img->mem + elems + i * 4);
                    if (elem && elem + 8 < img->mem_size) {
                        s_cached_target_elems_touch[s_cached_target_count_touch++] = elem;
                    }
                }
            }
        }
        if (s_cached_target_count_touch > 0) {
            printf("[Soft-Mods] Clean Touch Controls: masked %u touch UI elements in HUD 0x%08X (cnt=%u).\n",
                   s_cached_target_count_touch, p0_hud, cnt);
        }
    }

    // Clear bit 0 (visible) and bit 1 (active/clickable) on each cached target element
    for (uint32_t i = 0; i < s_cached_target_count_touch; ++i) {
        uint32_t elem = s_cached_target_elems_touch[i];
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
    if (s_soft_mods_cfg.clean_shop) {
        mod_clean_shop_apply(img, p0_hud);
    }
    if (s_soft_mods_cfg.clean_level_complete) {
        mod_clean_level_complete_apply(img, p0_hud);
    }
    if (s_soft_mods_cfg.clean_touch_hud) {
        mod_clean_touch_hud_apply(img, p0_hud);
    }
}

void soft_mods_patch_compressed_texture(int level, int w, int h, int imageSize, void *data) {
    if (!s_soft_mods_cfg.clean_shop || !data) return;

    uint8_t *d = (uint8_t*)data;

    // Level 0: 1024x1024 (256x256 blocks, 16 bytes per block)
    if (w == 1024 && h == 1024 && level == 0 && imageSize >= 1048576) {
        static const uint8_t sig1[16] = { 255, 255, 0, 0, 0, 0, 0, 0, 151, 189, 1, 106, 139, 114, 92, 214 };
        static const uint8_t sig2[16] = { 255, 255, 0, 0, 0, 0, 0, 0, 46, 246, 236, 213, 87, 170, 0, 0 };

        if (memcmp(d + 0x6b040, sig1, 16) == 0 && memcmp(d + 0xb3c50, sig2, 16) == 0) {
            // Patch bar1 (standard coin counter): replace plus button with dark red velvet
            for (int by = 107; by <= 116; ++by) {
                for (int bx = 4; bx <= 13; ++bx) {
                    int src_bx = bx + 12;
                    int src_off = (by * 256 + src_bx) * 16;
                    int dst_off = (by * 256 + bx) * 16;
                    memcpy(d + dst_off, d + src_off, 16);
                }
            }
            // Patch bar2 (highlighted coin counter): replace plus button with dark red velvet
            for (int by = 179; by <= 188; ++by) {
                for (int bx = 197; bx <= 206; ++bx) {
                    int src_bx = bx + 12;
                    int src_off = (by * 256 + src_bx) * 16;
                    int dst_off = (by * 256 + bx) * 16;
                    memcpy(d + dst_off, d + src_off, 16);
                }
            }
            printf("[Soft-Mods] UI_mainmenu_store.dds Clean Shop texture patch applied.\n");
        }
    }
}
