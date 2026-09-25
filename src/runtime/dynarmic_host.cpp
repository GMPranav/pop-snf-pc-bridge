#include "dynarmic_host.h"
#include "../bridge/bridge_libc.h"
#include "../bridge/bridge_gles.h"
#include "../bridge/bridge_openal.h"
#include "../bridge/bridge_jni.h"

#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>
#include <array>
#include <vector>

constexpr uint32_t HALT_SENTINEL_LR = 0x00000800u;
constexpr uint32_t HALT_SVC_ARM     = 0x0000DEADu;
constexpr uint32_t HALT_SVC_THUMB   = 0x000000ADu;

/* INVARIANT: Single-threaded, non-reentrant guest execution model.
 * g_guest_lr and g_guest_pc hold SVC dispatch context for the currently
 * executing host bridge function. Bridge handlers MUST NOT make nested/reentrant
 * calls into guest code (nested jit->Run()), as doing so would overwrite these
 * globals. If reentrant guest invocation is ever needed in the future, these
 * must be converted into a thread-local execution context stack. */
uint32_t g_guest_lr = 0;
uint32_t g_guest_pc = 0;
extern bool g_debug_mode;

class HostCallbacks : public Dynarmic::A32::UserCallbacks {
public:
    elf32_image_t *img;
    Dynarmic::A32::Jit *jit = nullptr;
    std::vector<svc_handler_fn> handlers;

    HostCallbacks(elf32_image_t *image) : img(image) {}

    void InitHandlers() {
        handlers.resize(img->trampoline_count, nullptr);
        for (uint32_t i = 0; i < img->trampoline_count; ++i) {
            const char *name = img->trampoline_names[i];
            svc_handler_fn fn = bridge_gles_lookup(name);
            if (!fn) fn = bridge_openal_lookup(name);
            if (!fn) fn = bridge_libc_lookup(name);
            if (!fn) fn = bridge_jni_lookup(name);
            handlers[i] = fn;
        }
    }

    std::uint8_t MemoryRead8(std::uint32_t vaddr) override {
        return (vaddr < img->mem_size) ? img->mem[vaddr] : 0;
    }
    std::uint16_t MemoryRead16(std::uint32_t vaddr) override {
        if (vaddr + 1 < img->mem_size) {
            uint16_t val;
            memcpy(&val, img->mem + vaddr, 2);
            return val;
        }
        return 0;
    }
    std::uint32_t MemoryRead32(std::uint32_t vaddr) override {
        if (vaddr + 3 < img->mem_size) {
            uint32_t val;
            memcpy(&val, img->mem + vaddr, 4);
            return val;
        }
        return 0;
    }
    std::uint64_t MemoryRead64(std::uint32_t vaddr) override {
        if (vaddr + 7 < img->mem_size) {
            uint64_t val;
            memcpy(&val, img->mem + vaddr, 8);
            return val;
        }
        return 0;
    }

    void MemoryWrite8(std::uint32_t vaddr, std::uint8_t value) override {
        if (vaddr < img->mem_size) img->mem[vaddr] = value;
    }
    void MemoryWrite16(std::uint32_t vaddr, std::uint16_t value) override {
        if (vaddr + 1 < img->mem_size) memcpy(img->mem + vaddr, &value, 2);
    }
    void MemoryWrite32(std::uint32_t vaddr, std::uint32_t value) override {
        if (vaddr + 3 < img->mem_size) memcpy(img->mem + vaddr, &value, 4);
    }
    void MemoryWrite64(std::uint32_t vaddr, std::uint64_t value) override {
        if (vaddr + 7 < img->mem_size) memcpy(img->mem + vaddr, &value, 8);
    }

    void InterpreterFallback(std::uint32_t pc, std::size_t num_instructions) override {
        fprintf(stderr, "dynarmic: interpreter fallback at pc=0x%08x (instructions=%zu)\n", pc, num_instructions);
    }

    void CallSVC(std::uint32_t swi) override {
        uint32_t swi_num = swi & 0x00FFFFFFu;
        if (swi_num == HALT_SVC_ARM || swi_num == HALT_SVC_THUMB) {
            jit->HaltExecution();
            return;
        }

        uint32_t idx = swi_num;
        g_guest_lr = jit->Regs()[14];
        g_guest_pc = jit->Regs()[15];
        if (idx < handlers.size() && handlers[idx]) {
            uint32_t r0 = jit->Regs()[0];
            uint32_t r1 = jit->Regs()[1];
            uint32_t r2 = jit->Regs()[2];
            uint32_t r3 = jit->Regs()[3];
            uint32_t sp = jit->Regs()[13];
            g_has_guest_ret_r1 = 0;
            uint32_t ret = handlers[idx](img, r0, r1, r2, r3, sp);
            jit->Regs()[0] = ret;
            if (g_has_guest_ret_r1) {
                jit->Regs()[1] = g_guest_ret_r1;
                g_has_guest_ret_r1 = 0;
            }
        } else {
            const char *name = (idx < img->trampoline_count) ? img->trampoline_names[idx] : "unknown";
            fprintf(stderr, "[TRANSLATOR MISSING] Unhandled SVC #%u (%s) r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X lr=0x%08X pc=0x%08X\n",
                    idx, name, jit->Regs()[0], jit->Regs()[1], jit->Regs()[2], jit->Regs()[3], g_guest_lr, g_guest_pc);
            jit->Regs()[0] = 0;
        }
    }

