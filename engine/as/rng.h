// castle.exe's random number generator: MT19937 with the original 1998
// seeding (sgenrand), seeded with 1234567 at start-up and again whenever a
// movie is loaded. Only scripts draw from it (RandomNumber and Math.random).
#pragma once

#include <cstdint>

namespace as {

class Rng {
public:
    static constexpr uint32_t kSeed = 1234567;  // 0x12d687

    void reseed(uint32_t seed = kSeed);  // 0x42c700
    uint32_t next();                     // 0x42c740
    uint32_t draws = 0;                  // 0x651f50, reset with reseed

private:
    uint32_t mt_[624]{};
    int mti_ = 625;  // unseeded: next() seeds with 4357
};

Rng& rng();

}  // namespace as
