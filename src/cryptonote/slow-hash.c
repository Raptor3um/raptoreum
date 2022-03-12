// Copyright (c) 2021 The Raptoreum Project
// Copyright (c) 2012-2013 The Cryptonote developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
// Portions Copyright (c) 2018 The Monero developers
// Portions Copyright (c) 2018 The TurtleCoin Developers

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <cryptonote/slow-hash.h>
#include <cryptonote/oaes_lib.h>
#include <cryptonote/c_keccak.h>
#include <cryptonote/c_groestl.h>
#include <cryptonote/c_blake256.h>
#include <cryptonote/c_jh.h>
#include <cryptonote/c_skein.h>
#include <cryptonote/int-util.h>
#include <cryptonote/variant2_int_sqrt.h>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

#define AES_BLOCK_SIZE  16
#define AES_KEY_SIZE    32 /*16*/
#define INIT_SIZE_BLK   8
#define INIT_SIZE_BYTE  (INIT_SIZE_BLK * AES_BLOCK_SIZE)

#define VARIANT1_1(p) \
  do if (variant == 1) \
  { \
    const uint8_t tmp = ((const uint8_t*)(p))[11]; \
    static const uint32_t table = 0x75310; \
    const uint8_t index = (((tmp >> 3) & 6) | (tmp & 1)) << 1; \
    ((uint8_t*)(p))[11] = tmp ^ ((table >> index) & 0x30); \
  } while(0)

#define VARIANT1_2(p) \
   do if (variant == 1) \
   { \
     ((uint64_t*)p)[1] ^= tweak1_2; \
   } while(0)

#define VARIANT1_INIT() \
  if (variant == 1 && len < 43) \
  { \
    fprintf(stderr, "Cryptonight variant 1 needs at least 43 bytes of data"); \
    _exit(1); \
  } \
  const uint64_t tweak1_2 = (variant == 1) ? *(const uint64_t*)(((const uint8_t*)input)+35) ^ state.hs.w[24] : 0

#define U64(p) ((uint64_t*)(p))

#define VARIANT2_INIT(b, state) \
  uint64_t division_result; \
  uint64_t sqrt_result; \
  do if (variant >= 2) \
  { \
    U64(b)[2] = state.hs.w[8] ^ state.hs.w[10]; \
    U64(b)[3] = state.hs.w[9] ^ state.hs.w[11]; \
    division_result = state.hs.w[12]; \
    sqrt_result = state.hs.w[13]; \
  } while (0)

#define VARIANT2_SHUFFLE_ADD(base_ptr, offset, a, b) \
  do if (variant >= 2) \
  { \
    uint64_t* chunk1 = U64((base_ptr) + ((offset) ^ 0x10)); \
    uint64_t* chunk2 = U64((base_ptr) + ((offset) ^ 0x20)); \
    uint64_t* chunk3 = U64((base_ptr) + ((offset) ^ 0x30)); \
    \
    const uint64_t chunk1_old[2] = { chunk1[0], chunk1[1] }; \
    \
    chunk1[0] = chunk3[0] + U64(b + 16)[0]; \
    chunk1[1] = chunk3[1] + U64(b + 16)[1]; \
    \
    chunk3[0] = chunk2[0] + U64(a)[0]; \
    chunk3[1] = chunk2[1] + U64(a)[1]; \
    \
    chunk2[0] = chunk1_old[0] + U64(b)[0]; \
    chunk2[1] = chunk1_old[1] + U64(b)[1]; \
    } while (0)

#define VARIANT2_INTEGER_MATH_DIVISION_STEP(b, ptr) \
  ((uint64_t*)(b))[0] ^= division_result ^ (sqrt_result << 32); \
  { \
    const uint64_t dividend = ((uint64_t*)(ptr))[1]; \
    const uint32_t divisor = (((uint32_t*)(ptr))[0] + (uint32_t)(sqrt_result << 1)) | 0x80000001UL; \
    division_result = ((uint32_t)(dividend / divisor)) + \
                     (((uint64_t)(dividend % divisor)) << 32); \
  } \
  const uint64_t sqrt_input = ((uint64_t*)(ptr))[0] + division_result

#define VARIANT2_INTEGER_MATH(b, ptr) \
    do if (variant >= 2) \
    { \
      VARIANT2_INTEGER_MATH_DIVISION_STEP(b, ptr); \
      VARIANT2_INTEGER_MATH_SQRT_STEP_FP64(); \
      VARIANT2_INTEGER_MATH_SQRT_FIXUP(sqrt_result); \
    } while (0)

