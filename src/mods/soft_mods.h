#ifndef SOFT_MODS_H
#define SOFT_MODS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "elf32_loader.h"

typedef struct {
    bool clean_main_menu; // Hide "Get More Coins" & "Get More Games" mobile promo UI
} soft_mods_config_t;

// Initialize the soft-mods subsystem with the active configuration
void soft_mods_init(const soft_mods_config_t *cfg);

// Update runtime configuration if modified dynamically
void soft_mods_set_config(const soft_mods_config_t *cfg);

// Retrieve active soft-mods configuration
const soft_mods_config_t* soft_mods_get_config(void);

// Apply active HUD soft-mods on each frame when p0_hud is available
void soft_mods_apply_hud(elf32_image_t *img, uint32_t p0_hud, uint32_t p0_scene);

#ifdef __cplusplus
}
#endif

#endif // SOFT_MODS_H
