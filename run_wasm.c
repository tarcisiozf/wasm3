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
#include "m3_api_wasi.h"
#include "wasm3.h"
#include "m3_env.h"
#include "m3_memory.h"

#define STACK_SIZE_BYTES  (64 * 1024)   /* 64 KB interpreter stack */

int http_get(const char* url, char* buffer, const size_t buf_size);

/* Print a human-readable error and exit. */
static void fatal(const char *step, M3Result err) {
    fprintf(stderr, "[wasm3] %s failed: %s\n", step, err);
    exit(1);
}

const void* http_req(IM3Runtime rt, IM3ImportContext ctx, uint64_t* sp) {
    printf("SDK http_req called\n");fflush(stdout);

    M3Result result = m3Err_none;
    uint32_t offset = (uint32_t)sp[1];
    uint32_t len = (uint32_t)sp[2];

    char url[len+1];
    url[len] = '\0';
    result = memLoad(&rt->memory, url, offset, len);
    if (result) return result;

    printf("SDK http_req called with URL: %s\n", url);fflush(stdout);

    uint64_t ok = 0;

    size_t buf_size = 256;
    char buf[buf_size];
    if (http_get(url, buf, buf_size) != 0) {
        printf("HTTP GET request failed\n");
        goto end;
    }
    size_t response_len = strnlen(buf, buf_size);

    printf("HTTP GET response (%lu): %s\n", response_len, buf);

    IM3Function reserve;
    if (m3_FindFunction(&reserve, rt, "reserve")) {
        printf("Error finding 'reserve' function\n");
        goto end;
    }

    if (m3_CallV(reserve, response_len)) {
        printf("Error calling 'reserve' function\n");
        goto end;
    }

    uint32_t dest;
    if (m3_GetResultsV(reserve, &dest)) {
        printf("Error getting result from 'reserve' function\n");
        goto end;
    }

    result = memStore(&rt->memory, buf, dest, response_len);
    if (result) {
        printf("Error writing response to wasm memory\n");
        goto end;
    }

    ok = 1;

    end:
        sp[0] = ok;
    return m3Err_none;
}

const void* yield(IM3Runtime rt, IM3ImportContext ctx, uint64_t* sp) {
    printf("yield called\n");
    return m3Err_none;
}

const void* set_value(IM3Runtime rt, IM3ImportContext ctx, uint64_t* sp) {
    printf("set_value called\n");
    int32_t* value_ptr = (int32_t*)rt->userdata;
    if (value_ptr) {
        printf("Setting value at %p to %d\n", (void*)value_ptr, (int32_t)sp[0]);
        *value_ptr = (int32_t)sp[0];
    }
    return m3Err_none;
}

const void* wasi_sched_yield_stub(IM3Runtime rt, IM3ImportContext ctx, uint64_t* sp) {
    printf("WASI sched_yield called\n");fflush(stdout);
    sp[0] = 0;
    return m3Err_none;
}

const void* wasi_poll_oneoff_stub(IM3Runtime rt, IM3ImportContext ctx, uint64_t* sp) {
    printf("WASI poll_oneoff called\n");fflush(stdout);

    // poll_oneoff(in: i32, out: i32, nsubscriptions: i32, nevents_ptr: i32) -> errno
    // sp[0] = return (errno)
    // sp[1] = in_offset
    // sp[2] = out_offset
    // sp[3] = nsubscriptions
    // sp[4] = nevents_offset
    uint32_t in_offset      = (uint32_t)sp[1];
    uint32_t out_offset     = (uint32_t)sp[2];
    uint32_t nsubscriptions = (uint32_t)sp[3];
    uint32_t nevents_offset = (uint32_t)sp[4];

    // For each subscription, read userdata+type and write a corresponding event
    // subscription: 48 bytes (userdata@0 = u64, type@8 = u8, ...)
    // event:        32 bytes (userdata@0 = u64, error@8 = u16, type@10 = u8, ...)
    for (uint32_t i = 0; i < nsubscriptions; i++) {
        uint32_t sub_base = in_offset + i * 48;
        uint32_t evt_base = out_offset + i * 32;

        // Read subscription userdata (8 bytes) and type (1 byte at offset 8)
        uint8_t sub_data[48];
        memLoad(&rt->memory, sub_data, sub_base, 48);
        uint64_t userdata = *(uint64_t*)&sub_data[0];
        uint8_t  type     = sub_data[8];

        // Write event: zero it first, then fill fields
        uint8_t evt[32];
        memset(evt, 0, 32);
        *(uint64_t*)&evt[0]  = userdata;  // userdata @ 0
        *(uint16_t*)&evt[8]  = 0;         // error @ 8 = success
        evt[10]              = type;       // type @ 10
        memStore(&rt->memory, evt, evt_base, 32);
    }

    // Write nevents = nsubscriptions
    memStore(&rt->memory, &nsubscriptions, nevents_offset, sizeof(uint32_t));

    sp[0] = 0; // __WASI_ERRNO_SUCCESS
    return m3Err_none;
}

