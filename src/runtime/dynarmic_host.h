#ifndef DYNARMIC_HOST_H
#define DYNARMIC_HOST_H

#include <stdint.h>
#include "../elf32/elf32_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dynarmic_host_s dynarmic_host_t;

/* Create and initialize Dynarmic ARMv7 JIT for the guest image */
dynarmic_host_t *dynarmic_host_create(elf32_image_t *img);
void dynarmic_host_destroy(dynarmic_host_t *host);

/* Call guest function at given virtual address, returning R0 */
uint32_t dynarmic_call(dynarmic_host_t *host, uint32_t entry_point, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3);

/* Call guest function with stack parameters */
uint32_t dynarmic_call_stack(dynarmic_host_t *host, uint32_t entry_point, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, const uint32_t *stack_args, uint32_t stack_count);

#ifdef __cplusplus
}
#endif

#endif /* DYNARMIC_HOST_H */
