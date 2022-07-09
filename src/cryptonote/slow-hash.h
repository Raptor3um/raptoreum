#pragma once

#include <stddef.h>
#include <stdint.h>
#include <cryptonote/hash-ops.h>

#define CN_PAGE_SIZE          2097152
#define CN_ITERATIONS          524288
#define CN_AES_ROUNDS          131072

#define CN_DARK_PAGE_SIZE      524288
#define CN_DARK_ITERATIONS     131072
#define CN_DARK_AES_ROUNDS      32768

#define CN_DARK_LITE_AES_ROUNDS 16384

#define CN_FAST_PAGE_SIZE     2097152
#define CN_FAST_ITERATIONS     262144
#define CN_FAST_AES_ROUNDS     131072

#define CN_LITE_PAGE_SIZE     1048576
#define CN_LITE_ITERATIONS     262144
#define CN_LITE_AES_ROUNDS      65536

#define CN_TURTLE_PAGE_SIZE    262144
#define CN_TURTLE_ITERATIONS    65536
#define CN_TURTLE_AES_ROUNDS    16384

#define CN_TURTLE_LITE_AES_ROUNDS 8192

typedef unsigned char BitSequence;
typedef unsigned long long DataLength;

#ifdef __cplusplus

#include <string>

typedef std::string blobdata;

namespace crypto {

extern "C" {

#pragma pack(push, 1)
  class hash {
    char data[HASH_SIZE];
  };
#pragma pack(pop)

  void cn_slow_hash(const char* input, char* output, uint32_t len, int variant, uint32_t page_size, uint32_t iterations, size_t aes_rounds);
  void cn_fast_hash(const char* input, char* output, uint32_t len);

//-----------------------------------------------------------------------------------
  inline void cryptonight_dark_fast_hash(const char* input, char* output, uint32_t len) {
    cn_fast_hash(input, output, len);
  }

  inline void cryptonight_darklite_fast_hash(const char* input, char* output, uint32_t len) {
    cn_fast_hash(input, output, len);
  }

  inline void cryptonight_cnfast_fast_hash(const char* input, char* output, uint32_t len) {
    cn_fast_hash(input, output, len);
  }

  inline void cryptonight_cnlite_fast_hash(const char* input, char* output, uint32_t len) {
    cn_fast_hash(input, output, len);
  }

  inline void cryptonight_turtle_fast_hash(const char* input, char* output, uint32_t len) {
    cn_fast_hash(input, output, len);
  }

  inline void cryptonight_turtlelite_fast_hash(const char* input, char* output, uint32_t len) {
    cn_fast_hash(input, output, len);
  }

//-----------------------------------------------------------------------------------
  inline void cryptonight_dark_hash(const char* input, char* output, uint32_t len, int variant) {
    cn_slow_hash(input, output, len, 1, CN_DARK_PAGE_SIZE, CN_DARK_ITERATIONS, CN_DARK_AES_ROUNDS);
  }

  inline void cryptonight_darklite_hash(const char* input, char* output, uint32_t len, int variant) {
    cn_slow_hash(input, output, len, 1, CN_DARK_PAGE_SIZE, CN_DARK_ITERATIONS, CN_DARK_LITE_AES_ROUNDS);
  }

  inline void cryptonight_cnfast_hash(const char* input, char* output, uint32_t len, int variant) {
    cn_slow_hash(input, output, len, 1, CN_FAST_PAGE_SIZE, CN_FAST_ITERATIONS, CN_FAST_AES_ROUNDS);
  }

  inline void cryptonight_cnlite_hash(const char* input, char* output, uint32_t len, int variant) {
    cn_slow_hash(input, output, len, 1, CN_LITE_PAGE_SIZE, CN_LITE_ITERATIONS, CN_LITE_AES_ROUNDS);
  }

  inline void cryptonight_turtle_hash(const char* input, char* output, uint32_t len, int variant) {
    cn_slow_hash(input, output, len, 1, CN_TURTLE_PAGE_SIZE, CN_TURTLE_ITERATIONS, CN_TURTLE_AES_ROUNDS);
  }

  inline void cryptonight_turtlelite_hash(const char* input, char* output, uint32_t len, int variant) {
    cn_slow_hash(input, output, len, 1, CN_TURTLE_PAGE_SIZE, CN_TURTLE_ITERATIONS, CN_TURTLE_LITE_AES_ROUNDS);
  }

} // extern
} // namespace

#endif