/*
 * neo-pi - minimal C library shim for the Geolith core on Circle (bare metal)
 *
 * Circle provides memcpy/memset/strlen/malloc/... ; newlib provides the
 * headers, libm (sin) and setjmp/longjmp (extracted from libc.a at build
 * time). Everything else the core references is implemented or stubbed here.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/reent.h>

#include "neopi.h"

/* ---- stderr / exit, used only on fatal paths of Musashi ---------------- */

static struct _reent s_reent;
struct _reent *_impure_ptr = &s_reent;

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

int fprintf(FILE *stream, const char *fmt, ...) {
    (void)stream;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    neopi_log("core: %s", buf);
    return n;
}

void exit(int code) {
    neopi_fatal(NEOPI_ERR_CORE, "core exit(%d)", code);
    for (;;) { }
}

/* ---- time: fixed date for the MVS calendar chip (no RTC on the Pi) ----- */

time_t time(time_t *t) {
    if (t) *t = 0;
    return 0;
}

struct tm *localtime(const time_t *t) {
    (void)t;
    static struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 9;      /* October */
    tm.tm_mday = 28;
    tm.tm_hour = 12;
    tm.tm_wday = 3;     /* Wednesday */
    return &tm;
}

/* ---- strings ------------------------------------------------------------ */

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    do {
        if (*s == (char)c) last = s;
    } while (*s++);
    return (char*)last;
}

/* ---- minimal vsnprintf: %s %c %d %i %u %x %X %p %%, flags 0-, width,
        length modifiers l ll z h (enough for the core's log messages) ---- */

static void out_ch(char *buf, size_t size, size_t *pos, char c) {
    if (*pos + 1 < size) buf[*pos] = c;
    (*pos)++;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    size_t pos = 0;
    for (; *fmt; ++fmt) {
        if (*fmt != '%') { out_ch(buf, size, &pos, *fmt); continue; }
        ++fmt;
        int left = 0, zero = 0, width = 0, lng = 0;
        for (;; ++fmt) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else break;
        }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') { ++fmt; while (*fmt >= '0' && *fmt <= '9') ++fmt; }
        for (;; ++fmt) {
            if (*fmt == 'l') lng++;
            else if (*fmt == 'z') lng = 2;
            else if (*fmt == 'h') { }
            else break;
        }
        char tmp[32];
        const char *s = tmp;
        int len = 0;
        switch (*fmt) {
        case 's':
            s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            len = (int)strlen(s);
            break;
        case 'c':
            tmp[0] = (char)va_arg(ap, int);
            len = 1;
            break;
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'p': {
            unsigned long long v;
            int neg = 0;
            unsigned base = (*fmt == 'x' || *fmt == 'X' || *fmt == 'p') ? 16 : 10;
            if (*fmt == 'p') v = (uintptr_t)va_arg(ap, void*);
            else if (*fmt == 'd' || *fmt == 'i') {
                long long sv = lng >= 2 ? va_arg(ap, long long)
                             : lng == 1 ? va_arg(ap, long) : va_arg(ap, int);
                if (sv < 0) { neg = 1; v = (unsigned long long)(-sv); }
                else v = (unsigned long long)sv;
            }
            else {
                v = lng >= 2 ? va_arg(ap, unsigned long long)
                  : lng == 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            }
            const char *dig = *fmt == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char rev[24];
            int n = 0;
            do { rev[n++] = dig[v % base]; v /= base; } while (v);
            if (neg) tmp[len++] = '-';
            while (n) tmp[len++] = rev[--n];
            break;
        }
        case '%':
            tmp[0] = '%';
            len = 1;
            break;
        case 0:
            --fmt;
            continue;
        default:
            tmp[0] = '?';
            len = 1;
            break;
        }
        int pad = width > len ? width - len : 0;
        if (!left) while (pad-- > 0) out_ch(buf, size, &pos, zero ? '0' : ' ');
        for (int i = 0; i < len; ++i) out_ch(buf, size, &pos, s[i]);
        if (left) while (pad-- > 0) out_ch(buf, size, &pos, ' ');
    }
    if (size) buf[pos < size ? pos : size - 1] = 0;
    return (int)pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

/* ---- stdio: only referenced by Geolith's default VFS backend, which
        neo-pi never uses (files are read by the frontend). ------------- */

FILE *fopen(const char *path, const char *mode) { (void)path; (void)mode; return NULL; }
int fclose(FILE *f) { (void)f; return 0; }
size_t fread(void *p, size_t s, size_t n, FILE *f) { (void)p; (void)s; (void)n; (void)f; return 0; }
size_t fwrite(const void *p, size_t s, size_t n, FILE *f) { (void)p; (void)s; (void)n; (void)f; return 0; }
int fseeko(FILE *f, off_t o, int w) { (void)f; (void)o; (void)w; return -1; }
off_t ftello(FILE *f) { (void)f; return -1; }
int fseek(FILE *f, long o, int w) { (void)f; (void)o; (void)w; return -1; }
long ftell(FILE *f) { (void)f; return -1; }
int fflush(FILE *f) { (void)f; return 0; }
