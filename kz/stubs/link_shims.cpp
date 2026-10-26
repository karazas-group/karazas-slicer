// Link-time shims for the wasm build.
#include <cstdlib>
#include <cstring>
#include <cstddef>

extern "C" {
// tbbmalloc: libslic3r uses tbb::scalable_allocator (Point.hpp, clipper.hpp, SupportLayer.hpp).
// Emscripten's mimalloc (-sMALLOC=mimalloc) is already a thread-caching allocator, so forward to malloc.
void *scalable_malloc(size_t n) { return std::malloc(n); }
void  scalable_free(void *p) { std::free(p); }
void *scalable_realloc(void *p, size_t n) { return std::realloc(p, n); }
void *scalable_calloc(size_t n, size_t s) { return std::calloc(n, s); }
int   scalable_posix_memalign(void **m, size_t a, size_t s) { return posix_memalign(m, a, s); }
void *scalable_aligned_malloc(size_t s, size_t a) { void *p = nullptr; return posix_memalign(&p, a < sizeof(void*) ? sizeof(void*) : a, s) ? nullptr : p; }
void  scalable_aligned_free(void *p) { std::free(p); }
size_t scalable_msize(void *) { return 0; }

// OpenSSL MD5: only bbl_calc_md5() (3MF project hashing) – never called when slicing.
typedef struct { unsigned int state[4]; unsigned int count[2]; unsigned char buffer[64]; } MD5_CTX;
int MD5_Init(MD5_CTX *c) { std::memset(c, 0, sizeof(*c)); return 1; }
int MD5_Update(MD5_CTX *, const void *, size_t) { return 1; }
int MD5_Final(unsigned char *d, MD5_CTX *) { std::memset(d, 0, 16); return 1; }
}

// Emscripten's libc has no pthread_setname_np (Thread.cpp names TBB worker threads).
#include <pthread.h>
extern "C" int pthread_setname_np(pthread_t, const char *) { return 0; }
