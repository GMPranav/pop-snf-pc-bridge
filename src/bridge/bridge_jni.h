#ifndef BRIDGE_JNI_H
#define BRIDGE_JNI_H

#include <stdint.h>
#include "../elf32/elf32_loader.h"
#include "bridge_libc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FAKE_VM_ADDR   0x00004000u
#define FAKE_ENV_ADDR  0x00005000u
#define STR_VER_ADDR   0x00006000u
#define STR_DIR_ADDR   0x00006100u

/* Set the logical screen resolution reported to the engine via JNI (DisplayMetrics,
 * GetScreenWidth/Height, etc.). Call this before bridge_jni_setup(). */
void bridge_jni_set_screen_size(int width, int height);

/* Initialize fake Android JNIEnv and JavaVM tables in guest memory */
void bridge_jni_setup(elf32_image_t *img);

/* Set debug mode: enables/disables verbose JNI call logs */
void bridge_jni_set_debug_mode(int enabled);

/* Look up JNI host handler */
svc_handler_fn bridge_jni_lookup(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* BRIDGE_JNI_H */