void print_unresolved_imports(IM3Runtime runtime) {
    IM3Module module = runtime->modules;
    if (!module) return;
    printf("numFuncImports: %d, numFunctions: %d\n", module->numFuncImports, module->numFunctions);
    for (uint32_t i = 0; i < module->numFuncImports; i++) {
        IM3Function f = &module->functions[i];
        if (f->compiled == 0) {
            printf("IMPORT[%d]: %s.%s compiled=%p\n", i,
                f->import.moduleUtf8 ? f->import.moduleUtf8 : "(null)",
                f->import.fieldUtf8 ? f->import.fieldUtf8 : "(null)",
                (void*)f->compiled);
        }
    }
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

    result = m3_LinkRawFunction(runtime->modules, "wasi_snapshot_preview1", "sched_yield", "i()", wasi_sched_yield_stub);
    if (result) printf("sched_yield link: %s\n", result);

    result = m3_LinkRawFunction(runtime->modules, "wasi_snapshot_preview1", "poll_oneoff", "i(iiii)", wasi_poll_oneoff_stub);
    if (result) printf("poll_oneoff link: %s\n", result);

    result = m3_LinkWASI(runtime->modules);
    if (result) fatal("m3_LinkWASI", result);

    result = m3_LinkRawFunction(runtime->modules, "sdk", "http_req", "i(ii)", http_req);
    if (result) fatal("sdk.http_req", result);

    result = m3_LinkRawFunction(runtime->modules, "sdk", "yield", "v()", yield);
    if (result) fatal("sdk.yield", result);

    result = m3_LinkRawFunction(runtime->modules, "sdk", "set_value", "v(i)", set_value);
    if (result) fatal("sdk.set_value", result);

    print_unresolved_imports(runtime);

    runtime->userdata = malloc(sizeof(int32_t));

    /* ── Set up WASI context (argc/argv) ────────────────────────────────── */
    m3_wasi_context_t* wasi_ctx = m3_GetWasiContext();
    if (wasi_ctx) {
        wasi_ctx->argc = argc;
        wasi_ctx->argv = (const char **)argv;
    }

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
        /* Handle WASI proc_exit gracefully */
        if (result == m3Err_trapExit) {
            m3_wasi_context_t* ctx = m3_GetWasiContext();
            int exit_code = ctx ? ctx->exit_code : 0;
            if (exit_code != 0) {
                fprintf(stderr, "[wasm3] program exited with code %d\n", exit_code);
            }
            printf("FINAL: %d\n", runtime->userdata ? *(int32_t*)runtime->userdata : -1);
            m3_FreeRuntime(runtime);
            m3_FreeEnvironment(env);
            free(wasm);
            return 0;
        }

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

    printf("FINAL: %d\n", runtime->userdata ? *(int32_t*)runtime->userdata : -1);

    /* ── 7. Clean up ─────────────────────────────────────────────────────── */
    m3_FreeRuntime(runtime);
    m3_FreeEnvironment(env);
    free(wasm);

    return 0;
}

