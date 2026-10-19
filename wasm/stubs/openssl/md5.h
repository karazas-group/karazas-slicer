// WASM stub: only bbl_calc_md5() (project/3MF hashing) uses this; not needed for slicing.
#pragma once
#include <cstddef>
#define MD5_DIGEST_LENGTH 16
typedef struct { unsigned int state[4]; unsigned int count[2]; unsigned char buffer[64]; } MD5_CTX;
extern "C" { int MD5_Init(MD5_CTX*); int MD5_Update(MD5_CTX*, const void*, size_t); int MD5_Final(unsigned char*, MD5_CTX*); }
