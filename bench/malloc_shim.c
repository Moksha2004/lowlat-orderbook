// LD_PRELOAD shim: counts malloc calls while "tracking" is on.
// operator new routes through malloc in glibc/libstdc++, so this catches
// C++ allocations too.  Build:
//   gcc -shared -fPIC -O2 malloc_shim.c -o libshim.so -ldl
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>

static long alloc_count = 0;
static int  tracking    = 0;

void start_tracking(void) { tracking = 1; alloc_count = 0; }
long get_alloc_count(void) { return alloc_count; }

void* malloc(size_t size) {
    static void* (*real_malloc)(size_t) = 0;
    if (!real_malloc) real_malloc = (void* (*)(size_t))dlsym(RTLD_NEXT, "malloc");
    if (tracking) __sync_fetch_and_add(&alloc_count, 1);
    return real_malloc(size);
}
