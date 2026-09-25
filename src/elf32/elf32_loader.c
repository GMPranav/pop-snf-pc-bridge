#include "elf32_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static uint32_t read32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static void write32(uint8_t *p, uint32_t v) {
    memcpy(p, &v, sizeof(v));
}

static char *dup_str(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

elf32_image_t *elf32_image_create(uint32_t mem_size) {
    elf32_image_t *img = (elf32_image_t *)calloc(1, sizeof(elf32_image_t));
    if (!img) return NULL;

    img->mem_size = mem_size;
#ifdef _WIN32
    img->mem = (uint8_t *)VirtualAlloc(NULL, mem_size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#else
    img->mem = (uint8_t *)malloc(mem_size);
#endif
    if (!img->mem) {
        free(img);
        return NULL;
    }

    img->trampoline_base = GUEST_TRAMPOLINE_BASE;
    img->trampoline_count = 0;
    img->data_import_count = 0;
    img->next_module_base = 0x01000000u; // Start modules at 16 MB

    img->module_capacity = 8;
    img->modules = (elf32_module_t *)calloc(img->module_capacity, sizeof(elf32_module_t));

    return img;
}

void elf32_image_destroy(elf32_image_t *img) {
    if (!img) return;
    for (uint32_t i = 0; i < img->trampoline_count; ++i) {
        free(img->trampoline_names[i]);
    }
    for (uint32_t i = 0; i < img->data_import_count; ++i) {
        free(img->data_import_names[i]);
    }
    for (uint32_t i = 0; i < img->module_count; ++i) {
        free(img->modules[i].name);
    }
    free(img->modules);
#ifdef _WIN32
    if (img->mem) VirtualFree(img->mem, 0, MEM_RELEASE);
#else
    free(img->mem);
#endif
    free(img);
}

uint32_t elf32_add_trampoline(elf32_image_t *img, const char *name) {
    for (uint32_t i = 0; i < img->trampoline_count; ++i) {
        if (strcmp(img->trampoline_names[i], name) == 0) {
            return img->trampoline_base + i * 8;
        }
    }
    if (img->trampoline_count >= GUEST_TRAMPOLINE_MAX) {
        fprintf(stderr, "elf32_loader: trampoline table overflow for %s\n", name);
        return 0;
    }
    uint32_t idx = img->trampoline_count++;
    img->trampoline_names[idx] = dup_str(name);
    // Write ARM instructions: SVC #idx (0xEF000000 | idx) followed by BX LR (0xE12FFF1E)
    uint32_t svc_inst = 0xEF000000u | (idx & 0x00FFFFFFu);
    uint32_t bx_lr    = 0xE12FFF1Eu;
    write32(img->mem + img->trampoline_base + idx * 8, svc_inst);
    write32(img->mem + img->trampoline_base + idx * 8 + 4, bx_lr);
    return img->trampoline_base + idx * 8;
}

static uint32_t get_or_create_data_import(elf32_image_t *img, const char *name) {
    for (uint32_t i = 0; i < img->data_import_count; ++i) {
        if (strcmp(img->data_import_names[i], name) == 0) {
            return img->data_import_addrs[i];
        }
    }
    if (img->data_import_count >= GUEST_DATA_IMPORT_MAX) {
        fprintf(stderr, "elf32_loader: data import overflow for %s\n", name);
        return 0;
    }
    uint32_t idx = img->data_import_count++;
    img->data_import_names[idx] = dup_str(name);
    uint32_t addr = GUEST_DATA_IMPORT_BASE + idx * 1024; // 1024 bytes per slot
    img->data_import_addrs[idx] = addr;

    // Special initialization for known data symbols
    if (strcmp(name, "__stack_chk_guard") == 0) {
        write32(img->mem + addr, 0xBAADF00D);
    } else if (strcmp(name, "_ctype_") == 0) {
        // _ctype_ points to table + 1
        uint8_t *tbl = img->mem + addr + 16;
        for (int c = -1; c < 256; ++c) {
            uint8_t flags = 0;
            if (c >= 'A' && c <= 'Z') flags |= 0x01 | 0x40; // Upper, Hex
            if (c >= 'a' && c <= 'z') flags |= 0x02;        // Lower
            if (c >= 'a' && c <= 'f') flags |= 0x40;        // Hex
            if (c >= '0' && c <= '9') flags |= 0x04 | 0x40; // Digit, Hex
            if (c == ' ' || (c >= 9 && c <= 13)) flags |= 0x08; // Space
            tbl[c + 1] = flags;
        }
        write32(img->mem + addr, addr + 16 + 1); // Pointer to tbl+1
    } else if (strcmp(name, "_tolower_tab_") == 0) {
        int16_t *tbl = (int16_t*)(img->mem + addr + 16);
        for (int c = -1; c < 256; ++c) {
            tbl[c + 1] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
        }
        write32(img->mem + addr, addr + 16 + 2); // Pointer to tbl+1
    } else if (strcmp(name, "__sF") == 0) {
        // stdin, stdout, stderr (3 FILE structures of 96 bytes each)
        for (int s = 0; s < 3; ++s) {
            uint32_t file_slot = addr + s * 96;
            write32(img->mem + file_slot + 12, s); // _file = fd
        }
    } else if (strcmp(name, "stderr") == 0) {
        uint32_t sf = get_or_create_data_import(img, "__sF");
        write32(img->mem + addr, sf + 2 * 96);
    } else if (strcmp(name, "stdout") == 0) {
        uint32_t sf = get_or_create_data_import(img, "__sF");
        write32(img->mem + addr, sf + 1 * 96);
    } else if (strcmp(name, "stdin") == 0) {
        uint32_t sf = get_or_create_data_import(img, "__sF");
        write32(img->mem + addr, sf);
    }
    return addr;
}

uint32_t elf32_lookup_symbol(const elf32_image_t *img, const char *name) {
    for (uint32_t m = 0; m < img->module_count; ++m) {
        const elf32_module_t *mod = &img->modules[m];
        if (!mod->dynsym || !mod->dynstr) continue;

        for (uint32_t i = 1; i < mod->dynsym_count; ++i) {
            const Elf32_Sym *sym = &mod->dynsym[i];
            if (sym->st_shndx == SHN_UNDEF) continue;
            const char *sym_name = mod->dynstr + sym->st_name;
            if (strcmp(sym_name, name) == 0) {
                return mod->base + sym->st_value;
            }
        }
    }
    return 0;
}

static uint32_t resolve_symbol(elf32_image_t *img, const elf32_module_t *mod, uint32_t sym_idx) {
    if (sym_idx >= mod->dynsym_count) return 0;
    const Elf32_Sym *sym = &mod->dynsym[sym_idx];
    const char *name = mod->dynstr + sym->st_name;

    /* Return address if symbol is defined internally within this module */
    if (sym->st_shndx != SHN_UNDEF) {
        return mod->base + sym->st_value;
    }

    /* Check across other loaded ELF modules */
    uint32_t addr = elf32_lookup_symbol(img, name);
    if (addr != 0) return addr;

    /* Allocate real guest storage for imported data objects */
    if (ELF32_ST_TYPE(sym->st_info) == STT_OBJECT) {
        return get_or_create_data_import(img, name);
    }

    /* Unresolved function symbols bind to an SVC trampoline */
    return elf32_add_trampoline(img, name);
}

static void apply_relocations(elf32_image_t *img, elf32_module_t *mod, const Elf32_Rel *rel, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t r_offset = rel[i].r_offset;
        uint32_t r_info = rel[i].r_info;
        uint32_t r_type = ELF32_R_TYPE(r_info);
        uint32_t r_sym = ELF32_R_SYM(r_info);

        uint8_t *target = img->mem + mod->base + r_offset;
        uint32_t orig_val = read32(target);

        switch (r_type) {
            case R_ARM_NONE:
                break;
            case R_ARM_RELATIVE:
                write32(target, orig_val + mod->base);
                break;
            case R_ARM_GLOB_DAT:
            case R_ARM_JUMP_SLOT:
            case R_ARM_ABS32: {
                uint32_t sym_addr = resolve_symbol(img, mod, r_sym);
                if (r_type == R_ARM_ABS32) {
                    write32(target, orig_val + sym_addr);
                } else {
                    write32(target, sym_addr);
                }
                break;
            }
            default:
                fprintf(stderr, "elf32_loader: unsupported relocation type %u at offset %x\n", r_type, r_offset);
                break;
        }
    }
}

elf32_module_t *elf32_load_module(elf32_image_t *img, const char *filepath, uint32_t preferred_base) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "elf32_loader: could not open %s\n", filepath);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < sizeof(Elf32_Ehdr)) {
        fprintf(stderr, "elf32_loader: file %s is too small to be a valid ELF\n", filepath);
        fclose(f);
        return NULL;
    }

    uint8_t *file_data = (uint8_t *)malloc(file_size);
    if (!file_data) {
        fprintf(stderr, "elf32_loader: failed to allocate %zu bytes for %s\n", file_size, filepath);
        fclose(f);
        return NULL;
    }
    if (fread(file_data, 1, file_size, f) != file_size) {
        fprintf(stderr, "elf32_loader: failed to read %s\n", filepath);
        free(file_data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)file_data;
    if (memcmp(ehdr->e_ident, "\x7f\x45\x4c\x46", 4) != 0 || ehdr->e_machine != 40 /* EM_ARM */) {
        fprintf(stderr, "elf32_loader: invalid ARM ELF header in %s\n", filepath);
        free(file_data);
        return NULL;
    }

    uint32_t min_vaddr = 0xFFFFFFFFu;
    uint32_t max_vaddr = 0;
    const Elf32_Phdr *phdrs = (const Elf32_Phdr *)(file_data + ehdr->e_phoff);

    for (int i = 0; i < ehdr->e_phnum; ++i) {
        if (phdrs[i].p_type == PT_LOAD) {
            if (phdrs[i].p_vaddr < min_vaddr) min_vaddr = phdrs[i].p_vaddr;
            if (phdrs[i].p_vaddr + phdrs[i].p_memsz > max_vaddr) {
                max_vaddr = phdrs[i].p_vaddr + phdrs[i].p_memsz;
            }
        }
    }

    uint32_t mod_base = preferred_base ? preferred_base : img->next_module_base;
    // Align base to 64KB
    mod_base = (mod_base + 0xFFFFu) & ~0xFFFFu;

    // Map PT_LOAD segments into guest memory
    for (int i = 0; i < ehdr->e_phnum; ++i) {
        if (phdrs[i].p_type == PT_LOAD) {
            uint32_t seg_dest = mod_base + phdrs[i].p_vaddr;
            uint32_t filesz = phdrs[i].p_filesz;
            uint32_t memsz = phdrs[i].p_memsz;

            // Bounds check segment memory and file range
            if ((uint64_t)seg_dest + (uint64_t)memsz > (uint64_t)img->mem_size) {
                fprintf(stderr, "elf32_loader: PT_LOAD segment [%d] exceeds guest address space (0x%08X + 0x%X > 0x%08X)\n",
                        i, seg_dest, memsz, (uint32_t)img->mem_size);
                free(file_data);
                return NULL;
            }
            if ((uint64_t)phdrs[i].p_offset + (uint64_t)filesz > (uint64_t)file_size) {
                fprintf(stderr, "elf32_loader: PT_LOAD segment [%d] file offset exceeds file bounds\n", i);
                free(file_data);
                return NULL;
            }

            memcpy(img->mem + seg_dest, file_data + phdrs[i].p_offset, filesz);
            if (memsz > filesz) {
                memset(img->mem + seg_dest + filesz, 0, memsz - filesz);
            }
        }
    }

    // Allocate module entry with dynamic capacity expansion
    if (img->module_count >= img->module_capacity) {
        uint32_t new_cap = img->module_capacity * 2;
        if (new_cap < 8) new_cap = 8;
        elf32_module_t *new_mods = (elf32_module_t *)realloc(img->modules, new_cap * sizeof(elf32_module_t));
        if (!new_mods) {
            fprintf(stderr, "elf32_loader: failed to expand module table capacity to %u\n", new_cap);
            free(file_data);
            return NULL;
        }
        memset(new_mods + img->module_capacity, 0, (new_cap - img->module_capacity) * sizeof(elf32_module_t));
        img->modules = new_mods;
        img->module_capacity = new_cap;
    }
    elf32_module_t *mod = &img->modules[img->module_count++];
    mod->name = dup_str(filepath);
    mod->base = mod_base;
    mod->size = max_vaddr - min_vaddr;
    img->next_module_base = mod_base + ((mod->size + 0x000FFFFFu) & ~0x000FFFFFu);

    // Locate PT_DYNAMIC
    for (int i = 0; i < ehdr->e_phnum; ++i) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            mod->dynamic = (Elf32_Dyn *)(img->mem + mod_base + phdrs[i].p_vaddr);
            break;
        }
    }

    if (mod->dynamic) {
        Elf32_Dyn *d = mod->dynamic;
        uint32_t rel_sz = 0, rel_ent = 8;
        uint32_t plt_rel_sz = 0;

        for (; d->d_tag != DT_NULL; ++d) {
            switch (d->d_tag) {
                case DT_STRTAB:
                    mod->dynstr = (const char *)(img->mem + mod_base + d->d_un.d_ptr);
                    break;
                case DT_SYMTAB:
                    mod->dynsym = (const Elf32_Sym *)(img->mem + mod_base + d->d_un.d_ptr);
                    break;
                case DT_REL:
                    mod->rel = (const Elf32_Rel *)(img->mem + mod_base + d->d_un.d_ptr);
                    break;
                case DT_RELSZ:
                    rel_sz = d->d_un.d_val;
                    break;
                case DT_RELENT:
                    rel_ent = d->d_un.d_val;
                    break;
                case DT_JMPREL:
                    mod->plt_rel = (const Elf32_Rel *)(img->mem + mod_base + d->d_un.d_ptr);
                    break;
                case DT_PLTRELSZ:
                    plt_rel_sz = d->d_un.d_val;
                    break;
                case DT_INIT_ARRAY:
                    mod->init_array = (uint32_t *)(img->mem + mod_base + d->d_un.d_ptr);
                    break;
                case DT_INIT_ARRAYSZ:
                    mod->init_array_count = d->d_un.d_val / sizeof(uint32_t);
                    break;
                case DT_HASH: {
                    const uint32_t *hash_tab = (const uint32_t *)(img->mem + mod_base + d->d_un.d_ptr);
                    if (hash_tab) {
                        // In standard ELF DT_HASH: hash_tab[0] = nbucket, hash_tab[1] = nchain
                        // nchain matches the total symbol table entry count in .dynsym
                        mod->dynsym_count = hash_tab[1];
                    }
                    break;
                }
            }
        }

        if (rel_ent > 0) mod->rel_count = rel_sz / rel_ent;
        mod->plt_rel_count = plt_rel_sz / sizeof(Elf32_Rel);

        // Fallback approximation of dynsym count if DT_HASH was absent
        if (mod->dynsym_count == 0 && mod->dynsym && mod->dynstr) {
            ptrdiff_t symtab_bytes = (const uint8_t *)mod->dynstr - (const uint8_t *)mod->dynsym;
            if (symtab_bytes > 0) {
                mod->dynsym_count = (uint32_t)(symtab_bytes / sizeof(Elf32_Sym));
            }
        }

        // Apply relocations
        if (mod->rel && mod->rel_count > 0) {
            apply_relocations(img, mod, mod->rel, mod->rel_count);
        }
        if (mod->plt_rel && mod->plt_rel_count > 0) {
            apply_relocations(img, mod, mod->plt_rel, mod->plt_rel_count);
        }
    }

    free(file_data);
    printf("[+] elf32_loader: Loaded %s at base 0x%08x (size 0x%08x)\n", filepath, mod->base, mod->size);
    return mod;
}

void elf32_call_init_array(elf32_image_t *img, elf32_module_t *mod, void (*call_fn)(uint32_t addr)) {
    if (!mod->init_array || mod->init_array_count == 0) return;
    for (uint32_t i = 0; i < mod->init_array_count; ++i) {
        uint32_t fn_addr = mod->init_array[i];
        if (fn_addr != 0 && fn_addr != 0xFFFFFFFFu) {
            call_fn(fn_addr);
        }
    }
}

elf32_module_t *elf32_get_module(const elf32_image_t *img, uint32_t index) {
    if (!img || index >= img->module_count) return NULL;
    return &img->modules[index];
}

int elf32_find_module_index(const elf32_image_t *img, const char *name) {
    if (!img || !name) return -1;
    for (uint32_t i = 0; i < img->module_count; ++i) {
        if (img->modules[i].name && (strcmp(img->modules[i].name, name) == 0 ||
                                     strstr(img->modules[i].name, name) != NULL)) {
            return (int)i;
        }
    }
    return -1;
}
