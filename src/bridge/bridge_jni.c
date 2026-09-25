#include "bridge_jni.h"
#include "bridge_libc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define G_PTR(addr) ((addr) ? (img->mem + (addr)) : NULL)
#define READ_STACK(idx) (*(uint32_t*)(img->mem + sp + (idx) * 4))

static void write32(uint8_t *p, uint32_t val) {
    p[0] = (uint8_t)(val & 0xFF);
    p[1] = (uint8_t)((val >> 8) & 0xFF);
    p[2] = (uint8_t)((val >> 16) & 0xFF);
    p[3] = (uint8_t)((val >> 24) & 0xFF);
}

/* Screen resolution reported to the engine via JNI.
 * Set by bridge_jni_set_screen_size() from main.cpp after config load. */
static int g_jni_screen_width  = 1280;
static int g_jni_screen_height = 720;
static int s_jni_debug_mode = 0;

void bridge_jni_set_debug_mode(int enabled) {
    s_jni_debug_mode = enabled;
}

void bridge_jni_set_screen_size(int width, int height) {
    g_jni_screen_width  = (width  > 0) ? width  : 1280;
    g_jni_screen_height = (height > 0) ? height : 720;
}

/* -----------------------------------------------------------------------
 * Safe guest string allocator (60 KB ring buffer in [0x01FF0000, 0x01FFF000))
 *
 * LIFETIME & CONCURRENCY INVARIANT:
 * Strings returned to guest code via JNI (e.g. GetStringUTFChars, system property
 * lookups, and reflection queries) are transient data buffers consumed synchronously
 * by the guest caller before yielding control back to the engine loop. The 60 KB
 * ring buffer provides ample capacity for bursts of simultaneous active strings.
 * ----------------------------------------------------------------------- */
static uint32_t alloc_guest_string(elf32_image_t *img, const char *str) {
    if (!str) str = "";
    static uint32_t s_scratch = 0x01FF0000u;
    size_t len = strlen(str) + 1;
    uint32_t aligned_len = (uint32_t)((len + 3) & ~3u);
    if (s_scratch + aligned_len >= 0x01FFF000u) {
        s_scratch = 0x01FF0000u;
    }
    uint32_t addr = s_scratch;
    memcpy(img->mem + addr, str, len);
    s_scratch += aligned_len;
    return addr;
}

/* -----------------------------------------------------------------------
 * Dynamic Method-ID Registry
 * ----------------------------------------------------------------------- */
#define MAX_METHOD_IDS 512

enum JniReturnType {
    JNI_RET_INT = 0,    /* void, boolean, byte, char, short, int */
    JNI_RET_STRING = 1, /* java/lang/String */
    JNI_RET_OBJECT = 2, /* Java object reference handle */
    JNI_RET_FLOAT = 3,  /* float */
    JNI_RET_DOUBLE = 4, /* double */
    JNI_RET_LONG = 5    /* long (64-bit integer) */
};

typedef struct {
    char  name[64];
    char  sig[128];
    int   is_static;
    int   ret_type;      /* Return category (JniReturnType) */
    uint32_t ret_val;    /* default return value (int/bool/obj) */
    const char *ret_str; /* static string to return (or NULL) */
} jni_method_t;

static jni_method_t s_methods[MAX_METHOD_IDS];
static uint32_t s_num_methods = 0;
static int s_methods_inited = 0;

static void register_method_builtin(const char *name, const char *sig, int is_static, int ret_type, uint32_t ret_val, const char *ret_str) {
    if (s_num_methods >= MAX_METHOD_IDS) return;
    jni_method_t *m = &s_methods[s_num_methods++];
    strncpy(m->name, name, sizeof(m->name) - 1);
    m->name[sizeof(m->name) - 1] = '\0';
    strncpy(m->sig, sig ? sig : "", sizeof(m->sig) - 1);
    m->sig[sizeof(m->sig) - 1] = '\0';
    m->is_static = is_static;
    m->ret_type = ret_type;
    m->ret_val = ret_val;
    m->ret_str = ret_str;
}

