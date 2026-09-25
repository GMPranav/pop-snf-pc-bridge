#include "bridge_openal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <AL/al.h>
#include <AL/alc.h>

static ALCdevice *g_al_device = NULL;
static ALCcontext *g_al_context = NULL;

int bridge_openal_init(void) {
    if (g_al_device && g_al_context) return 1;
    g_al_device = alcOpenDevice(NULL);
    if (!g_al_device) {
        fprintf(stderr, "bridge_openal: failed to open default audio device\n");
        return 0;
    }
    g_al_context = alcCreateContext(g_al_device, NULL);
    if (!g_al_context || alcMakeContextCurrent(g_al_context) == ALC_FALSE) {
        fprintf(stderr, "bridge_openal: failed to create / activate audio context\n");
        if (g_al_context) alcDestroyContext(g_al_context);
        alcCloseDevice(g_al_device);
        g_al_device = NULL;
        g_al_context = NULL;
        return 0;
    }
    printf("[+] bridge_openal: initialized OpenAL audio successfully\n");
    return 1;
}

void bridge_openal_shutdown(void) {
    if (g_al_context) {
        alcMakeContextCurrent(NULL);
        alcDestroyContext(g_al_context);
        g_al_context = NULL;
    }
    if (g_al_device) {
        alcCloseDevice(g_al_device);
        g_al_device = NULL;
    }
}

#define G_PTR(addr) ((addr) ? (img->mem + (addr)) : NULL)
#define READ_STACK(idx) (*(uint32_t*)(img->mem + sp + (idx) * 4))

static uint32_t wrap_alGenBuffers(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    alGenBuffers(r0, (ALuint*)G_PTR(r1));
    return 0;
}

static uint32_t wrap_alDeleteBuffers(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    alDeleteBuffers(r0, (const ALuint*)G_PTR(r1));
    return 0;
}

static uint32_t wrap_alBufferData(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    uint32_t freq = READ_STACK(0);
    alBufferData((ALuint)r0, (ALenum)r1, G_PTR(r2), (ALsizei)r3, (ALsizei)freq);
    return 0;
}

static uint32_t wrap_alGenSources(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    alGenSources(r0, (ALuint*)G_PTR(r1));
    return 0;
}

static uint32_t wrap_alDeleteSources(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    alDeleteSources(r0, (const ALuint*)G_PTR(r1));
    return 0;
}

static uint32_t wrap_alSourcePlay(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    alSourcePlay((ALuint)r0);
    return 0;
}

static uint32_t wrap_alSourcePause(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    alSourcePause((ALuint)r0);
    return 0;
}

static uint32_t wrap_alSourceStop(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    alSourceStop((ALuint)r0);
    return 0;
}

static uint32_t wrap_alSourceRewind(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    alSourceRewind((ALuint)r0);
    return 0;
}

static uint32_t wrap_alSourcei(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    alSourcei((ALuint)r0, (ALenum)r1, (ALint)r2);
    return 0;
}

static uint32_t wrap_alSourcef(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    float val;
    memcpy(&val, &r2, 4);
    alSourcef((ALuint)r0, (ALenum)r1, val);
    return 0;
}

static uint32_t wrap_alSource3f(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    float v1, v2, v3;
    memcpy(&v1, &r2, 4);
    memcpy(&v2, &r3, 4);
    uint32_t s0 = READ_STACK(0);
    memcpy(&v3, &s0, 4);
    alSource3f((ALuint)r0, (ALenum)r1, v1, v2, v3);
    return 0;
}

static uint32_t wrap_alSourcefv(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    alSourcefv((ALuint)r0, (ALenum)r1, (const ALfloat*)G_PTR(r2));
    return 0;
}

static uint32_t wrap_alGetSourcei(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    alGetSourcei((ALuint)r0, (ALenum)r1, (ALint*)G_PTR(r2));
    return 0;
}

static uint32_t wrap_alSourceQueueBuffers(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    alSourceQueueBuffers((ALuint)r0, (ALsizei)r1, (ALuint*)G_PTR(r2));
    return 0;
}

static uint32_t wrap_alSourceUnqueueBuffers(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    alSourceUnqueueBuffers((ALuint)r0, (ALsizei)r1, (ALuint*)G_PTR(r2));
    return 0;
}

static uint32_t wrap_alListenerf(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    float val;
    memcpy(&val, &r1, 4);
    alListenerf((ALenum)r0, val);
    return 0;
}

static uint32_t wrap_alListener3f(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    float x, y, z;
    memcpy(&x, &r1, 4);
    memcpy(&y, &r2, 4);
    memcpy(&z, &r3, 4);
    alListener3f((ALenum)r0, x, y, z);
    return 0;
}

static uint32_t wrap_alListenerfv(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    alListenerfv((ALenum)r0, (const ALfloat*)G_PTR(r1));
    return 0;
}

static uint32_t wrap_alGetError(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return (uint32_t)alGetError();
}

static uint32_t wrap_alGetBufferi(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    alGetBufferi((ALuint)r0, (ALenum)r1, (ALint*)G_PTR(r2));
    return 0;
}

