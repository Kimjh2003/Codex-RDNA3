#pragma once

#include <cstddef>
#include <cstdint>

struct SveCapability {
    bool sve;
    bool sve2;
    uint32_t current_vl_bits;
    uint32_t maximum_vl_bits;
    uint32_t lanes_u64;
};

SveCapability query_sve_capability();

// Uses real SVE instructions and predicates. The loop is vector-length
// agnostic: a logical 32-lane batch may require multiple hardware vectors.
void sve_xor_shift_one(const uint64_t* input, uint64_t* output, size_t lanes);