static void ensure_methods_init(void) {
    if (s_methods_inited) return;
    s_methods_inited = 1;

    /* Base constructor */
    register_method_builtin("<init>", "()V", 0, 0, 0, NULL);

    /* Android & Client callbacks */
    register_method_builtin("IsConnectedToNetwork", "()Z", 1, 0, 1, NULL);
    register_method_builtin("GetDeviceLanguage", "()Ljava/lang/String;", 1, 1, 0, "en");
    register_method_builtin("GetControllerType", "()I", 1, 0, 0, NULL);
    register_method_builtin("GetBuildType", "()I", 1, 0, 0, NULL);
    register_method_builtin("IsZeus", "()Z", 1, 0, 0, NULL);
    register_method_builtin("IsDeviceZeus", "()Z", 1, 0, 0, NULL);
    register_method_builtin("GetDiagonalScreenSize", "()D", 1, 4, 0, NULL);
    register_method_builtin("GetDevicedensity", "()I", 1, 0, 160, NULL);
    register_method_builtin("ShowAlertInfo", "()V", 1, 0, 0, NULL);
    register_method_builtin("onEngineEvent", "()V", 1, 0, 0, NULL);
    register_method_builtin("IAPCallInit", "()V", 1, 0, 0, NULL);
    register_method_builtin("IAPCallBuy", "()V", 1, 0, 0, NULL);
    register_method_builtin("IAPCallRestore", "()V", 1, 0, 0, NULL);
    register_method_builtin("GetProductList", "()Ljava/lang/String;", 1, 1, 0, "");
    register_method_builtin("GetPurchasedList", "()Ljava/lang/String;", 1, 1, 0, "");
    register_method_builtin("IsNetworkConnected", "()Z", 1, 0, 1, NULL);
    register_method_builtin("GetPlatform", "()Ljava/lang/String;", 1, 1, 0, "pc");
    register_method_builtin("GetDeviceID", "()Ljava/lang/String;", 1, 1, 0, "pcdevice");
    register_method_builtin("GetDeviceModel", "()Ljava/lang/String;", 1, 1, 0, "PC");
    register_method_builtin("GetOSVersion", "()Ljava/lang/String;", 1, 1, 0, "10");
    register_method_builtin("GetAppVersion", "()Ljava/lang/String;", 1, 1, 0, "1.0");
    register_method_builtin("GetCountryCode", "()Ljava/lang/String;", 1, 1, 0, "US");
    register_method_builtin("IsTablet", "()Z", 1, 0, 0, NULL);
    register_method_builtin("HasTouchScreen", "()Z", 1, 0, 0, NULL);
    register_method_builtin("ShowLeaderboard", "()V", 1, 0, 0, NULL);
    register_method_builtin("ShowAchievements", "()V", 1, 0, 0, NULL);
    register_method_builtin("UnlockAchievement", "()V", 1, 0, 0, NULL);
    register_method_builtin("SubmitScore", "()V", 1, 0, 0, NULL);
    register_method_builtin("ShareScreenshot", "()V", 1, 0, 0, NULL);
    register_method_builtin("OpenURL", "()V", 1, 0, 0, NULL);
    register_method_builtin("onOpenURL", "(Ljava/lang/String;Ljava/lang/String;)V", 1, 0, 0, NULL);
    register_method_builtin("GetSaveData", "()Ljava/lang/String;", 1, 1, 0, "");
    register_method_builtin("SetSaveData", "()V", 1, 0, 0, NULL);
    register_method_builtin("DeleteSaveData", "()V", 1, 0, 0, NULL);
    register_method_builtin("GetStorageDirectory", "()Ljava/lang/String;", 1, 1, 0, ".");
    register_method_builtin("GetCacheDirectory", "()Ljava/lang/String;", 1, 1, 0, ".");
    register_method_builtin("IsFirstLaunch", "()Z", 1, 0, 0, NULL);
    register_method_builtin("GetScreenWidth", "()I", 1, 0, g_jni_screen_width, NULL);
    register_method_builtin("GetScreenHeight", "()I", 1, 0, g_jni_screen_height, NULL);
    register_method_builtin("IsConnected", "()Z", 1, 0, 1, NULL);
    register_method_builtin("GetLanguage", "()Ljava/lang/String;", 1, 1, 0, "en");
    register_method_builtin("POP2_SetBuildType", "()V", 1, 0, 0, NULL);
    register_method_builtin("POP2_InitMobileSDK", "()Z", 1, 0, 1, NULL);
    register_method_builtin("POP2_SetServerURL", "()V", 1, 0, 0, NULL);
    register_method_builtin("GetAnalyticsID", "()Ljava/lang/String;", 1, 1, 0, "pc_analytics");
    register_method_builtin("GetStoreURL", "()Ljava/lang/String;", 1, 1, 0, "");
    register_method_builtin("RateApp", "()V", 1, 0, 0, NULL);
    register_method_builtin("OnGameReady", "()V", 1, 0, 0, NULL);
    register_method_builtin("OnFirstFrame", "()V", 1, 0, 0, NULL);
    register_method_builtin("GetAdvertisingID", "()Ljava/lang/String;", 1, 1, 0, "");
    register_method_builtin("IsGooglePlayConnected", "()Z", 1, 0, 0, NULL);
    register_method_builtin("GetGPGSDisplay", "()Ljava/lang/String;", 1, 1, 0, "");
    register_method_builtin("getMainActivity", "()Landroid/app/Activity;", 1, 2, 0x45454545, NULL);
    register_method_builtin("getWindowManager", "()Landroid/view/WindowManager;", 0, 2, 0x46464646, NULL);
    register_method_builtin("getDefaultDisplay", "()Landroid/view/Display;", 0, 2, 0x47474747, NULL);
    register_method_builtin("getMetrics", "(Landroid/util/DisplayMetrics;)V", 0, 0, 0, NULL);
    register_method_builtin("getRotation", "()I", 0, 0, 0, NULL);
    register_method_builtin("isGPGSSignedIn", "()Z", 1, 0, 0, NULL);

    /* Audio / Music JNI callbacks */
    register_method_builtin("onInitSound", "()Z", 1, 0, 1, NULL);
    register_method_builtin("onLoadMusic", "(Ljava/lang/String;)I", 1, 0, 1, NULL);
    register_method_builtin("onPlayMusic", "(IFZF)I", 1, 0, 1, NULL);
    register_method_builtin("onSetMusicVolume", "(IF)V", 1, 0, 0, NULL);
    register_method_builtin("onStopMusic", "(I)V", 1, 0, 0, NULL);
    register_method_builtin("onResumeMusic", "(I)V", 1, 0, 0, NULL);
    register_method_builtin("onPauseMusic", "(I)V", 1, 0, 0, NULL);
    register_method_builtin("onUnloadMusic", "(I)V", 1, 0, 0, NULL);
    register_method_builtin("onLoadSound", "(Ljava/lang/String;)I", 1, 0, 1, NULL);
    register_method_builtin("onPlaySound", "(IFZF)I", 1, 0, 1, NULL);
    register_method_builtin("onSetSoundLooping", "(IZ)V", 1, 0, 0, NULL);
    register_method_builtin("onSetSoundPitch", "(IF)V", 1, 0, 0, NULL);
    register_method_builtin("onSetSoundVolume", "(IF)V", 1, 0, 0, NULL);
    register_method_builtin("onStopSound", "(I)V", 1, 0, 0, NULL);
    register_method_builtin("onResumeSound", "(I)V", 1, 0, 0, NULL);
    register_method_builtin("onPauseSound", "(I)V", 1, 0, 0, NULL);
    register_method_builtin("onUnloadSound", "(I)V", 1, 0, 0, NULL);
    register_method_builtin("onShutdownSound", "()V", 1, 0, 0, NULL);

    /* Analytics & Flurry */
    register_method_builtin("logEvent", "(Ljava/lang/String;)V", 1, 0, 0, NULL);
    register_method_builtin("logEventTimed", "(Ljava/lang/String;Z)V", 1, 0, 0, NULL);
    register_method_builtin("endTimedEvent", "(Ljava/lang/String;)V", 1, 0, 0, NULL);
    register_method_builtin("logEventWithParameters", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", 1, 0, 0, NULL);
    register_method_builtin("logEventWithParametersTimed", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", 1, 0, 0, NULL);
    register_method_builtin("onStartSession", "(Landroid/content/Context;Ljava/lang/String;)V", 1, 0, 0, NULL);
    register_method_builtin("onEndSession", "(Landroid/content/Context;)V", 1, 0, 0, NULL);
    register_method_builtin("LaunchAppRater", "()V", 1, 0, 0, NULL);
    register_method_builtin("ShowOfferAds", "()V", 1, 0, 0, NULL);
    register_method_builtin("CheckRewardCoins", "()V", 1, 0, 0, NULL);
    register_method_builtin("getItemsList", "()Ljava/lang/String;", 1, 1, 0, "");
    register_method_builtin("displayGPGSAchievement", "()V", 1, 0, 0, NULL);
    register_method_builtin("displayGPGSLeaderboard", "()V", 1, 0, 0, NULL);
    register_method_builtin("startPurchase", "(Ljava/lang/String;)V", 1, 0, 0, NULL);
    register_method_builtin("gpgSubmitScore", "(Ljava/lang/String;J)V", 1, 0, 0, NULL);
    register_method_builtin("gpgsUnlockAchievement", "(Ljava/lang/String;)V", 1, 0, 0, NULL);

    /* Storage / SharedPreferences */
    register_method_builtin("getSharedPreferences", "(Ljava/lang/String;I)Landroid/content/SharedPreferences;", 1, 2, 0x49494949, NULL);
    register_method_builtin("getPreferences", "(I)Landroid/content/SharedPreferences;", 1, 2, 0x49494949, NULL);
    register_method_builtin("edit", "()Landroid/content/SharedPreferences$Editor;", 0, 2, 0x4A4A4A4A, NULL);
    register_method_builtin("putString", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;", 0, 2, 0x4A4A4A4A, NULL);
    register_method_builtin("getString", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", 0, 1, 0, "");
    register_method_builtin("contains", "(Ljava/lang/String;)Z", 0, 0, 0, NULL);
    register_method_builtin("getFilesDir", "()Ljava/io/File;", 0, 2, 0x4B4B4B4B, NULL);
    register_method_builtin("getPath", "()Ljava/lang/String;", 0, 1, 0, ".");
    register_method_builtin("getAbsolutePath", "()Ljava/lang/String;", 0, 1, 0, ".");
    register_method_builtin("myLooper", "()Landroid/os/Looper;", 1, 2, 0x4C4C4C4C, NULL);
}

static uint32_t find_or_register_method(const char *name, const char *sig, int is_static) {
    if (!name || !name[0]) return 0;
    ensure_methods_init();

    for (uint32_t i = 0; i < s_num_methods; ++i) {
        if (strcmp(s_methods[i].name, name) == 0) {
            return i + 1;
        }
    }

    if (s_num_methods >= MAX_METHOD_IDS) {
        fprintf(stderr, "[-] JNI method registry full (%d)!\n", MAX_METHOD_IDS);
        return 0;
    }

    uint32_t id = ++s_num_methods;
    jni_method_t *m = &s_methods[id - 1];
    strncpy(m->name, name, sizeof(m->name) - 1);
    m->name[sizeof(m->name) - 1] = '\0';
    strncpy(m->sig, sig ? sig : "", sizeof(m->sig) - 1);
    m->sig[sizeof(m->sig) - 1] = '\0';
    m->is_static = is_static;
    m->ret_type = 0;
    m->ret_val = 0;
    m->ret_str = NULL;

    /* Parse signature for return type: (...)X */
    if (sig) {
        const char *p = strchr(sig, ')');
        if (p && *(p + 1)) {
            char r = *(p + 1);
            if (r == 'V') { m->ret_type = 0; m->ret_val = 0; }
            else if (r == 'Z') {
                m->ret_type = 0;
                if (strstr(name, "Connected") || strstr(name, "Available") || strstr(name, "Init") || strstr(name, "Ready")) {
                    m->ret_val = 1;
                } else {
                    m->ret_val = 0;
                }
            }
            else if (r == 'I' || r == 'S' || r == 'B' || r == 'C') {
                m->ret_type = 0;
                if (strcmp(name, "GetDevicedensity") == 0) m->ret_val = 160;
                else if (strcmp(name, "onLoadMusic") == 0 || strcmp(name, "onPlayMusic") == 0) m->ret_val = 1;
                else m->ret_val = 0;
            }
            else if (r == 'J') { m->ret_type = 5; m->ret_val = 0; }
            else if (r == 'F') { m->ret_type = 3; m->ret_val = 0; }
            else if (r == 'D') { m->ret_type = 4; m->ret_val = 0; }
            else if (r == 'L') {
                if (strstr(p + 1, "java/lang/String;")) {
                    m->ret_type = 1;
                    m->ret_str = "";
                } else {
                    m->ret_type = 2;
                    m->ret_val = 0x48484848; /* Dummy object handle */
                }
            }
            else if (r == '[') {
                m->ret_type = 2;
                m->ret_val = 0x48484848; /* Dummy array object */
            }
        }
    }

    if (s_jni_debug_mode) {
        fprintf(stderr, "[TRANSLATOR MISSING] JNI Unknown Method: '%s' sig='%s' static=%d -> auto-registered dynamic mid=%u (ret_type=%d, ret_val=0x%x)\n",
                name, sig ? sig : "", is_static, id, m->ret_type, m->ret_val);
    }
    return id;
}

static const jni_method_t *get_method(uint32_t id) {
    if (id == 0 || id > s_num_methods) return NULL;
    return &s_methods[id - 1];
}

/* -----------------------------------------------------------------------
 * Dynamic Field-ID Registry
 * ----------------------------------------------------------------------- */
#define MAX_FIELD_IDS 128

typedef struct {
    char name[64];
    char sig[64];
    int is_static;
    uint32_t val;
} jni_field_t;

static jni_field_t s_fields[MAX_FIELD_IDS];
static uint32_t s_num_fields = 0;

static uint32_t find_or_register_field(const char *name, const char *sig, int is_static) {
    if (!name || !name[0]) return 0;
    for (uint32_t i = 0; i < s_num_fields; ++i) {
        if (strcmp(s_fields[i].name, name) == 0) return i + 1;
    }
    if (s_num_fields >= MAX_FIELD_IDS) {
        fprintf(stderr, "[-] JNI field registry full (%d)!\n", MAX_FIELD_IDS);
        return 0;
    }
    uint32_t fid = ++s_num_fields;
    jni_field_t *f = &s_fields[fid - 1];
    strncpy(f->name, name, sizeof(f->name) - 1);
    f->name[sizeof(f->name) - 1] = '\0';
    strncpy(f->sig, sig ? sig : "", sizeof(f->sig) - 1);
    f->sig[sizeof(f->sig) - 1] = '\0';
    f->is_static = is_static;
    f->val = 0;

    if (strcmp(name, "widthPixels") == 0) f->val = g_jni_screen_width;
    else if (strcmp(name, "heightPixels") == 0) f->val = g_jni_screen_height;
    else if (strcmp(name, "densityDpi") == 0) f->val = 160;

    if (s_jni_debug_mode) {
        fprintf(stderr, "[TRANSLATOR MISSING] JNI Unknown Field: '%s' sig='%s' static=%d -> auto-registered dynamic fid=%u\n",
                name, sig ? sig : "", is_static, fid);
    }
    return fid;
}

/* -----------------------------------------------------------------------
 * JNI stub handlers
 * ----------------------------------------------------------------------- */
static uint32_t wrap_jni_ret0(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return 0;
}

static uint32_t wrap_jni_ret1(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return 1;
}

static uint32_t wrap_jni_FindClass(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    const char *cname = r1 ? (const char*)G_PTR(r1) : "<null>";
    if (s_jni_debug_mode) {
        fprintf(stderr, "[JNI] FindClass(%s)\n", cname);
    }
    return 0x41414141;
}

static uint32_t wrap_jni_GetMethodID(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    const char *name = r2 ? (const char*)G_PTR(r2) : "";
    const char *sig  = r3 ? (const char*)G_PTR(r3) : "";
    uint32_t mid = find_or_register_method(name, sig, 0);
    if (s_jni_debug_mode) {
        fprintf(stderr, "[JNI] GetMethodID('%s', '%s') -> %u\n", name, sig, mid);
    }
    return mid;
}

static uint32_t wrap_jni_GetStaticMethodID(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    const char *name = r2 ? (const char*)G_PTR(r2) : "";
    const char *sig  = r3 ? (const char*)G_PTR(r3) : "";
    uint32_t mid = find_or_register_method(name, sig, 1);
    if (s_jni_debug_mode) {
        fprintf(stderr, "[JNI] GetStaticMethodID('%s', '%s') -> %u\n", name, sig, mid);
    }
    return mid;
}

static uint32_t wrap_jni_GetFieldID(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    const char *fname = r2 ? (const char*)G_PTR(r2) : "";
    const char *fsig  = r3 ? (const char*)G_PTR(r3) : "";
    uint32_t fid = find_or_register_field(fname, fsig, 0);
    if (s_jni_debug_mode) {
        fprintf(stderr, "[JNI] GetFieldID('%s', '%s') -> fid=%u\n", fname, fsig, fid);
    }
    return fid;
}

static uint32_t wrap_jni_GetStaticFieldID(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    const char *fname = r2 ? (const char*)G_PTR(r2) : "";
    const char *fsig  = r3 ? (const char*)G_PTR(r3) : "";
    uint32_t fid = find_or_register_field(fname, fsig, 1);
    if (s_jni_debug_mode) {
        fprintf(stderr, "[JNI] GetStaticFieldID('%s', '%s') -> fid=%u\n", fname, fsig, fid);
    }
    return fid;
}

static uint32_t wrap_jni_GetIntField(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    uint32_t fid = r2;
    uint32_t val = (fid > 0 && fid <= s_num_fields) ? s_fields[fid - 1].val : 0;
    if (s_jni_debug_mode) {
        const char *fname = (fid > 0 && fid <= s_num_fields) ? s_fields[fid - 1].name : "?";
        static int s_logged_fids[MAX_FIELD_IDS] = {0};
        if (fid < MAX_FIELD_IDS && s_logged_fids[fid]++ < 5) {
            fprintf(stderr, "[JNI] GetIntField(fid=%u '%s') -> %u\n", fid, fname, val);
        }
    }
    return val;
}

static uint32_t wrap_jni_GetFloatField(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    uint32_t fid = r2;
    float fval = 1.0f;
    const char *fname = (fid > 0 && fid <= s_num_fields) ? s_fields[fid - 1].name : "?";
    if (strcmp(fname, "xdpi") == 0 || strcmp(fname, "ydpi") == 0) fval = 160.0f;
    else if (strcmp(fname, "density") == 0) fval = 1.0f;
    else fval = 0.0f;

    uint32_t ret;
    memcpy(&ret, &fval, 4);
    if (s_jni_debug_mode) {
        static int s_logged_ffids[MAX_FIELD_IDS] = {0};
        if (fid < MAX_FIELD_IDS && s_logged_ffids[fid]++ < 5) {
            fprintf(stderr, "[JNI] GetFloatField(fid=%u '%s') -> %.2f\n", fid, fname, fval);
        }
    }
    return ret;
}

static uint32_t wrap_jni_GetObjectField(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    if (s_jni_debug_mode) {
        uint32_t fid = r2;
        const char *fname = (fid > 0 && fid <= s_num_fields) ? s_fields[fid - 1].name : "?";
        fprintf(stderr, "[TRANSLATOR CALL] JNI GetObjectField(fid=%u '%s') -> 0\n", fid, fname);
    }
    return 0;
}

static uint32_t wrap_jni_GetStaticObjectField(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    if (s_jni_debug_mode) {
        uint32_t fid = r2;
        const char *fname = (fid > 0 && fid <= s_num_fields) ? s_fields[fid - 1].name : "?";
        fprintf(stderr, "[TRANSLATOR CALL] JNI GetStaticObjectField(fid=%u '%s') -> 0\n", fid, fname);
    }
    return 0;
}

/* --- Instance method calls --- */

static uint32_t wrap_jni_CallObjectMethodV(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    const jni_method_t *m = get_method(r2);
    const char *mname = m ? m->name : "?";
    if (s_jni_debug_mode) {
        static int s_logged[MAX_METHOD_IDS] = {0};
        if (r2 < MAX_METHOD_IDS && s_logged[r2]++ < 5) {
            fprintf(stderr, "[TRANSLATOR CALL] JNI CallObjectMethodV(mid=%u '%s')\n", r2, mname);
        }
    }

    if (strcmp(mname, "getWindowManager") == 0) return 0x46464646;
    if (strcmp(mname, "getDefaultDisplay") == 0) return 0x47474747;
    if (strcmp(mname, "getSharedPreferences") == 0 || strcmp(mname, "getPreferences") == 0) return 0x49494949;
    if (strcmp(mname, "edit") == 0) return 0x4A4A4A4A;
    if (strcmp(mname, "putString") == 0) return 0x4A4A4A4A;
    if (strcmp(mname, "getFilesDir") == 0) return 0x4B4B4B4B;

    if (m && m->ret_str) {
        return alloc_guest_string(img, m->ret_str);
    }
    if (m && m->ret_type == 1) {
        return alloc_guest_string(img, "");
    }
    if (m && m->ret_type == 2) {
        return m->ret_val ? m->ret_val : 0x48484848;
    }
    return 0;
}

static uint32_t wrap_jni_CallBooleanMethodV(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    const jni_method_t *m = get_method(r2);
    const char *mname = m ? m->name : "?";
    uint32_t val = m ? m->ret_val : 0;
    if (s_jni_debug_mode) {
        static int s_logged[MAX_METHOD_IDS] = {0};
        if (r2 < MAX_METHOD_IDS && s_logged[r2]++ < 5) {
            fprintf(stderr, "[TRANSLATOR CALL] JNI CallBooleanMethodV(mid=%u '%s') -> %u\n", r2, mname, val);
        }
    }
    return val;
}

static uint32_t wrap_jni_CallIntMethodV(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    const jni_method_t *m = get_method(r2);
    const char *mname = m ? m->name : "?";
    uint32_t val = m ? m->ret_val : 0;
    if (s_jni_debug_mode) {
        static int s_logged[MAX_METHOD_IDS] = {0};
        if (r2 < MAX_METHOD_IDS && s_logged[r2]++ < 5) {
            fprintf(stderr, "[TRANSLATOR CALL] JNI CallIntMethodV(mid=%u '%s') -> %u\n", r2, mname, val);
        }
    }
    return val;
}

static uint32_t wrap_jni_CallVoidMethodV(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    const jni_method_t *m = get_method(r2);
    const char *mname = m ? m->name : "?";
    if (s_jni_debug_mode) {
        static int s_logged[MAX_METHOD_IDS] = {0};
        if (r2 < MAX_METHOD_IDS && s_logged[r2]++ < 5) {
            fprintf(stderr, "[TRANSLATOR CALL] JNI CallVoidMethodV(mid=%u '%s')\n", r2, mname);
        }
    }
    return 0;
}

static uint32_t wrap_jni_CallLongMethodV(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    if (s_jni_debug_mode) {
        const jni_method_t *m = get_method(r2);
        fprintf(stderr, "[TRANSLATOR CALL] JNI CallLongMethodV(mid=%u '%s') -> 0\n", r2, m ? m->name : "?");
    }
    return 0;
}

/* --- Static method calls --- */

static uint32_t wrap_jni_CallStaticObjectMethodV(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    const jni_method_t *m = get_method(r2);
    const char *mname = m ? m->name : "?";
    if (s_jni_debug_mode) {
        static int s_logged[MAX_METHOD_IDS] = {0};
        if (r2 < MAX_METHOD_IDS && s_logged[r2]++ < 5) {
            fprintf(stderr, "[TRANSLATOR CALL] JNI CallStaticObjectMethodV(mid=%u '%s')\n", r2, mname);
        }
    }

    if (strcmp(mname, "getMainActivity") == 0) return 0x45454545;
    if (strcmp(mname, "myLooper") == 0) return 0x4C4C4C4C;

    if (m && m->ret_str) {
        return alloc_guest_string(img, m->ret_str);
    }
    if (m && m->ret_type == 1) {
        return alloc_guest_string(img, "");
    }
    if (m && m->ret_type == 2) {
        return m->ret_val ? m->ret_val : 0x48484848;
    }
    return 0;
}

static uint32_t wrap_jni_CallStaticBooleanMethod(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    const jni_method_t *m = get_method(r2);
    const char *mname = m ? m->name : "?";
    uint32_t val = m ? m->ret_val : 0;
    if (s_jni_debug_mode) {
        static int s_logged[MAX_METHOD_IDS] = {0};
        if (r2 < MAX_METHOD_IDS && s_logged[r2]++ < 5) {
            fprintf(stderr, "[TRANSLATOR CALL] JNI CallStaticBooleanMethod(mid=%u '%s') -> %u\n", r2, mname, val);
        }
    }
    return val;
}

static uint32_t wrap_jni_CallStaticIntMethod(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    const jni_method_t *m = get_method(r2);
    const char *mname = m ? m->name : "?";
    uint32_t val = m ? m->ret_val : 0;

    if (strcmp(mname, "onLoadMusic") == 0) {
        if (s_jni_debug_mode) {
            const char *mpath = r3 ? (const char*)G_PTR(r3) : "<null>";
            fprintf(stderr, "[TRANSLATOR AUDIO] onLoadMusic('%s') -> handle 1\n", mpath);
        }
        val = 1;
    } else if (strcmp(mname, "onPlayMusic") == 0) {
        if (s_jni_debug_mode) {
            fprintf(stderr, "[TRANSLATOR AUDIO] onPlayMusic(track=%u)\n", r3);
        }
        val = 1;
    }

    if (s_jni_debug_mode) {
        static int s_logged[MAX_METHOD_IDS] = {0};
        if (r2 < MAX_METHOD_IDS && s_logged[r2]++ < 5) {
            fprintf(stderr, "[TRANSLATOR CALL] JNI CallStaticIntMethod(mid=%u '%s') -> %u\n", r2, mname, val);
        }
    }
    return val;
}

static uint32_t wrap_jni_CallStaticLongMethodV(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    if (s_jni_debug_mode) {
        const jni_method_t *m = get_method(r2);
        fprintf(stderr, "[TRANSLATOR CALL] JNI CallStaticLongMethodV(mid=%u '%s') -> 0\n", r2, m ? m->name : "?");
    }
    return 0;
}

static uint32_t wrap_jni_CallStaticFloatMethodV(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    if (s_jni_debug_mode) {
        const jni_method_t *m = get_method(r2);
        fprintf(stderr, "[TRANSLATOR CALL] JNI CallStaticFloatMethodV(mid=%u '%s') -> 0.0f\n", r2, m ? m->name : "?");
    }
    return 0;
}

static uint32_t wrap_jni_CallStaticDoubleMethodV(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    const jni_method_t *m = get_method(r2);
    const char *mname = m ? m->name : "?";
    double ret_val = 5.0; /* Phone screen diagonal (< 7.0 inches -> selects iPhone 16:9 HUD layout) */
    if (r2 == 7 || strcmp(mname, "GetDiagonalScreenSize") == 0) {
        ret_val = 5.0;
    }
    uint32_t parts[2];
    memcpy(parts, &ret_val, 8);
    g_guest_ret_r1 = parts[1];
    g_has_guest_ret_r1 = 1;

    if (s_jni_debug_mode) {
        static int s_logged[MAX_METHOD_IDS] = {0};
        if (r2 < MAX_METHOD_IDS && s_logged[r2]++ < 5) {
            fprintf(stderr, "[TRANSLATOR CALL] JNI CallStaticDoubleMethodV(mid=%u '%s') -> %.2f\n", r2, mname, ret_val);
        }
    }
    return parts[0];
}

static uint32_t wrap_jni_CallStaticDoubleMethod(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return wrap_jni_CallStaticDoubleMethodV(img, r0, r1, r2, r3, sp);
}

static uint32_t wrap_jni_CallDoubleMethodV(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return wrap_jni_CallStaticDoubleMethodV(img, r0, r1, r2, r3, sp);
}

static uint32_t wrap_jni_CallStaticVoidMethodV(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    const jni_method_t *m = get_method(r2);
    const char *mname = m ? m->name : "?";
    if (s_jni_debug_mode) {
        static int s_logged[MAX_METHOD_IDS] = {0};
        if (r2 < MAX_METHOD_IDS && s_logged[r2]++ < 5) {
            fprintf(stderr, "[TRANSLATOR CALL] JNI CallStaticVoidMethodV(mid=%u '%s')\n", r2, mname);
        }
    }
    return 0;
}

static uint32_t wrap_jni_NewObjectV(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    if (s_jni_debug_mode) {
        fprintf(stderr, "[JNI] NewObjectV(class=0x%x, mid=%u)\n", r1, r2);
    }
    return 0x43434343;
}

static uint32_t wrap_jni_GetObjectClass(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return 0x44444444;
}

static uint32_t wrap_jni_NewGlobalRef(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return r1 ? r1 : 0x42424242;
}

static uint32_t wrap_jni_NewStringUTF(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return r1;
}

static uint32_t wrap_jni_GetStringUTFChars(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return r1;
}

static uint32_t wrap_jni_GetStringUTFLength(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    const char *s = (const char*)G_PTR(r1);
    return s ? (uint32_t)strlen(s) : 0;
}

static uint32_t wrap_jni_GetStringUTFRegion(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    uint32_t buf = READ_STACK(0);
    void *dst = G_PTR(buf);
    void *src = G_PTR(r1);
    if (dst && src) {
        memcpy(dst, (const uint8_t*)src + r2, r3);
        ((char*)dst)[r3] = '\0';
    }
    return 0;
}

static uint32_t wrap_jni_GetJavaVM(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    void *dst = G_PTR(r1);
    if (dst) {
        uint32_t vm = FAKE_VM_ADDR;
        memcpy(dst, &vm, 4);
    }
    return 0;
}

static uint32_t wrap_jvm_GetEnv(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    void *dst = G_PTR(r1);
    if (dst) {
        uint32_t env = FAKE_ENV_ADDR;
        memcpy(dst, &env, 4);
    }
    return 0;
}

static uint32_t wrap_jni_ExceptionCheck(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return 0; /* No exception */
}

static uint32_t wrap_jni_ExceptionClear(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return 0;
}

static uint32_t wrap_jni_Throw(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    if (s_jni_debug_mode) {
        fprintf(stderr, "[JNI] Throw called!\n");
    }
    return 0;
}

/* GetDirectBufferAddress(JNIEnv*, jobject buf) -> void*
 * We treat the jobject handle as the raw guest-memory address of the buffer data.
 * This lets engineOnTouchesChange find the touch float array we place there. */
static uint32_t wrap_jni_GetDirectBufferAddress(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return r1;  /* jobject IS the guest address of the buffer */
}

/* GetDirectBufferCapacity(JNIEnv*, jobject buf) -> jlong (lo in r0, hi in r1) */
static uint32_t wrap_jni_GetDirectBufferCapacity(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    return 256; /* generous capacity, hi word irrelevant */
}

static uint32_t wrap_jni_RegisterNatives(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    if (s_jni_debug_mode) {
        fprintf(stderr, "[JNI] RegisterNatives(class=0x%x, methods=0x%x, count=%u)\n", r1, r2, r3);
    }
    return 0;
}

static uint32_t wrap_jni_unhandled_slot(elf32_image_t *img, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t sp) {
    if (s_jni_debug_mode) {
        static int s_logged = 0;
        if (s_logged++ < 20) {
            fprintf(stderr, "[TRANSLATOR UNHANDLED JNI] Called unmapped JNI slot from lr=0x%08X (r1=0x%x, r2=0x%x, r3=0x%x)\n", g_guest_lr, r1, r2, r3);
        }
    }
    return 0;
}

struct JNIDispatch {
    const char *name;
    svc_handler_fn fn;
};

static const struct JNIDispatch s_jni_table[] = {
    {"jni_unhandled_slot",              wrap_jni_unhandled_slot},
    {"jni_ret0",                        wrap_jni_ret0},
    {"jni_ret1",                        wrap_jni_ret1},
    {"jni_FindClass",                   wrap_jni_FindClass},
    {"jni_GetMethodID",                 wrap_jni_GetMethodID},
    {"jni_GetStaticMethodID",           wrap_jni_GetStaticMethodID},
    {"jni_CallObjectMethodV",           wrap_jni_CallObjectMethodV},
    {"jni_CallBooleanMethodV",          wrap_jni_CallBooleanMethodV},
    {"jni_CallIntMethodV",              wrap_jni_CallIntMethodV},
    {"jni_CallVoidMethodV",             wrap_jni_CallVoidMethodV},
    {"jni_CallLongMethodV",             wrap_jni_CallLongMethodV},
    {"jni_CallDoubleMethodV",           wrap_jni_CallDoubleMethodV},
    {"jni_CallStaticObjectMethodV",     wrap_jni_CallStaticObjectMethodV},
    {"jni_CallStaticBooleanMethod",     wrap_jni_CallStaticBooleanMethod},
    {"jni_CallStaticIntMethod",         wrap_jni_CallStaticIntMethod},
    {"jni_CallStaticLongMethodV",       wrap_jni_CallStaticLongMethodV},
    {"jni_CallStaticFloatMethodV",      wrap_jni_CallStaticFloatMethodV},
    {"jni_CallStaticDoubleMethod",      wrap_jni_CallStaticDoubleMethod},
    {"jni_CallStaticDoubleMethodV",     wrap_jni_CallStaticDoubleMethodV},
    {"jni_CallStaticVoidMethodV",       wrap_jni_CallStaticVoidMethodV},
    {"jni_NewObjectV",                  wrap_jni_NewObjectV},
    {"jni_GetObjectClass",              wrap_jni_GetObjectClass},
    {"jni_NewGlobalRef",                wrap_jni_NewGlobalRef},
    {"jni_NewStringUTF",                wrap_jni_NewStringUTF},
    {"jni_GetStringUTFChars",           wrap_jni_GetStringUTFChars},
    {"jni_GetStringUTFLength",          wrap_jni_GetStringUTFLength},
    {"jni_GetStringUTFRegion",          wrap_jni_GetStringUTFRegion},
    {"jni_GetJavaVM",                   wrap_jni_GetJavaVM},
    {"jvm_GetEnv",                      wrap_jvm_GetEnv},
    {"jni_ExceptionCheck",              wrap_jni_ExceptionCheck},
    {"jni_ExceptionClear",              wrap_jni_ExceptionClear},
    {"jni_Throw",                       wrap_jni_Throw},
    {"jni_RegisterNatives",             wrap_jni_RegisterNatives},
    {"jni_GetFieldID",                  wrap_jni_GetFieldID},
    {"jni_GetStaticFieldID",            wrap_jni_GetStaticFieldID},
    {"jni_GetIntField",                 wrap_jni_GetIntField},
    {"jni_GetFloatField",               wrap_jni_GetFloatField},
    {"jni_GetObjectField",              wrap_jni_GetObjectField},
    {"jni_GetStaticObjectField",        wrap_jni_GetStaticObjectField},
    {"jni_GetDirectBufferAddress",      wrap_jni_GetDirectBufferAddress},
    {"jni_GetDirectBufferCapacity",     wrap_jni_GetDirectBufferCapacity},
    {NULL, NULL}
};

svc_handler_fn bridge_jni_lookup(const char *name) {
    for (int i = 0; s_jni_table[i].name != NULL; ++i) {
        if (strcmp(s_jni_table[i].name, name) == 0) {
            return s_jni_table[i].fn;
        }
    }
    return NULL;
}

void bridge_jni_setup(elf32_image_t *img) {
    ensure_methods_init();

    uint32_t t_ret0                 = elf32_add_trampoline(img, "jni_ret0");
    uint32_t t_ret1                 = elf32_add_trampoline(img, "jni_ret1");
    uint32_t t_FindClass            = elf32_add_trampoline(img, "jni_FindClass");
    uint32_t t_GetMethodID          = elf32_add_trampoline(img, "jni_GetMethodID");
    uint32_t t_GetStaticMethodID    = elf32_add_trampoline(img, "jni_GetStaticMethodID");
    uint32_t t_CallObjectV          = elf32_add_trampoline(img, "jni_CallObjectMethodV");
    uint32_t t_CallBoolV            = elf32_add_trampoline(img, "jni_CallBooleanMethodV");
    uint32_t t_CallIntV             = elf32_add_trampoline(img, "jni_CallIntMethodV");
    uint32_t t_CallVoidV            = elf32_add_trampoline(img, "jni_CallVoidMethodV");
    uint32_t t_CallLongV            = elf32_add_trampoline(img, "jni_CallLongMethodV");
    uint32_t t_CallDoubleV          = elf32_add_trampoline(img, "jni_CallDoubleMethodV");
    uint32_t t_CallStaticObjectV    = elf32_add_trampoline(img, "jni_CallStaticObjectMethodV");
    uint32_t t_CallStaticBool       = elf32_add_trampoline(img, "jni_CallStaticBooleanMethod");
    uint32_t t_CallStaticInt        = elf32_add_trampoline(img, "jni_CallStaticIntMethod");
    uint32_t t_CallStaticLongV      = elf32_add_trampoline(img, "jni_CallStaticLongMethodV");
    uint32_t t_CallStaticFloatV     = elf32_add_trampoline(img, "jni_CallStaticFloatMethodV");
    uint32_t t_CallStaticDouble     = elf32_add_trampoline(img, "jni_CallStaticDoubleMethod");
    uint32_t t_CallStaticDoubleV    = elf32_add_trampoline(img, "jni_CallStaticDoubleMethodV");
    uint32_t t_CallStaticVoidV      = elf32_add_trampoline(img, "jni_CallStaticVoidMethodV");
    uint32_t t_NewObjectV           = elf32_add_trampoline(img, "jni_NewObjectV");
    uint32_t t_GetObjectClass       = elf32_add_trampoline(img, "jni_GetObjectClass");
    uint32_t t_NewGlobalRef         = elf32_add_trampoline(img, "jni_NewGlobalRef");
    uint32_t t_NewStringUTF         = elf32_add_trampoline(img, "jni_NewStringUTF");
    uint32_t t_GetStringChars       = elf32_add_trampoline(img, "jni_GetStringUTFChars");
    uint32_t t_GetStringLen         = elf32_add_trampoline(img, "jni_GetStringUTFLength");
    uint32_t t_GetStringRegion      = elf32_add_trampoline(img, "jni_GetStringUTFRegion");
    uint32_t t_GetJavaVM            = elf32_add_trampoline(img, "jni_GetJavaVM");
    uint32_t t_jvm_GetEnv           = elf32_add_trampoline(img, "jvm_GetEnv");
    uint32_t t_ExceptionCheck       = elf32_add_trampoline(img, "jni_ExceptionCheck");
    uint32_t t_ExceptionClear       = elf32_add_trampoline(img, "jni_ExceptionClear");
    uint32_t t_Throw                = elf32_add_trampoline(img, "jni_Throw");
    uint32_t t_RegisterNatives      = elf32_add_trampoline(img, "jni_RegisterNatives");
    uint32_t t_GetFieldID           = elf32_add_trampoline(img, "jni_GetFieldID");
    uint32_t t_GetStaticFieldID     = elf32_add_trampoline(img, "jni_GetStaticFieldID");
    uint32_t t_GetIntField          = elf32_add_trampoline(img, "jni_GetIntField");
    uint32_t t_GetFloatField        = elf32_add_trampoline(img, "jni_GetFloatField");
    uint32_t t_GetObjectField       = elf32_add_trampoline(img, "jni_GetObjectField");
    uint32_t t_GetStaticObjectField = elf32_add_trampoline(img, "jni_GetStaticObjectField");
    uint32_t t_jni_unhandled        = elf32_add_trampoline(img, "jni_unhandled_slot");

    /* Initialize fake JavaVM (at FAKE_VM_ADDR) */
    memset(img->mem + FAKE_VM_ADDR, 0, 0x1000);
    write32(img->mem + FAKE_VM_ADDR + 0x00, FAKE_VM_ADDR); // *vm = fake_vm
    for (uint32_t off = 4; off < 0x40; off += 4) {
        write32(img->mem + FAKE_VM_ADDR + off, t_jni_unhandled);
    }
    write32(img->mem + FAKE_VM_ADDR + 0x10, t_ret0);       // DestroyJavaVM
    write32(img->mem + FAKE_VM_ADDR + 0x14, t_ret0);       // AttachCurrentThread
    write32(img->mem + FAKE_VM_ADDR + 0x18, t_jvm_GetEnv); // GetEnv

    /* Initialize fake JNIEnv (at FAKE_ENV_ADDR) */
    memset(img->mem + FAKE_ENV_ADDR, 0, 0x1000);
    write32(img->mem + FAKE_ENV_ADDR + 0x00, FAKE_ENV_ADDR); // *env = fake_env
    // Pre-populate all slots with unhandled handler so any unexpected JNI call is safely caught & logged
    for (uint32_t off = 4; off < 0x400; off += 4) {
        write32(img->mem + FAKE_ENV_ADDR + off, t_jni_unhandled);
    }
    write32(img->mem + FAKE_ENV_ADDR + 0x18, t_FindClass);
    write32(img->mem + FAKE_ENV_ADDR + 0x40, t_Throw);          // ThrowNew
    write32(img->mem + FAKE_ENV_ADDR + 0x44, t_ret0);           // ExceptionOccurred
    write32(img->mem + FAKE_ENV_ADDR + 0x48, t_ret0);           // ExceptionDescribe
    write32(img->mem + FAKE_ENV_ADDR + 0x4C, t_ExceptionClear); // ExceptionClear
    write32(img->mem + FAKE_ENV_ADDR + 0x54, t_ret0);           // PushLocalFrame
    write32(img->mem + FAKE_ENV_ADDR + 0x58, t_ret0);           // PopLocalFrame
    write32(img->mem + FAKE_ENV_ADDR + 0x5C, t_NewGlobalRef);
    write32(img->mem + FAKE_ENV_ADDR + 0x60, t_ret0);           // DeleteGlobalRef
    write32(img->mem + FAKE_ENV_ADDR + 0x64, t_ret0);           // DeleteLocalRef
    write32(img->mem + FAKE_ENV_ADDR + 0x74, t_NewObjectV);
    write32(img->mem + FAKE_ENV_ADDR + 0x7C, t_GetObjectClass);
    write32(img->mem + FAKE_ENV_ADDR + 0x84, t_GetMethodID);
    write32(img->mem + FAKE_ENV_ADDR + 0x8C, t_CallObjectV);    // CallObjectMethodV
    write32(img->mem + FAKE_ENV_ADDR + 0x98, t_CallBoolV);      // CallBooleanMethodV
    write32(img->mem + FAKE_ENV_ADDR + 0xC8, t_CallIntV);       // CallIntMethodV
    write32(img->mem + FAKE_ENV_ADDR + 0xD4, t_CallLongV);      // CallLongMethodV
    write32(img->mem + FAKE_ENV_ADDR + 0xE8, t_CallDoubleV);    // CallDoubleMethodV
    write32(img->mem + FAKE_ENV_ADDR + 0xF8, t_CallVoidV);      // CallVoidMethodV
    write32(img->mem + FAKE_ENV_ADDR + 0x178, t_GetFieldID);       // GetFieldID
    write32(img->mem + FAKE_ENV_ADDR + 0x17C, t_GetObjectField);   // GetObjectField
    write32(img->mem + FAKE_ENV_ADDR + 0x180, t_ret1);          // GetBooleanField
    write32(img->mem + FAKE_ENV_ADDR + 0x190, t_GetIntField);       // GetIntField
    write32(img->mem + FAKE_ENV_ADDR + 0x198, t_GetFloatField);     // GetFloatField
    write32(img->mem + FAKE_ENV_ADDR + 0x1C4, t_GetStaticMethodID);
    write32(img->mem + FAKE_ENV_ADDR + 0x1CC, t_CallStaticObjectV);  // CallStaticObjectMethodV
    write32(img->mem + FAKE_ENV_ADDR + 0x1D8, t_CallStaticBool);     // CallStaticBooleanMethodV
    write32(img->mem + FAKE_ENV_ADDR + 0x208, t_CallStaticInt);      // CallStaticIntMethodV
    write32(img->mem + FAKE_ENV_ADDR + 0x21C, t_CallStaticLongV);
    write32(img->mem + FAKE_ENV_ADDR + 0x220, t_CallStaticFloatV);
    write32(img->mem + FAKE_ENV_ADDR + 0x228, t_CallStaticDouble);
    write32(img->mem + FAKE_ENV_ADDR + 0x22C, t_CallStaticDoubleV);
    write32(img->mem + FAKE_ENV_ADDR + 0x238, t_CallStaticVoidV);
    write32(img->mem + FAKE_ENV_ADDR + 0x240, t_GetStaticFieldID);     // GetStaticFieldID
    write32(img->mem + FAKE_ENV_ADDR + 0x244, t_GetStaticObjectField); // GetStaticObjectField
    write32(img->mem + FAKE_ENV_ADDR + 0x29C, t_NewStringUTF);
    write32(img->mem + FAKE_ENV_ADDR + 0x2A0, t_GetStringLen);
    write32(img->mem + FAKE_ENV_ADDR + 0x2A4, t_GetStringChars);
    write32(img->mem + FAKE_ENV_ADDR + 0x2A8, t_ret0);          // ReleaseStringUTFChars
    write32(img->mem + FAKE_ENV_ADDR + 0x2AC, t_ret0);          // GetArrayLength
    write32(img->mem + FAKE_ENV_ADDR + 0x2B4, t_ret0);          // GetObjectArrayElement
    write32(img->mem + FAKE_ENV_ADDR + 0x35C, t_RegisterNatives);
    write32(img->mem + FAKE_ENV_ADDR + 0x364, t_ret0);          // UnregisterNatives
    write32(img->mem + FAKE_ENV_ADDR + 0x368, t_ret0);          // MonitorEnter
    write32(img->mem + FAKE_ENV_ADDR + 0x36C, t_ret0);       // MonitorExit  → no-op
    write32(img->mem + FAKE_ENV_ADDR + 0x370, t_GetJavaVM);  // GetJavaVM    → correct slot
    write32(img->mem + FAKE_ENV_ADDR + 0x374, t_GetStringRegion);
    write32(img->mem + FAKE_ENV_ADDR + 0x3A4, t_ExceptionCheck);

    /* JNI 1.4 NIO direct-buffer support (needed for engineOnTouchesChange) */
    uint32_t t_GetDirectBufAddr     = elf32_add_trampoline(img, "jni_GetDirectBufferAddress");
    uint32_t t_GetDirectBufCap      = elf32_add_trampoline(img, "jni_GetDirectBufferCapacity");
    /* NewDirectByteBuffer: treat the address (r1) as the jobject returned - so the engine
     * can later pass it back to GetDirectBufferAddress which will just return r1 again. */
    write32(img->mem + FAKE_ENV_ADDR + 0x37C, t_GetDirectBufAddr);  /* NewDirectByteBuffer -> return r1 */
    write32(img->mem + FAKE_ENV_ADDR + 0x380, t_GetDirectBufAddr);  /* GetDirectBufferAddress */
    write32(img->mem + FAKE_ENV_ADDR + 0x384, t_GetDirectBufCap);   /* GetDirectBufferCapacity */

    if (s_jni_debug_mode) {
        printf("[+] bridge_jni: Android JavaVM and JNIEnv function tables set up with dynamic registry!\n");
    }
}
