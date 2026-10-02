#include "input/input.h"

namespace input {

namespace {

constexpr float kStickFull = 0.85f;

// XINPUT_GAMEPAD bits.
enum : uint16_t {
    kXDpadUp = 0x1, kXDpadDown = 0x2, kXDpadLeft = 0x4, kXDpadRight = 0x8, kXStart = 0x10, kXBack = 0x20,
    kXShoulderL = 0x100, kXShoulderR = 0x200, kXA = 0x1000, kXB = 0x2000, kXX = 0x4000, kXY = 0x8000,
};

// Key.isDown codes read from a port's buttons, per port.
constexpr int kCodes[4][14][2] = {
    {{0x31, 0}, {0x35, 1}, {0x02, 10}, {0x03, 11}, {0x0e, 12}, {0x0f, 13}, {0x41, 19},
     {0x44, 20}, {0x53, 18}, {0x57, 21}, {0x45, 17}, {0x42, 15}, {0x51, 16}, {0x43, 14}},
    {{0x32, 0}, {0x36, 1}, {0x04, 10}, {0x05, 11}, {0x13, 12}, {0x15, 13}, {0x10, 19},
     {0x12, 20}, {0x11, 18}, {0x14, 21}, {0x21, 17}, {0x22, 15}, {0x1b, 16}, {0x20, 14}},
    {{0x33, 0}, {0x37, 1}, {0x06, 10}, {0x07, 11}, {0x16, 12}, {0x17, 13}, {0x61, 19},
     {0x63, 20}, {0x62, 18}, {0x64, 21}, {0x67, 17}, {0x68, 15}, {0x65, 16}, {0x66, 14}},
    {{0x34, 0}, {0x38, 1}, {0x0a, 10}, {0x0b, 11}, {0x18, 12}, {0x01, 13}, {0x6d, 19},
     {0x6f, 20}, {0x6e, 18}, {0x70, 21}, {0x73, 17}, {0x74, 15}, {0x71, 16}, {0x72, 14}},
};

// Key.isDown codes read from a port's sticks: left, up, right,
// down of the first stick, then of the second.
constexpr int kStickCodes[4][8] = {
    {0x25, 0x26, 0x27, 0x28, 0x75, 0x76, 0x77, 0x78},
    {0x08, 0x09, 0x0c, 0x0d, 0x79, 0x7a, 0x7b, 0x7c},
    {0x58, 0x59, 0x5a, 0x60, 0x7d, 0x7e, 0x7f, 0x80},
    {0x69, 0x6a, 0x6b, 0x6c, 0x81, 0x82, 0x83, 0x84},
};

// XINPUT_GAMEPAD -> pad and stick bits.
void convert(const PadReading& p, Device& d) {
    uint16_t b = p.buttons & (kXDpadUp | kXDpadDown | kXDpadLeft | kXDpadRight | kXStart | kXBack |
                               kXShoulderL | kXShoulderR);
    if (p.buttons & kXA) b |= kA;
    if (p.buttons & kXB) b |= kB;
    if (p.buttons & kXX) b |= kX;
    if (p.buttons & kXY) b |= kY;
    if (p.left_trigger > 0x40) b |= kTriggerL;
    if (p.right_trigger > 0x40) b |= kTriggerR;
    constexpr float kScale = 3.0517578125e-05f;  // 1/32768
    uint8_t s = 0;
    float x = float(p.thumb_lx) * kScale;
    if (x < -0.75f) s = 0x04;
    else if (x < -0.25f) s = 0x40;
    if (x > 0.75f) s |= 0x08;
    else if (x > 0.25f) s |= 0x80;
    float y = float(p.thumb_ly) * kScale;
    if (y > 0.75f) s |= 0x01;
    else if (y > 0.25f) s |= 0x10;
    if (y < -0.75f) s |= 0x02;
    else if (y < -0.25f) s |= 0x20;
    d.sampled = b;
    d.sampled_stick = s;
}

}  // namespace

void Device::set_stick(uint8_t bits) {
    stick = bits;
    axes[0] = bits & 4 ? -kStickFull : bits & 8 ? kStickFull : 0.0f;
    axes[1] = bits & 1 ? kStickFull : bits & 2 ? -kStickFull : 0.0f;
    axes[2] = bits & 0x40 ? -kStickFull : bits & 0x80 ? kStickFull : 0.0f;
    axes[3] = bits & 0x10 ? kStickFull : bits & 0x20 ? -kStickFull : 0.0f;
}

Keyboard::Keyboard() {
    // Each binding is (stick << 24) | (virtual key << 16) | pad bits.
    auto add = [&](const char* name, std::initializer_list<uint32_t> list) {
        auto& v = maps_[name];
        for (uint32_t b : list) v.push_back({uint16_t(b), uint8_t(b >> 16), uint8_t(b >> 24)});
    };
    add("defaultmenu", {0x4250000, 0x8270000, 0x2280000, 0x1260000, 0xc00020, 0x1b8000, 0x428000, 0xd2000,
                        0x412000, 0x584000, 0x591000, 0x220800, 0x210400});
    add("defaultgame", {0x1b0010, 0x4250000, 0x8270000, 0x2280000, 0x1260000, 0x444000, 0x202000, 0x418000,
                        0x571000, 0x100800, 0x110400, 0x510100, 0x450200});
    select("defaultmenu");
    // the original's, when the GameManager starts.
    add("quaff", {0x414000, 0x531000, 0x1b0010, 0x442000});
    add("quaffStart", {0xd0010, 0x1b8000, 0x428000});
    add("map", {0x4250000, 0x8270000, 0x2280000, 0x1260000, 0x1b0010, 0xd2000, 0x484000, 0x584000, 0x91000,
                0x591000});
    add("attract", {0xd0010});
}

void Keyboard::select(const std::string& name) {
    // held keys are forgotten; an unknown name selects no map.
    // While a map is chosen over the movie's, the movie's choice is only
    // remembered.
    movie_map_ = name;
    if (overridden_) return;
    keys.fill(0);
    auto it = maps_.find(name);
    current_ = it == maps_.end() ? nullptr : &it->second;
}

void Keyboard::override_map(const char* name) {
    keys.fill(0);
    overridden_ = name != nullptr;
    auto it = maps_.find(name ? std::string(name) : movie_map_);
    current_ = it == maps_.end() ? nullptr : &it->second;
}

void Keyboard::sample(Device& into) const {
    uint16_t pad = 0;
    uint8_t stick = 0;
    if (current_) {
        for (const Binding& b : *current_) {
            if (keys[b.key]) {
                pad |= b.pad;
                stick |= b.stick;
            }
        }
    }
    into.sampled |= pad;
    into.sampled_stick |= stick;
}

FlashPad::FlashPad(int port) {
    // the original's.
    struct Setup { std::initializer_list<uint32_t> bindings; uint16_t set; };
    const Setup setup[kCount] = {
        {{kStart}, 2},
        {{kBack}, 2 | 8},
        {{kDpadLeft}, 0},
        {{kDpadRight}, 0},
        {{kDpadUp}, 0},
        {{kDpadDown}, 0},
        {{kDpadUp, 0x80010000}, 2},
        {{kDpadDown, 0x80020000}, 2},
        {{kDpadLeft, 0x80040000}, 2},
        {{kDpadRight, 0x80080000}, 2},
        {{kA}, 2},
        {{kB}, 2},
        {{kY}, 2},
        {{kX}, 2},
        {{kShoulderL}, 0},
        {{kShoulderR}, 0},
        {{kTriggerL}, 0},
        {{kTriggerR}, 0},
        {{kA}, 0},
        {{kX}, 0},
        {{kB}, 0},
        {{kY}, 0},
    };
    for (int i = 0; i < kCount; i++) {
        buttons[i].bindings.assign(setup[i].bindings.begin(), setup[i].bindings.end());
        buttons[i].config = uint16_t((buttons[i].config & ~6) | setup[i].set);
    }
    for (const auto& c : kCodes[port]) codes[c[0]] = c[1];
}

void FlashPad::update(const Device& d, float dt) {
    for (Button& b : buttons) {
        if (b.state & 8) b.state |= 4;
        else b.state &= ~4;
        // is the button down?
        b.state &= ~(1 | 8);
        if (!(d.flags & 0x80) || (b.config & 8)) {
            int count = 0, held = 0;
            for (uint32_t v : b.bindings) {
                bool on = false;
                if (!(b.state & 2)) {
                    if ((v & 0x80000000) && (d.stick & uint8_t(v >> 16))) on = true;
                    if (v & d.buttons) on = true;
                }
                count++;
                if (on) held++;
            }
            if (b.mode == 0) {
                if (held) b.state |= 1;
            } else if (b.mode == 1) {
                if (held == count) b.state |= 1;
            }
        }
        if (b.state & 0x10) {
            if (!(b.state & 1)) b.state &= ~0x10;
        } else if (b.config & (2 | 4)) {
            if (b.state & 1) b.state |= 0x18;
        } else if (b.state & 1) {
            b.state |= 8;
        }
        if (b.config & 4) {
            if (b.state & 0x20) {
                if (b.state & 1) {
                    b.repeat -= dt;
                    if (b.repeat <= 0.0f) {
                        b.state |= 8;
                        b.repeat = 0.3f;
                    }
                } else {
                    b.state &= ~0x20;
                    b.repeat = 0.0f;
                }
            } else if ((b.state & 0x10) && (b.state & 8)) {
                b.state |= 0x20;
                b.repeat = 0.5f;
            }
        }
    }
}

bool FlashPad::stick_dir(const Device& d, int stick, int axis, bool negative) const {
    const Stick& s = sticks_[stick];
    float v = s.index < 2 ? d.axes[s.index * 2 + axis] : 0.0f;
    float threshold = axis == 0 ? s.threshold_x : s.threshold_y;
    if (d.flags & 0x80) return false;
    if (s.thresholded && !(threshold < v) && !(v < -threshold)) return false;
    return negative ? v < 0.0f : 0.0f < v;
}

bool FlashPad::key_down(int code, const Device& d) const {
    auto it = codes.find(code);
    if (it != codes.end()) return buttons[it->second].down();
    return false;
}

Input::Input() {}

void Input::tick(float dt, bool sample) {
    // every device samples (pad 0 also takes the keyboard),
    // queues and, offline, immediately takes its sample.
    for (int i = 0; sample && i < 4; i++) {
        Device& d = devices[i];
        d.sampled = 0;
        d.sampled_stick = 0;
        if (pads[i].connected) convert(pads[i], d);
        if (i == 0) keyboard.sample(d);
        d.flags = uint8_t(focused ? d.flags & ~0x80 : d.flags | 0x80);
        d.buttons = d.sampled;
        d.set_stick(d.sampled_stick);
    }

    for (int i = 0; i < 4; i++) ports[i].update(devices[i], dt);
}

bool Input::key_down(int code) const {
    for (int port = 0; port < 4; port++) {
        const FlashPad& p = ports[port];
        const Device& d = devices[port];
        for (int k = 0; k < 8; k++) {
            if (kStickCodes[port][k] != code) continue;
            int stick = k / 4, dir = k % 4;  // left, up, right, down
            int axis = dir == 1 || dir == 3 ? 1 : 0;
            bool negative = dir == 0 || dir == 3;
            if (p.stick_dir(d, stick, axis, negative)) return true;
            if (stick == 0) {
                static constexpr int kPad[4] = {FlashPad::kPadLeft, FlashPad::kPadUp, FlashPad::kPadRight,
                                                FlashPad::kPadDown};
                return p.buttons[kPad[dir]].down();
            }
            return false;
        }
        if (p.codes.count(code)) return p.key_down(code, d);
    }
    return false;
}

}  // namespace input
