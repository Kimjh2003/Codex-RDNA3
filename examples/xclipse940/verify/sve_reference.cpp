#include "sve_reference.hpp"

#include <arm_sve.h>
#include <asm/hwcap.h>
#include <asm/sigcontext.h>
#include <linux/prctl.h>
#include <sys/auxv.h>
#include <sys/prctl.h>

#include <stdexcept>

SveCapability query_sve_capability() {
    const bool sve = (getauxval(AT_HWCAP) & HWCAP_SVE) != 0;
    const bool sve2 = (getauxval(AT_HWCAP2) & HWCAP2_SVE2) != 0;
    if (!sve) return {false, sve2, 0, 0, 0};
    const int current = prctl(PR_SVE_GET_VL, 0, 0, 0, 0);
    if (current < 0) throw std::runtime_error("PR_SVE_GET_VL failed");
    const int maximum = prctl(PR_SVE_SET_VL, SVE_VL_MAX, 0, 0, 0);
    if (maximum < 0) throw std::runtime_error("PR_SVE_SET_VL(max) failed");
    const uint32_t current_bits = uint32_t(current & PR_SVE_VL_LEN_MASK) * 8;
    const uint32_t maximum_bits = uint32_t(maximum & PR_SVE_VL_LEN_MASK) * 8;
    return {sve, sve2, current_bits, maximum_bits, uint32_t(svcntd())};
}

void sve_xor_shift_one(const uint64_t* input, uint64_t* output, size_t lanes) {
    const uint64_t hardware_lanes = svcntd();
    for (uint64_t offset = 0; offset < lanes; offset += hardware_lanes) {
        const svbool_t active = svwhilelt_b64(offset, uint64_t(lanes));
        const svuint64_t value = svld1_u64(active, input + offset);
        const svuint64_t shifted = svlsl_n_u64_x(active, value, 1);
        const svuint64_t transformed = sveor_u64_x(active, value, shifted);
        svst1_u64(active, output + offset, transformed);
    }
}
