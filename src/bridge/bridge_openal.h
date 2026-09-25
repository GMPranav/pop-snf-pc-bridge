#ifndef BRIDGE_OPENAL_H
#define BRIDGE_OPENAL_H

#include <stdint.h>
#include "../elf32/elf32_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize host OpenAL Soft device and context */
int bridge_openal_init(void);
void bridge_openal_shutdown(void);

/* Check if symbol is an OpenAL function and dispatch */
typedef uint32_t (*svc_handler_fn)(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp);
svc_handler_fn bridge_openal_lookup(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* BRIDGE_OPENAL_H */
