//
//  m3_api_libc.c
//
//  Created by Volodymyr Shymanskyy on 11/20/19.
//  Copyright © 2019 Volodymyr Shymanskyy. All rights reserved.
//

#define _POSIX_C_SOURCE 200809L

#include "m3_api_libc.h"

#include "m3_env.h"
#include "m3_exception.h"

#include <time.h>
#include <errno.h>
#include <stdio.h>


typedef uint32_t wasm_ptr_t;
typedef uint32_t wasm_size_t;

m3ApiRawFunction(m3_libc_abort)
{
    m3ApiTrap(m3Err_trapAbort);
}

m3ApiRawFunction(m3_libc_exit)
{
    m3ApiGetArg     (int32_t, code)

    m3ApiTrap(m3Err_trapExit);
}


m3ApiRawFunction(m3_libc_memset)
{
    m3ApiReturnType (int32_t)

    m3ApiGetArgMem  (uint32_t,        i_offset)
    m3ApiGetArg     (int32_t,         i_value)
    m3ApiGetArg     (wasm_size_t,     i_size)

    m3ApiCheckMem(i_offset, i_size);

    uint8_t* tmp = (uint8_t*)alloca(i_size);
    memset(tmp, i_value, i_size);
    m3ApiMemStore(i_offset, tmp, i_size);
    m3ApiReturn(i_offset);
}

m3ApiRawFunction(m3_libc_memmove)
{
    m3ApiReturnType (int32_t)

    m3ApiGetArgMem  (uint32_t,        o_dst)
    m3ApiGetArgMem  (uint32_t,        i_src)
    m3ApiGetArg     (wasm_size_t,     i_size)

    m3ApiCheckMem(o_dst, i_size);
    m3ApiCheckMem(i_src, i_size);

    uint8_t* tmp = (uint8_t*)alloca(i_size);
    m3ApiMemLoad(tmp, i_src, i_size);
    m3ApiMemStore(o_dst, tmp, i_size);
    m3ApiReturn(o_dst);
}

m3ApiRawFunction(m3_libc_print)
{
    m3ApiReturnType (uint32_t)

    m3ApiGetArgMem  (uint32_t,        i_offset)
    m3ApiGetArg     (wasm_size_t,     i_size)

    m3ApiCheckMem(i_offset, i_size);

    uint8_t* tmp = (uint8_t*)alloca(i_size);
    m3ApiMemLoad(tmp, i_offset, i_size);
    fwrite(tmp, i_size, 1, stdout);
    fflush(stdout);

    m3ApiReturn(i_size);
}

