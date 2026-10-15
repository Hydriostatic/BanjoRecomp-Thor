#ifndef __MODDING_H__
#define __MODDING_H__

// Section markers understood by RecompModTool.

// Declares a function that lives in another mod ("mod_id"), in the base game ("*"),
// or in this mod's own native library (".").
#define RECOMP_IMPORT(mod, func) \
    _Pragma("GCC diagnostic push") \
    _Pragma("GCC diagnostic ignored \"-Wunused-parameter\"") \
    __attribute__((noinline, weak, used, section(".recomp_import." mod))) func {} \
    _Pragma("GCC diagnostic pop")

#define RECOMP_EXPORT __attribute__((section(".recomp_export")))
#define RECOMP_PATCH __attribute__((section(".recomp_patch")))
#define RECOMP_FORCE_PATCH __attribute__((section(".recomp_force_patch")))

// Runs before / after a game function.
#define RECOMP_HOOK(func) __attribute__((section(".recomp_hook." func)))
#define RECOMP_HOOK_RETURN(func) __attribute__((section(".recomp_hook_return." func)))

// Registers a function as a callback for an event declared by a mod ("*" for the base game).
#define RECOMP_CALLBACK(mod, event) __attribute__((section(".recomp_callback." mod ":" #event)))

typedef unsigned char u8;
typedef signed int s32;
typedef unsigned int u32;
typedef float f32;

#ifndef NULL
#define NULL ((void*)0)
#endif

#endif
