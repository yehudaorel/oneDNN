/*******************************************************************************
* Copyright 2020-2024 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/


#pragma once

static const char *philox_rng_source = R"CLC(
// Clear bits 30 & 14: 0xBFFFFFFF & 0xFFFFBFFF => 0xBFFFBFFF:
#define INF_NAN_MASK 0xBFFFBFFF

#define WRITE(dst, offset, r0, r1, r2, r3) \
    do { \
        ((__global uint *)((dst) + (offset)))[0] = (r0); \
        ((__global uint *)((dst) + (offset)))[1] = (r1); \
        ((__global uint *)((dst) + (offset)))[2] = (r2); \
        ((__global uint *)((dst) + (offset)))[3] = (r3); \
    } while (0)

#define PARTIAL_WRITE(dst, offset, lbytes, r0, r1, r2, r3) \
    do { \
        __global uchar *data = (__global uchar *)(dst); \
        uint vals[4] = {(r0), (r1), (r2), (r3)}; \
        for (size_t i = 0; i < (lbytes); i++) { \
            size_t w_idx = i >> 2; \
            size_t b_idx = i & 3; \
            uchar b_val = (uchar)((vals[w_idx] >> (8 * b_idx)) & 0xFF); \
            data[offset + i] = b_val; \
        } \
    } while (0)

inline uint philox_4x32(long idx, uint seed) {
#define PHILOX_4UINT_ROUND(mul, ctr, key) \
    as_uint4(convert_ulong2(ctr.s31) * mul) ^ (uint4)(ctr.s20 ^ key, 0, 0).s3120

    uint4 ctr = 0;
    const ulong2 ctr_mul = (ulong2)(0xD2511F53uL, 0xCD9E8D57uL);
    const ulong key_add = as_ulong((uint2)(0x9E3779B9u, 0xBB67AE85u));
    const uint16 key0 = (uint16)(seed)
            + as_uint16((ulong8)(key_add))
                    * (uint16)(0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7);
    const uint4 key1
            = (uint4)(seed) + as_uint4((ulong2)(key_add)) * (uint4)(8, 8, 9, 9);

    ctr = (uint4)(idx & ~3L) + (uint4)(3, 2, 1, 0);
    ctr = PHILOX_4UINT_ROUND(ctr_mul, ctr, key0.s01);
    ctr = PHILOX_4UINT_ROUND(ctr_mul, ctr, key0.s23);
    ctr = PHILOX_4UINT_ROUND(ctr_mul, ctr, key0.s45);
    ctr = PHILOX_4UINT_ROUND(ctr_mul, ctr, key0.s67);
    ctr = PHILOX_4UINT_ROUND(ctr_mul, ctr, key0.s89);
    ctr = PHILOX_4UINT_ROUND(ctr_mul, ctr, key0.sAB);
    ctr = PHILOX_4UINT_ROUND(ctr_mul, ctr, key0.sCD);
    ctr = PHILOX_4UINT_ROUND(ctr_mul, ctr, key0.sEF);
    ctr = PHILOX_4UINT_ROUND(ctr_mul, ctr, key1.s01);
    ctr = PHILOX_4UINT_ROUND(ctr_mul, ctr, key1.s23);

    return ctr[~idx & 3L];
}

__kernel void philox_fill_kernel(__global uint *data, long nbytes, uint seed) {
    size_t gid = get_global_id(0);
    size_t offset = gid * 16;

    if (offset >= nbytes) return;
    size_t leftover = nbytes - offset;
    if (leftover > 16) leftover = 16;

    // Generate 4x32-bit randoms
    uint r0 = philox_4x32((long)(4*gid + 0), seed) & INF_NAN_MASK;
    uint r1 = philox_4x32((long)(4*gid + 1), seed) & INF_NAN_MASK;
    uint r2 = philox_4x32((long)(4*gid + 2), seed) & INF_NAN_MASK;
    uint r3 = philox_4x32((long)(4*gid + 3), seed) & INF_NAN_MASK;
    if (leftover == 16) {
        WRITE(data, offset, r0, r1, r2, r3);
    } else {
        PARTIAL_WRITE(data, offset, leftover, r0, r1, r2, r3);
    }
}
)CLC";