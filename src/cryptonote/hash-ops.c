#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "c_keccak.h"

void hash_permutation(union hash_state *state) {
  keccakf((uint64_t*)state, 24);
}

void hash_process(union hash_state *state, const uint8_t *buf, size_t count) {
  keccak1600(buf, count, (uint8_t*)state);
}
