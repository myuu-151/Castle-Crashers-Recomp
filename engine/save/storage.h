// The save storage (castle.exe's storage objects, *(0x651f7c)+0x14+i*4): a
// byte buffer with a cursor, read and written by the scripts through
// ReadStorage / WriteStorage. Local ports 0-3 all use the signed-in Steam
// user's storage (slot 8), so they share one buffer and one cursor.
// docs/engine/save.md.
#pragma once

#include <cstdint>
#include <vector>

namespace save {

class Storage {
public:
    static constexpr uint32_t kSize = 0x60c;

    // A fresh save, then the DLC unlocks for
    // the owned packs (bit 0: character 29, bit 1: 30).
    void reset(uint32_t dlc);
    void apply_dlc(uint32_t dlc);

    bool seek(uint32_t pos);    // only within the buffer
    bool read(uint8_t& value);  // at the cursor, which advances
    bool write(uint8_t value);

    const std::vector<uint8_t>& bytes() const { return data_; }
    // Loaded save bytes (a save file's), the cursor at 0.
    void assign(const std::vector<uint8_t>& bytes);

    // Fields as the natives use them: seek, then byte reads or writes that
    // move the cursor; big-endian, reads past the end give 0.
    uint32_t read_be(uint32_t pos, int bytes);
    void write_be(uint32_t pos, int bytes, uint32_t value);

    // the original's, after a save is loaded: levels over 98 and stat bytes
    // over 25 are put back in range.
    void sanitize();

    // TESTING: character `type` (1 the green knight, 2 red, 3 blue, 4 orange,
    // ... as the scripts' p_type) maxed: level 99, every stat 25, every level
    // unlocked, a stock of potions, bombs and sandwiches.
    void max_out(int type);

private:
    void put(uint32_t pos, uint8_t value);

    std::vector<uint8_t> data_;
    uint32_t cursor_ = 0;
};

// The checksum stored after the buffer in cc_save.dat.
uint32_t checksum(const uint8_t* data, uint32_t size);

}  // namespace save
