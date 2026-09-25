#include <stdio.h>
#include "elf32_loader.h"

int main() {
    printf("[+] Testing elf32_loader on libS3DClient.so...\n");
    elf32_image_t *img = elf32_image_create(256 * 1024 * 1024); // 256 MB guest address space
    if (!img) {
        printf("[-] Failed to create guest image!\n");
        return 1;
    }

    elf32_module_t *mod = elf32_load_module(img, "ref_assets/APK Extract/lib/armeabi-v7a/libS3DClient.so", 0x01000000);
    if (!mod) {
        printf("[-] Failed to load module!\n");
        return 1;
    }

    printf("[+] Module loaded at: 0x%08x\n", mod->base);
    printf("[+] Total external trampolines created: %u\n", img->trampoline_count);
    printf("[+] Total data imports created: %u\n", img->data_import_count);

    // Look up essential symbols
    const char *symbols[] = {
        "JNI_OnLoad",
        "Java_com_ubisoft_pop2_S3DRenderer_engineInitialize",
        "Java_com_ubisoft_pop2_S3DRenderer_engineRunOneFrame",
        "Java_com_ubisoft_pop2_S3DRenderer_engineOnKeyboardKeyDown",
        "Java_com_ubisoft_pop2_S3DRenderer_engineSetDirectories",
        "S3DClient_InstallCurrentUserEventHook",
        "_ZN8PrinceAID0Ev",
        "_ZN8aiJaffarD0Ev",
        NULL
    };

    printf("[+] Looking up exported symbols:\n");
    for (int i = 0; symbols[i] != NULL; ++i) {
        uint32_t addr = elf32_lookup_symbol(img, symbols[i]);
        printf("    %-55s -> 0x%08x\n", symbols[i], addr);
        if (addr == 0) {
            printf("[-] ERROR: Symbol %s not found!\n", symbols[i]);
        }
    }

    // Print first 10 trampolines
    printf("[+] Sample SVC trampolines:\n");
    for (uint32_t i = 0; i < (img->trampoline_count < 15 ? img->trampoline_count : 15); ++i) {
        printf("    SVC #%03u: 0x%08x -> %s\n", i, img->trampoline_base + i * 4, img->trampoline_names[i]);
    }

    elf32_image_destroy(img);
    printf("[+] Test completed successfully!\n");
    return 0;
}