#define VARIANT2_2() \
  do if (variant >= 2) { \
    ((uint64_t*)(long_state + ((j * AES_BLOCK_SIZE) ^ 0x10)))[0] ^= hi; \
    ((uint64_t*)(long_state + ((j * AES_BLOCK_SIZE) ^ 0x10)))[1] ^= lo; \
    hi ^= ((uint64_t*)(long_state + ((j * AES_BLOCK_SIZE) ^ 0x20)))[0]; \
    lo ^= ((uint64_t*)(long_state + ((j * AES_BLOCK_SIZE) ^ 0x20)))[1]; \
  } while (0)

#pragma pack(push, 1)
union cn_slow_hash_state {
    union hash_state hs;
    struct {
        uint8_t k[64];
        uint8_t init[INIT_SIZE_BYTE];
    };
};
#pragma pack(pop)

static void do_blake_hash(const void* input, size_t len, char* output) {
    blake256_hash((uint8_t*)output, input, len);
}

void do_groestl_hash(const void* input, size_t len, char* output) {
    groestl(input, len * 8, (uint8_t*)output);
}

static void do_jh_hash(const void* input, size_t len, char* output) {
    int r = jh_hash(HASH_SIZE * 8, input, 8 * len, (uint8_t*)output);
    assert(SUCCESS == r);
}

static void do_skein_hash(const void* input, size_t len, char* output) {
    int r = c_skein_hash(8 * HASH_SIZE, input, 8 * len, (uint8_t*)output);
    assert(SKEIN_SUCCESS == r);
}

static void (* const extra_hashes[4])(const void *, size_t, char *) = {
    do_blake_hash, do_groestl_hash, do_jh_hash, do_skein_hash
};

extern int aesb_single_round(const uint8_t *in, uint8_t *out, const uint8_t *expandedKey);
extern int aesb_pseudo_round(const uint8_t *in, uint8_t *out, const uint8_t *expandedKey);

static inline size_t e2i(const uint8_t* a, size_t count) {
    return (*((uint64_t*) a) / AES_BLOCK_SIZE) & (count - 1);
}

static void mul(const uint8_t* a, const uint8_t* b, uint8_t* res) {
    ((uint64_t*) res)[1] = mul128(((uint64_t*) a)[0], ((uint64_t*) b)[0], (uint64_t*) res);
}

static void sum_half_blocks(uint8_t* a, const uint8_t* b) {
    uint64_t a0, a1, b0, b1;

    a0 = SWAP64LE(((uint64_t*) a)[0]);
    a1 = SWAP64LE(((uint64_t*) a)[1]);
    b0 = SWAP64LE(((uint64_t*) b)[0]);
    b1 = SWAP64LE(((uint64_t*) b)[1]);
    a0 += b0;
    a1 += b1;
    ((uint64_t*) a)[0] = SWAP64LE(a0);
    ((uint64_t*) a)[1] = SWAP64LE(a1);
}

static inline void copy_block(uint8_t* dst, const uint8_t* src) {
    ((uint64_t*) dst)[0] = ((uint64_t*) src)[0];
    ((uint64_t*) dst)[1] = ((uint64_t*) src)[1];
}

