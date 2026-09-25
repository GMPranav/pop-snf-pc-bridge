#ifndef ELF32_LOADER_H
#define ELF32_LOADER_H

#include <stdint.h>
#include <stddef.h>
#include "elf32_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GUEST_TRAMPOLINE_BASE 0x00010000u
#define GUEST_TRAMPOLINE_MAX  2048u
#define GUEST_DATA_IMPORT_BASE 0x00030000u
#define GUEST_DATA_IMPORT_MAX  64u

typedef struct {
    char *name;
    uint32_t base;
    uint32_t size;
    uint32_t entry;

    /* Dynamic section pointers (within guest memory) */
    Elf32_Dyn *dynamic;
    const char *dynstr;
    const Elf32_Sym *dynsym;
    uint32_t dynsym_count;

    /* Relocations */
    const Elf32_Rel *rel;
    uint32_t rel_count;
    const Elf32_Rel *plt_rel;
    uint32_t plt_rel_count;

    /* Init / Fini arrays */
    uint32_t *init_array;
    uint32_t init_array_count;
} elf32_module_t;

typedef struct {
    uint8_t *mem;               /* Contiguous guest address space buffer */
    uint32_t mem_size;          /* Total size of guest address space (e.g. 512 MB) */
    uint32_t next_module_base;  /* Bump allocator for module base addresses */

    /* Trampoline table for external function imports */
    uint32_t trampoline_base;
    uint32_t trampoline_count;
    char *trampoline_names[GUEST_TRAMPOLINE_MAX];

    /* Data import slots for data symbols (__stack_chk_guard, etc.) */
    uint32_t data_import_count;
    char *data_import_names[GUEST_DATA_IMPORT_MAX];
    uint32_t data_import_addrs[GUEST_DATA_IMPORT_MAX];

    /* Loaded modules */
    elf32_module_t *modules;
    uint32_t module_count;
    uint32_t module_capacity;
} elf32_image_t;

/* Initialize guest memory image */
elf32_image_t *elf32_image_create(uint32_t mem_size);
void elf32_image_destroy(elf32_image_t *img);

/* Load an ELF shared library into guest memory.
 *
 * WARNING: The returned elf32_module_t pointer points directly into the
 * internal img->modules[] dynamic array. If subsequent elf32_load_module()
 * calls exceed capacity and trigger realloc(), existing raw pointers into
 * img->modules[] will dangle.
 *
 * To maintain a permanent reference across future module loads, store the
 * module index (img->module_count - 1, or via elf32_find_module_index()) and
 * retrieve the pointer via elf32_get_module(), or cache mod->base.
 */
elf32_module_t *elf32_load_module(elf32_image_t *img, const char *filepath, uint32_t preferred_base);

/* Get module by index. Returns NULL if index >= img->module_count.
 * NOTE: Pointers returned by this function may also be invalidated by
 * subsequent elf32_load_module() calls. */
elf32_module_t *elf32_get_module(const elf32_image_t *img, uint32_t index);

/* Find module index by file or library name. Returns -1 if not found. */
int elf32_find_module_index(const elf32_image_t *img, const char *name);

/* Find symbol address by name */
uint32_t elf32_lookup_symbol(const elf32_image_t *img, const char *name);

/* Register a trampoline name and get its virtual address */
uint32_t elf32_add_trampoline(elf32_image_t *img, const char *name);

/* Run .init_array constructors for a module */
void elf32_call_init_array(elf32_image_t *img, elf32_module_t *mod, void (*call_fn)(uint32_t addr));

#ifdef __cplusplus
}
#endif

#endif /* ELF32_LOADER_H */
