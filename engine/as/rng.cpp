#include "as/rng.h"

namespace as {

void Rng::reseed(uint32_t seed) {
    mt_[0] = seed;
    for (mti_ = 1; mti_ < 624; mti_++) mt_[mti_] = mt_[mti_ - 1] * 69069u;
    draws = 0;
}

uint32_t Rng::next() {
    constexpr uint32_t kMatrix = 0x9908b0dfu;
    uint32_t y;
    if (mti_ >= 624) {
        if (mti_ == 625) reseed(4357);
        int k;
        for (k = 0; k < 624 - 397; k++) {
            y = (mt_[k] & 0x80000000u) | (mt_[k + 1] & 0x7fffffffu);
            mt_[k] = mt_[k + 397] ^ (y >> 1) ^ ((y & 1) ? kMatrix : 0);
        }
        for (; k < 623; k++) {
            y = (mt_[k] & 0x80000000u) | (mt_[k + 1] & 0x7fffffffu);
            mt_[k] = mt_[k + 397 - 624] ^ (y >> 1) ^ ((y & 1) ? kMatrix : 0);
        }
        y = (mt_[623] & 0x80000000u) | (mt_[0] & 0x7fffffffu);
        mt_[623] = mt_[396] ^ (y >> 1) ^ ((y & 1) ? kMatrix : 0);
        mti_ = 0;
    }
    y = mt_[mti_++];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9d2c5680u;
    y ^= (y << 15) & 0xefc60000u;
    y ^= y >> 18;
    return y;
}

Rng& rng() {
    static Rng generator = [] {
        Rng r;
        r.reseed();
        return r;
    }();
    return generator;
}

}  // namespace as