static
void internal_itoa(int n, char s[], int radix)
{
    static char const HEXDIGITS[0x10] = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
    };

    int i, j, sign;
    char c;

    if ((sign = n) < 0) { n = -n; }
    i = 0;
    do {
        s[i++] = HEXDIGITS[n % radix];
    } while ((n /= radix) > 0);

    if (sign < 0) { s[i++] = '-'; }
    s[i] = '\0';

    // reverse
    for (i = 0, j = strlen(s)-1; i<j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

m3ApiRawFunction(m3_libc_printf)
{
    m3ApiReturnType (int32_t)

    m3ApiGetArgMem  (uint32_t,       i_fmt_offset)
    m3ApiGetArgMem  (uint32_t,       i_args_offset)

    if (m3ApiIsNullPtr(i_fmt_offset)) {
        m3ApiReturn(0);
    }

    m3ApiCheckMem(i_fmt_offset, 1);
    // Load format string from wasm memory
    char fmt_buf[1025];
    m3ApiMemLoad(fmt_buf, i_fmt_offset, 1024);
    fmt_buf[1024] = '\0';
    size_t fmt_len = strnlen(fmt_buf, 1024);
    m3ApiCheckMem(i_fmt_offset, fmt_len+1);

    FILE* file = stdout;

    int32_t length = 0;
    uint32_t fmt_pos = 0;
    uint32_t args_pos = 0;
    char ch;
    while (fmt_pos <= fmt_len && (ch = fmt_buf[fmt_pos++])) {
        if ( '%' != ch ) {
            putc(ch, file);
            length++;
            continue;
        }
        ch = fmt_buf[fmt_pos++];
        switch (ch) {
            case 'c': {
                m3ApiCheckMem(i_args_offset + args_pos, sizeof(wasm_ptr_t));
                wasm_ptr_t arg_val = m3ApiReadMem32(i_args_offset + args_pos);
                args_pos += sizeof(wasm_ptr_t);
                char char_temp = (char)arg_val;
                fputc(char_temp, file);
                length++;
                break;
            }
            case 'd':
            case 'x': {
                m3ApiCheckMem(i_args_offset + args_pos, sizeof(wasm_ptr_t));
                int int_temp = (int)m3ApiReadMem32(i_args_offset + args_pos);
                args_pos += sizeof(wasm_ptr_t);
                char buffer[32] = { 0, };
                internal_itoa(int_temp, buffer, (ch == 'x') ? 16 : 10);
                fputs(buffer, file);
                length += strnlen(buffer, sizeof(buffer));
                break;
            }
            case 's': {
                m3ApiCheckMem(i_args_offset + args_pos, sizeof(wasm_ptr_t));
                uint32_t string_offset = m3ApiReadMem32(i_args_offset + args_pos);
                args_pos += sizeof(wasm_ptr_t);
                size_t string_len;

                if (m3ApiIsNullPtr(string_offset)) {
                    const char* null_str = "(null)";
                    fwrite(null_str, 1, 6, file);
                    length += 6;
                } else {
                    char str_buf[1025];
                    m3ApiMemLoad(str_buf, string_offset, 1024);
                    str_buf[1024] = '\0';
                    string_len = strnlen(str_buf, 1024);
                    m3ApiCheckMem(string_offset, string_len+1);

                    fwrite(str_buf, 1, string_len, file);
                    length += string_len;
                }
                break;
            default:
                fputc(ch, file);
                length++;
                break;
            }
        }
    }

    m3ApiReturn(length);
}

m3ApiRawFunction(m3_libc_clock_ms)
{
    m3ApiReturnType (uint32_t)
#ifdef CLOCKS_PER_SEC
    uint32_t clock_divider = CLOCKS_PER_SEC/1000;
    if (clock_divider != 0) {
        m3ApiReturn(clock() / clock_divider);
    } else {
        m3ApiReturn(clock());
    }
#else
    m3ApiReturn(clock());
#endif
}

static
M3Result  SuppressLookupFailure (M3Result i_result)
{
    if (i_result == m3Err_functionLookupFailed)
        return m3Err_none;
    else
        return i_result;
}

m3ApiRawFunction(m3_spectest_dummy)
{
    m3ApiSuccess();
}

M3Result  m3_LinkSpecTest  (IM3Module module)
{
    M3Result result = m3Err_none;

    const char* spectest = "spectest";

_   (SuppressLookupFailure (m3_LinkRawFunction (module, spectest, "print",         "v()",      &m3_spectest_dummy)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, spectest, "print_i32",     "v(i)",     &m3_spectest_dummy)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, spectest, "print_i64",     "v(I)",     &m3_spectest_dummy)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, spectest, "print_f32",     "v(f)",     &m3_spectest_dummy)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, spectest, "print_f64",     "v(F)",     &m3_spectest_dummy)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, spectest, "print_i32_f32", "v(if)",    &m3_spectest_dummy)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, spectest, "print_i64_f64", "v(IF)",    &m3_spectest_dummy)));

_catch:
    return result;
}


M3Result  m3_LinkLibC  (IM3Module module)
{
    M3Result result = m3Err_none;

    const char* env = "env";

_   (SuppressLookupFailure (m3_LinkRawFunction (module, env, "_debug",            "i(*i)",   &m3_libc_print)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, env, "_memset",           "*(*ii)",  &m3_libc_memset)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, env, "_memmove",          "*(**i)",  &m3_libc_memmove)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, env, "_memcpy",           "*(**i)",  &m3_libc_memmove))); // just alias of memmove
_   (SuppressLookupFailure (m3_LinkRawFunction (module, env, "_abort",            "v()",     &m3_libc_abort)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, env, "_exit",             "v(i)",    &m3_libc_exit)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, env, "clock_ms",          "i()",     &m3_libc_clock_ms)));
_   (SuppressLookupFailure (m3_LinkRawFunction (module, env, "printf",            "i(**)",   &m3_libc_printf)));

_catch:
    return result;
}

