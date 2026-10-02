// castle.exe's input: InputManager's four devices (XInput pads 0-3, the
// keyboard folded into pad 0), each player port's FlashPad of 22 logical
// buttons, and what Key.isDown answers. See docs/engine/input.md.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace input {

// Pad bits (a device's +0x1c), as castle.exe numbers them.
enum PadBit : uint16_t {
    kDpadUp = 0x1, kDpadDown = 0x2, kDpadLeft = 0x4, kDpadRight = 0x8,
    kStart = 0x10, kBack = 0x20,
    kShoulderL = 0x100, kShoulderR = 0x200, kTriggerL = 0x400, kTriggerR = 0x800,
    kY = 0x1000, kA = 0x2000, kX = 0x4000, kB = 0x8000,
};

// Stick bits (a device's +0x1e): full deflection 1/2/4/8 (up/down/left/right),
// half deflection 0x10/0x20/0x40/0x80.
enum StickBit : uint8_t {
    kStickUp = 0x1, kStickDown = 0x2, kStickLeft = 0x4, kStickRight = 0x8,
};

// XINPUT_GAMEPAD as the frontend reports a controller.
struct PadReading {
    bool connected = false;
    uint16_t buttons = 0;  // XINPUT_GAMEPAD_* bits
    uint8_t left_trigger = 0, right_trigger = 0;
    int16_t thumb_lx = 0, thumb_ly = 0;  // y up positive
};

// InputPad (0x110 bytes): the state FlashPad reads.
struct Device {
    uint16_t buttons = 0;   // +0x1c
    uint8_t stick = 0;      // +0x1e
    uint8_t flags = 0;      // +0x1f (0x80: ignore input)
    uint16_t sampled = 0;   // +0x20
    uint8_t sampled_stick = 0;  // +0x22
    float axes[4] = {};     // +0xe8 x, +0xec y, +0xf0, +0xf4
    // +0x23: the flags queued with each sample; the low nibble is the port's
    // state (0 none, 1 playing, 2 paused, 3 exit to map), which each sample
    // taken publishes as g_nPort1State..g_nPort4State.
    uint8_t port_state = 0;

    void set_stick(uint8_t bits);
};

// PlatformInputKeyboard: named key maps; the selected one ORs the pad and
// stick bits of every held key.
class Keyboard {
public:
    Keyboard();  // "defaultmenu" and "defaultgame"
    void select(const std::string& name);  // SetKeyboardMapping
    // a map chosen over the movie's (the pause menu's
    // "defaultmenu"), or, with null, the movie's again.
    void override_map(const char* name);
    bool overridden() const { return overridden_; }
    void sample(Device& into) const;

    std::array<uint8_t, 256> keys{};  // Windows virtual-key state (app +0xe0)

private:
    struct Binding { uint16_t pad; uint8_t key; uint8_t stick; };
    std::map<std::string, std::vector<Binding>> maps_;
    const std::vector<Binding>* current_ = nullptr;
    std::string movie_map_ = "defaultmenu";  // +0x110: the one SetKeyboardMapping chose
    bool overridden_ = false;                // +0x194
};

// One FlashPad button (0x1c bytes at FlashPad+0x44).
struct Button {
    std::vector<uint32_t> bindings;  // pad bits, or 0x80SS0000 for stick bits SS
    uint32_t mode = 0;               // +0xc: 0 any binding, 1 all bindings
    uint16_t config = 0x0001;        // +0x10: 1 enabled, 2 edge, 4 repeat, 8 reads ignored devices
    uint16_t state = 0x0010;         // +0x12: 1 down, 2 disabled, 4 was pressed, 8 pressed, 0x10 latched, 0x20 repeating
    float repeat = 0;                // +0x14

    bool down() const { return (config & 1) && (state & 1); }
    bool pressed() const { return (config & 1) && (state & 8); }
};

// FlashPad: the logical buttons of one player port.
class FlashPad {
public:
    enum { kStartButton, kBackButton, kPadLeft, kPadRight, kPadUp, kPadDown, kUp, kDown, kLeft, kRight,
           kAccept, kCancel, kButtonY, kButtonX, kShoulderLButton, kShoulderRButton, kCount = 22 };
    explicit FlashPad(int port);
    void update(const Device& d, float dt);
    bool key_down(int code, const Device& d) const;  // Key.isDown for this port's codes

    // InputStick: a stick direction, past its threshold (the original's...).
    bool stick_dir(const Device& d, int stick, int axis, bool negative) const;

    Button buttons[kCount];
    std::map<int, int> codes;  // Key.isDown code -> button

private:
    struct Stick { int index; bool thresholded; float threshold_x, threshold_y; };
    Stick sticks_[2] = {{0, true, 0.5f, 0.5f}, {1, true, 0.5f, 0.5f}};
};

class Input {
public:
    Input();
    // One game tick: sample the devices if sampling is on
    // (GameManager +0x6f0; while off, devices keep their last state), then
    // update the ports' FlashPads.
    void tick(float dt, bool sample);
    // The window has the focus; without it samples are flagged "ignore"
    // (+0x1f bit 0x80) and the buttons read nothing.
    bool focused = true;
    bool key_down(int code) const;

    Keyboard keyboard;
    PadReading pads[4];
    Device devices[4];
    FlashPad ports[4] = {FlashPad(0), FlashPad(1), FlashPad(2), FlashPad(3)};
};

}  // namespace input