static uint32_t wrap_alIsBuffer(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return (uint32_t)alIsBuffer((ALuint)r0);
}

static uint32_t wrap_alcOpenDevice(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    bridge_openal_init();
    /* Return synthetic non-null OpenAL device handle for guest address space */
    return 0x12340001;
}

static uint32_t wrap_alcCloseDevice(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return ALC_TRUE;
}

static uint32_t wrap_alcCreateContext(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    /* Return synthetic non-null OpenAL context handle for guest address space */
    return 0x12340002;
}

static uint32_t wrap_alcMakeContextCurrent(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return ALC_TRUE;
}

static uint32_t wrap_alcProcessContext(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return 0;
}

static uint32_t wrap_alcSuspendContext(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return 0;
}

static uint32_t wrap_alcDestroyContext(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return 0;
}

static uint32_t wrap_alcGetCurrentContext(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return 0x12340002;
}

static uint32_t wrap_alcGetContextsDevice(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return 0x12340001;
}

static uint32_t wrap_alcGetError(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return ALC_NO_ERROR;
}

static uint32_t wrap_alGetString(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return 0;
}

static uint32_t wrap_alIsExtensionPresent(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return AL_FALSE;
}

static uint32_t wrap_alGetProcAddress(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return 0;
}

static uint32_t wrap_alcGetString(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return 0;
}

static uint32_t wrap_alcIsExtensionPresent(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return ALC_FALSE;
}

static uint32_t wrap_alcGetIntegerv(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return 0;
}

static uint32_t wrap_alSource3i(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    alSource3i((ALuint)r0, (ALenum)r1, (ALint)r2, (ALint)r3, (ALint)READ_STACK(0));
    return 0;
}

static uint32_t wrap_alcCaptureStub(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return 0;
}

struct OpenALDispatch {
    const char *name;
    svc_handler_fn fn;
};

static const struct OpenALDispatch s_al_table[] = {
    {"alGenBuffers", wrap_alGenBuffers},
    {"alDeleteBuffers", wrap_alDeleteBuffers},
    {"alBufferData", wrap_alBufferData},
    {"alGenSources", wrap_alGenSources},
    {"alDeleteSources", wrap_alDeleteSources},
    {"alSourcePlay", wrap_alSourcePlay},
    {"alSourcePause", wrap_alSourcePause},
    {"alSourceStop", wrap_alSourceStop},
    {"alSourceRewind", wrap_alSourceRewind},
    {"alSourcei", wrap_alSourcei},
    {"alSourcef", wrap_alSourcef},
    {"alSource3f", wrap_alSource3f},
    {"alSource3i", wrap_alSource3i},
    {"alSourcefv", wrap_alSourcefv},
    {"alGetSourcei", wrap_alGetSourcei},
    {"alSourceQueueBuffers", wrap_alSourceQueueBuffers},
    {"alSourceUnqueueBuffers", wrap_alSourceUnqueueBuffers},
    {"alListenerf", wrap_alListenerf},
    {"alListener3f", wrap_alListener3f},
    {"alListenerfv", wrap_alListenerfv},
    {"alGetError", wrap_alGetError},
    {"alGetBufferi", wrap_alGetBufferi},
    {"alIsBuffer", wrap_alIsBuffer},
    {"alGetString", wrap_alGetString},
    {"alIsExtensionPresent", wrap_alIsExtensionPresent},
    {"alGetProcAddress", wrap_alGetProcAddress},
    {"alcOpenDevice", wrap_alcOpenDevice},
    {"alcCloseDevice", wrap_alcCloseDevice},
    {"alcCreateContext", wrap_alcCreateContext},
    {"alcMakeContextCurrent", wrap_alcMakeContextCurrent},
    {"alcProcessContext", wrap_alcProcessContext},
    {"alcSuspendContext", wrap_alcSuspendContext},
    {"alcDestroyContext", wrap_alcDestroyContext},
    {"alcGetCurrentContext", wrap_alcGetCurrentContext},
    {"alcGetContextsDevice", wrap_alcGetContextsDevice},
    {"alcGetError", wrap_alcGetError},
    {"alcGetString", wrap_alcGetString},
    {"alcIsExtensionPresent", wrap_alcIsExtensionPresent},
    {"alcGetIntegerv", wrap_alcGetIntegerv},
    {"alcCaptureCloseDevice", wrap_alcCaptureStub},
    {"alcCaptureOpenDevice", wrap_alcCaptureStub},
    {"alcCaptureSamples", wrap_alcCaptureStub},
    {"alcCaptureStart", wrap_alcCaptureStub},
    {"alcCaptureStop", wrap_alcCaptureStub},
    {NULL, NULL}
};

svc_handler_fn bridge_openal_lookup(const char *name) {
    for (int i = 0; s_al_table[i].name != NULL; ++i) {
        if (strcmp(s_al_table[i].name, name) == 0) {
            return s_al_table[i].fn;
        }
    }
    return NULL;
}