    void AddTicks(std::uint64_t ticks) override {}
    std::uint64_t GetTicksRemaining() override { return 1000000000ull; }

    void ExceptionRaised(std::uint32_t pc, Dynarmic::A32::Exception exception) override {
        fprintf(stderr, "dynarmic: exception %d at pc=0x%08x\n", (int)exception, pc);
    }
};

struct dynarmic_host_s {
    elf32_image_t *img;
    HostCallbacks callbacks;
    std::unique_ptr<Dynarmic::A32::Jit> jit;
    std::array<std::uint8_t*, Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES> page_table;
    uint32_t stack_top;

    dynarmic_host_s(elf32_image_t *image)
        : img(image),
          callbacks(image),
          stack_top((image && image->mem_size >= 0x01000000u) ? ((image->mem_size - 64u) & ~7u) : 0x01FFFFF0u) {}
};

dynarmic_host_t *dynarmic_host_create(elf32_image_t *img) {
    auto host = new dynarmic_host_s(img);

    // Build absolute offset page table (maps all pages to img->mem)
    size_t num_pages = img->mem_size >> 12;
    for (size_t i = 0; i < Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES; ++i) {
        host->page_table[i] = (i < num_pages) ? img->mem : nullptr;
    }

    // Write HALT sentinel instructions at HALT_SENTINEL_LR (0x00000800)
    uint32_t arm_svc = 0xEF000000u | HALT_SVC_ARM;
    memcpy(img->mem + HALT_SENTINEL_LR, &arm_svc, 4);
    uint16_t thumb_svc = 0xDF00u | (uint16_t)HALT_SVC_THUMB;
    memcpy(img->mem + HALT_SENTINEL_LR + 4, &thumb_svc, 2);

    Dynarmic::A32::UserConfig config;
    config.callbacks = &host->callbacks;
    config.page_table = &host->page_table;
    config.absolute_offset_page_table = true;
    config.processor_id = 0;

    host->jit = std::make_unique<Dynarmic::A32::Jit>(config);
    host->callbacks.jit = host->jit.get();
    host->callbacks.InitHandlers();

    // Set initial stack pointer
    host->jit->Regs()[13] = host->stack_top;

    if (g_debug_mode) {
        printf("[+] dynarmic_host: initialized ARMv7 JIT with %zu SVC handlers (stack_top=0x%08X)\n",
               host->callbacks.handlers.size(), host->stack_top);
    }
    return host;
}

void dynarmic_host_destroy(dynarmic_host_t *host) {
    delete host;
}

uint32_t dynarmic_call(dynarmic_host_t *host, uint32_t entry_point, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3) {
    if (!host || !host->jit || entry_point == 0) return 0;

    auto *jit = host->jit.get();
    bool is_thumb = (entry_point & 1) != 0;
    uint32_t cpsr = jit->Cpsr();
    if (is_thumb) {
        cpsr |= (1u << 5);
    } else {
        cpsr &= ~(1u << 5);
    }
    jit->SetCpsr(cpsr);

    jit->Regs()[13] = host->stack_top;
    jit->Regs()[0] = r0;
    jit->Regs()[1] = r1;
    jit->Regs()[2] = r2;
    jit->Regs()[3] = r3;
    jit->Regs()[14] = HALT_SENTINEL_LR; // LR
    jit->Regs()[15] = entry_point & ~1u; // PC

    jit->ClearHalt();
    jit->Run();

    // Reset SP
    jit->Regs()[13] = host->stack_top;
    return jit->Regs()[0];
}

uint32_t dynarmic_call_stack(dynarmic_host_t *host, uint32_t entry_point, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, const uint32_t *stack_args, uint32_t stack_count) {
    if (!host || !host->jit || entry_point == 0) return 0;

    auto *jit = host->jit.get();
    bool is_thumb = (entry_point & 1) != 0;
    uint32_t cpsr = jit->Cpsr();
    if (is_thumb) {
        cpsr |= (1u << 5);
    } else {
        cpsr &= ~(1u << 5);
    }
    jit->SetCpsr(cpsr);

    uint32_t sp = host->stack_top;
    if (stack_count > 0 && stack_args) {
        sp -= stack_count * 4;
        sp &= ~7u; // 8-byte align
        for (uint32_t i = 0; i < stack_count; ++i) {
            memcpy(host->img->mem + sp + i * 4, &stack_args[i], 4);
        }
    }

    jit->Regs()[13] = sp;
    jit->Regs()[0] = r0;
    jit->Regs()[1] = r1;
    jit->Regs()[2] = r2;
    jit->Regs()[3] = r3;
    jit->Regs()[14] = HALT_SENTINEL_LR;
    jit->Regs()[15] = entry_point & ~1u;

    jit->ClearHalt();
    jit->Run();

    // Reset SP
    jit->Regs()[13] = host->stack_top;
    return jit->Regs()[0];
}
