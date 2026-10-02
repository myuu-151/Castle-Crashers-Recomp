#include "save/storage.h"

namespace save {

namespace {

// Each character's default weapon, characters 1-30.
constexpr uint8_t kDefaultWeapons[30] = {
    0x03, 0x19, 0x27, 0x38, 0x02, 0x13, 0x0a, 0x12, 0x22, 0x1b, 0x2f, 0x24, 0x0c, 0x21, 0x0f,
    0x0f, 0x06, 0x2d, 0x1a, 0x39, 0x2b, 0x14, 0x02, 0x23, 0x1f, 0x30, 0x32, 0x44, 0x3f, 0x55,
};

// The default keyboard map, stored at 0x5e0.
constexpr uint8_t kDefaultKeys[18] = {
    0x25, 0x27, 0x26, 0x28, 0x44, 0x57, 0x41, 0x20, 0x10, 0x51, 0x45, 0x1b, 0x11, 0x41, 0x53, 0x44, 0x09, 0x48,
};

constexpr uint32_t kCharacters = 0x40;  // 30 records of 0x30 bytes
constexpr uint32_t kCharacterSize = 0x30;
constexpr uint32_t kKeys = 0x5e0;

}  // namespace

void Storage::assign(const std::vector<uint8_t>& bytes) {
    data_ = bytes;
    data_.resize(kSize, 0);
    cursor_ = 0;
}

uint32_t Storage::read_be(uint32_t pos, int bytes) {
    seek(pos);
    uint32_t v = 0;
    for (int i = 0; i < bytes; i++) {
        uint8_t b = 0;
        read(b);
        v = (v << 8) | b;
    }
    return v;
}

void Storage::write_be(uint32_t pos, int bytes, uint32_t value) {
    seek(pos);
    for (int i = bytes - 1; i >= 0; i--) write(uint8_t(value >> (i * 8)));
}

void Storage::sanitize() {
    for (uint32_t i = 0; i < 30; i++) {
        uint32_t r = kCharacters + i * kCharacterSize;
        if (read_be(r + 1, 1) > 0x62) {
            write_be(r + 1, 1, 0x62);
            write_be(r + 2, 4, read_be(r + 2, 4) + 1);
        }
        for (uint32_t k = 8; k < 12; k++)
            if (read_be(r + k, 1) > 0x19) write_be(r + k, 1, 1);
    }
}

void Storage::max_out(int type) {
    if (type < 1 || type > 30) return;
    // the record as the scripts read it (main, f_LoadCharacter): +0 flags,
    // +1 level - 1, +2 experience (big-endian), +6 weapon, +7 animal, +8
    // strength, defense, magic, agility, +12 three bytes of level unlocks,
    // +15 potions, +16 bombs, +17 sandwiches
    const uint32_t r = kCharacters + uint32_t(type - 1) * kCharacterSize;
    const uint32_t level = 99;
    const uint32_t exp = 190 * (level - 1) + 10 * (level - 1) * (level - 2);   // level 99's start
    write_be(r + 0, 1, read_be(r + 0, 1) | 0x80);
    write_be(r + 1, 1, level - 1);
    write_be(r + 2, 4, exp);
    for (uint32_t k = 8; k < 12; k++) write_be(r + k, 1, 25);
    for (uint32_t k = 12; k < 15; k++) write_be(r + k, 1, 0xff);
    write_be(r + 15, 1, 9);
    write_be(r + 16, 1, 9);
    write_be(r + 17, 1, 9);
}

void Storage::put(uint32_t pos, uint8_t value) {
    seek(pos);
    write(value);
}

void Storage::reset(uint32_t dlc) {
    data_.assign(kSize, 0);
    cursor_ = 0;
    // Settings: 1, then the master, music and sound volumes, then 1.
    const uint8_t settings[5] = {1, 8, 9, 9, 1};
    for (uint32_t i = 0; i < 5; i++) put(i, settings[i]);
    // Character records: +0 flags (0x80: unlocked), +6 the default weapon,
    // +8..+11 set to 1; the four starting knights are unlocked.
    for (uint32_t i = 0; i < 30; i++) {
        uint32_t record = kCharacters + i * kCharacterSize;
        put(record + 6, kDefaultWeapons[i]);
        for (uint32_t k = 8; k < 12; k++) put(record + k, 1);
    }
    for (uint32_t i = 0; i < 4; i++) put(kCharacters + i * kCharacterSize, 0x80);
    for (uint32_t i = 0; i < sizeof(kDefaultKeys); i++) put(kKeys + i, kDefaultKeys[i]);
    apply_dlc(dlc);
}

void Storage::apply_dlc(uint32_t dlc) {
    // flag bits in the header and the
    // character's unlock byte, set or cleared; the last write leaves the
    // cursor after character 30's record flags.
    auto flag = [&](uint32_t pos, bool owned, uint8_t set, uint8_t keep) {
        uint8_t v = 0;
        seek(pos);
        read(v);
        put(pos, owned ? uint8_t(v | set) : uint8_t(v & keep));
    };
    bool pink = dlc & 1, blacksmith = dlc & 2;
    flag(0x1a, pink, 0x02, 0xfd);
    flag(0x1b, pink, 0xf2, 0x0d);
    put(kCharacters + 28 * kCharacterSize, pink ? 0x80 : 0);
    flag(0x1b, blacksmith, 0x04, 0xfb);
    flag(0x1c, blacksmith, 0xaf, 0x50);
    put(kCharacters + 29 * kCharacterSize, blacksmith ? 0x80 : 0);
}

bool Storage::seek(uint32_t pos) {
    if (pos >= data_.size()) return false;
    cursor_ = pos;
    return true;
}

bool Storage::read(uint8_t& value) {
    if (cursor_ >= data_.size()) {
        value = 0;
        return false;
    }
    value = data_[cursor_++];
    return true;
}

bool Storage::write(uint8_t value) {
    if (cursor_ >= data_.size()) return false;
    data_[cursor_++] = value;
    return true;
}

uint32_t checksum(const uint8_t* data, uint32_t size) {
    uint32_t key = 0xd971, even = 0, odd = 0, last = 0;
    uint32_t i = 0;
    for (; size > 1 && i + 1 < size; i += 2) {
        uint8_t a = uint8_t((key >> 8) ^ data[i]);
        uint16_t k = uint16_t((uint16_t(a + key) * 0xce6du) + 0x58bf);
        uint8_t b = uint8_t((k >> 8) ^ data[i + 1]);
        even += a;
        odd += b;
        key = uint16_t((uint16_t(b + k) * 0xce6du) + 0x58bf);
    }
    if (i < size) last = (key >> 8) ^ data[i];
    return size ? last + even + odd : 0;
}

}  // namespace save
