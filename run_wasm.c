/*
 * run_wasm.c - Minimal wasm3 runner
 *
 * Usage: ./run_wasm <path/to/module.wasm> [function_name] [args...]
 *
 * If no function is specified, it tries to call "_start" (WASI convention).
 *
 * Build:
 *   cd build && cmake .. && make
 *   gcc -o run_wasm ../run_wasm.c -I../source -L. -lm3 -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "m3_api_libc.h"
#include "wasm3.h"
#include "m3_env.h"

#define STACK_SIZE_BYTES  (64 * 1024)   /* 64 KB interpreter stack */

/* Print a human-readable error and exit. */
static void fatal(const char *step, M3Result err) {
    fprintf(stderr, "[wasm3] %s failed: %s\n", step, err);
    exit(1);
}

const void* foo(IM3Runtime rt, IM3ImportContext ctx, uint64_t* sp) {
    printf("foo called\n");
    return m3Err_none;
}

M3Result link_to_runtime(IM3Runtime rt, const char* moduleName, const char* funcName, const char* sig, const M3RawCall fn) {
    const TaggedUserData userdata = { .tag = 0x42 };
    return m3_LinkRawFunctionEx(rt->modules, moduleName, funcName, sig, fn, &userdata);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.wasm> [function] [args...]\n", argv[0]);
        return 1;
    }

    const char *wasm_path   = argv[1];
    const char *func_name   = (argc >= 3) ? argv[2] : "_start";
    int         extra_argc  = (argc >= 3) ? argc - 3 : 0;
    const char **extra_argv = (const char **)(argv + 3);

    /* ── 1. Load the .wasm file from disk ──────────────────────────────── */
    FILE *f = fopen(wasm_path, "rb");
    if (!f) { perror(wasm_path); return 1; }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);

    uint8_t *wasm = (uint8_t *)malloc(fsize);
    if (!wasm) { fprintf(stderr, "Out of memory\n"); return 1; }

    if ((long)fread(wasm, 1, fsize, f) != fsize) {
        fprintf(stderr, "Failed to read %s\n", wasm_path);
        return 1;
    }
    fclose(f);

    /* ── 2. Set up the wasm3 environment and runtime ────────────────────── */
    IM3Environment env = m3_NewEnvironment();
    if (!env) fatal("m3_NewEnvironment", "returned NULL");

    IM3Runtime runtime = m3_NewRuntime(env, STACK_SIZE_BYTES, NULL);
    if (!runtime) fatal("m3_NewRuntime", "returned NULL");

    /* ── 3. Parse and load the module ───────────────────────────────────── */
    IM3Module module;
    M3Result result;

    result = m3_ParseModule(env, &module, wasm, (uint32_t)fsize);
    if (result) fatal("m3_ParseModule", result);

    result = m3_LoadModule(runtime, module);
    if (result) fatal("m3_LoadModule", result);
    result = m3_LinkSpecTest(runtime->modules);
    if (result) fatal("m3_LinkSpecTest", result);

    result = link_to_runtime(runtime, "env", "foo", "v(i)", foo);
    if (result) fatal("link_to_runtime", result);

    /* ── 4. Optionally run the start section (like __wasm_call_ctors) ───── */
    result = m3_RunStart(module);
    /* m3_RunStart returns an error when there is no start function – ignore it */

    /* ── 5. Find and call the requested function ─────────────────────────── */
    IM3Function func;
    result = m3_FindFunction(&func, runtime, func_name);
    if (result) {
        fprintf(stderr, "[wasm3] m3_FindFunction(\"%s\") failed: %s\n", func_name, result);
        return 1;
    }

    /* Pass any extra command-line tokens as string arguments to the function. */
    result = m3_CallArgv(func, (uint32_t)extra_argc, extra_argv);
    if (result) {
        M3ErrorInfo info;
        m3_GetErrorInfo(runtime, &info);
        fprintf(stderr, "[wasm3] call to \"%s\" failed: %s\n", func_name, result);
        if (info.message && strlen(info.message))
            fprintf(stderr, "        detail: %s\n", info.message);
        return 1;
    }

    /* ── 6. Print return values (if any) ─────────────────────────────────── */
    uint32_t ret_count = m3_GetRetCount(func);
    if (ret_count > 0) {
        /* Allocate one 64-bit slot per return value and fetch them all at once. */
        uint64_t *ret_vals = (uint64_t *)calloc(ret_count, sizeof(uint64_t));
        const void **ret_ptrs = (const void **)malloc(ret_count * sizeof(void *));
        for (uint32_t i = 0; i < ret_count; i++) ret_ptrs[i] = &ret_vals[i];

        result = m3_GetResults(func, ret_count, ret_ptrs);
        if (result) fatal("m3_GetResults", result);

        for (uint32_t i = 0; i < ret_count; i++) {
            M3ValueType type = m3_GetRetType(func, i);
            switch (type) {
                case c_m3Type_i32: printf("i32: %u\n",   (uint32_t)ret_vals[i]);              break;
                case c_m3Type_i64: printf("i64: %llu\n", (unsigned long long)ret_vals[i]);    break;
                case c_m3Type_f32: { float  v; memcpy(&v, &ret_vals[i], 4); printf("f32: %f\n", v); break; }
                case c_m3Type_f64: { double v; memcpy(&v, &ret_vals[i], 8); printf("f64: %f\n", v); break; }
                default: printf("(unknown return type %d)\n", type); break;
            }
        }
        free(ret_vals);
        free(ret_ptrs);
    }

    /* ── 7. Clean up ─────────────────────────────────────────────────────── */
    m3_FreeRuntime(runtime);
    m3_FreeEnvironment(env);
    free(wasm);

    return 0;
}