static void swap_blocks(uint8_t* a, uint8_t* b) {
    size_t i;
    uint8_t t;
    for (i = 0; i < AES_BLOCK_SIZE; i++) {
        t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}

static inline void xor_blocks(uint8_t* a, const uint8_t* b) {
    ((uint64_t*) a)[0] ^= ((uint64_t*) b)[0];
    ((uint64_t*) a)[1] ^= ((uint64_t*) b)[1];
}

static inline void xor_blocks_dst(const uint8_t* a, const uint8_t* b, uint8_t* dst) {
    ((uint64_t*) dst)[0] = ((uint64_t*) a)[0] ^ ((uint64_t*) b)[0];
    ((uint64_t*) dst)[1] = ((uint64_t*) a)[1] ^ ((uint64_t*) b)[1];
}

void cn_slow_hash(const char* input, char* output, int len, int variant, uint32_t page_size, uint32_t iterations, size_t aes_rounds)
{
  union cn_slow_hash_state state;
  uint8_t text[INIT_SIZE_BYTE];
  uint8_t a[AES_BLOCK_SIZE];
  uint8_t b[AES_BLOCK_SIZE * 2];
  uint8_t c[AES_BLOCK_SIZE];
  uint8_t aes_key[AES_KEY_SIZE];
  oaes_ctx* aes_ctx;

  size_t init_rounds = (page_size / INIT_SIZE_BYTE);

#if defined(_MSC_VER)
  uint8_t *long_state = (uint8_t *)_malloca(page_size);
#else
#if defined(__APPLE__)
  uint8_t *long_state = (uint8_t *)calloc(page_size, sizeof(uint8_t));
#else
  uint8_t *long_state = (uint8_t *)malloc(page_size);
#endif
#endif
  hash_process(&state.hs, (const uint8_t*) input, len);
  memcpy(text, state.init, INIT_SIZE_BYTE);
  memcpy(aes_key, state.hs.b, AES_KEY_SIZE);
  aes_ctx = (oaes_ctx*) oaes_alloc();
  size_t i, j;

  VARIANT1_INIT();
  VARIANT2_INIT(b, state);

  oaes_key_import_data(aes_ctx, aes_key, AES_KEY_SIZE);
  for (i = 0; i < init_rounds; i++) {
    for (j = 0; j < INIT_SIZE_BLK; j++) {
      aesb_pseudo_round(&text[AES_BLOCK_SIZE * j],
      &text[AES_BLOCK_SIZE * j],
      aes_ctx->key->exp_data);
    }
    memcpy(&long_state[i * INIT_SIZE_BYTE], text, INIT_SIZE_BYTE);
  }

  for (i = 0; i < 16; i++) {
    a[i] = state.k[i] ^ state.k[32 + i];
    b[i] = state.k[16 + i] ^ state.k[48 + i];
  }

  for (i = 0; i < iterations; i++) {
    /* Dependency chain: address -> read value ------+
    * written value <-+ hard function (AES or MUL) <+
    * next address  <-+
    */
    /* Iteration 1 */
    j = e2i(a, aes_rounds);
    aesb_single_round(&long_state[j * AES_BLOCK_SIZE], c, a);
    VARIANT2_SHUFFLE_ADD(long_state, j * AES_BLOCK_SIZE, a, b);
    xor_blocks_dst(c, b, &long_state[j * AES_BLOCK_SIZE]);
    VARIANT1_1((uint8_t*)&long_state[j * AES_BLOCK_SIZE]);
    /* Iteration 2 */
    j = e2i(c, aes_rounds);

    uint64_t* dst = (uint64_t*)&long_state[j * AES_BLOCK_SIZE];

    uint64_t t[2];
    t[0] = dst[0];
    t[1] = dst[1];

    VARIANT2_INTEGER_MATH(t, c);

    uint64_t hi;
    uint64_t lo = mul128(((uint64_t*)c)[0], t[0], &hi);

    VARIANT2_2();
    VARIANT2_SHUFFLE_ADD(long_state, j * AES_BLOCK_SIZE, a, b);

    ((uint64_t*)a)[0] += hi;
    ((uint64_t*)a)[1] += lo;

    dst[0] = ((uint64_t*)a)[0];
    dst[1] = ((uint64_t*)a)[1];

    ((uint64_t*)a)[0] ^= t[0];
    ((uint64_t*)a)[1] ^= t[1];

    VARIANT1_2((uint8_t*)&long_state[j * AES_BLOCK_SIZE]);
    copy_block(b + AES_BLOCK_SIZE, b);
    copy_block(b, c);
  }

  memcpy(text, state.init, INIT_SIZE_BYTE);
  oaes_key_import_data(aes_ctx, &state.hs.b[32], AES_KEY_SIZE);
  for (i = 0; i < init_rounds; i++) {
    for (j = 0; j < INIT_SIZE_BLK; j++) {
      xor_blocks(&text[j * AES_BLOCK_SIZE], &long_state[i * INIT_SIZE_BYTE + j * AES_BLOCK_SIZE]);
      aesb_pseudo_round(&text[j * AES_BLOCK_SIZE], &text[j * AES_BLOCK_SIZE], aes_ctx->key->exp_data);
    }
  }
  memcpy(state.init, text, INIT_SIZE_BYTE);
  hash_permutation(&state.hs);
  /*memcpy(hash, &state, 32);*/
  extra_hashes[state.hs.b[0] & 3](&state, 200, output);
  oaes_free((OAES_CTX **) &aes_ctx);
  free(long_state);
}

void cn_fast_hash(const char* input, char* output, uint32_t len) {
    union hash_state state;
    hash_process(&state, (const uint8_t*) input, len);
    memcpy(output, &state, HASH_SIZE);
}
