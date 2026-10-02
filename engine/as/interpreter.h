// The bytecode interpreter: one per movie, like castle.exe's interpreter
// context (the Player, ctx = *(*(*runner+0x4c)+0xa0)). It keeps the engine's
// memory model so its quirks come out the same: a pool of 1256 value slots
// that popped values are freed back to (payload cleared, type kept), a value
// stack of slot pointers (PushDuplicate aliases a slot), the target stack,
// the function frame stack and a single integer register.
// See docs/engine/actionscript-semantics.md.
#pragma once

#include <cstdio>

#include <array>
#include <cstdint>

#include "as/objects.h"
#include "as/runner.h"
#include "as/value.h"

namespace player { class MovieClip; }

namespace as {

// CASTLE_TRACE=FILE: a log of the scripts' calls, for debugging; or null.
std::FILE* call_trace();
// With CASTLE_TRACE_VARS=1 as well: variable lookups and keys; or null.
std::FILE* var_trace();
// CASTLE_TRACE_SET=NAME and CASTLE_TRACE_SET_FILE=FILE: every script
// assignment to a member called NAME, with the value's bits (null otherwise).
std::FILE* set_trace();
NameId set_trace_name();

// What scripts need from the rest of the game.
class Host {
public:
    virtual ~Host() = default;
    virtual bool key_down(int code) = 0;
    virtual void stop_all_sounds() {}
    // GetURL2: loadMovie into `target` / unloadMovie of `target`.
    virtual void load_movie(player::MovieClip* target, NameId name) = 0;
    virtual void unload_movie(player::MovieClip* target) = 0;
};

class Interpreter {
public:
    static constexpr int kSlots = 1256;

    struct Slot : Value {
        bool in_use = false;
    };

    explicit Interpreter(Host& host);

    // The loops (docs/engine/actionscript-semantics.md 3.3). Each expects the
    // clip on the target stack already (the frame driver pushes it).
    void run_frame_actions(Runner& r, int frame);
    void run_clip_event(Runner& r, bool load);
    // run the user function `id` for `r`'s clip.
    void call_function(Runner& r, NameId id, Args args, Value& result, bool& abort);

    // The target stack ("the clip the code runs on").
    void push_target(Object* o) { targets_[++target_top_] = o; }
    void pop_target() { targets_[target_top_--] = nullptr; }
    Object* target() const { return target_top_ >= 0 ? targets_[target_top_] : nullptr; }

    // GetVariable's lookup with the current target.
    void lookup(NameId id, Value& out);

    Host& host() { return host_; }
    Pool<Function, 1000> functions;
    Pool<Array, 500> arrays;
    Pool<Object, 200> objects;
    Pool<Sound, 400> sounds;
    Pool<Color, 2200> colors;
    MathObject math_object;
    KeyObject key_object;
    player::MovieClip* root = nullptr;  // the movie's (or host movie's) root Scene

    float time = 0;  // seconds of play (ctx+0x2868), for GetTime

private:
    Slot* allocate();
    void free(Slot* s) { s->clear_payload(); s->in_use = false; }
    void push(Slot* s);
    Slot* pop();
    Slot* peek() { return sp_ > 0 ? stack_[sp_ - 1] : nullptr; }
    void reset_stack();  // the original's, after an abort

    // Runs actions from `code` until opcode 0, the end, or Return in a
    // function. Returns false if aborted.
    bool run(Runner& r, const Code& code, Value* result, bool function, bool& abort);
    // One action; returns the extra bytes to skip (or a branch offset).
    int execute(Runner& r, uint8_t op, const uint8_t* body, uint16_t length, Value* result, bool& abort);

    Function* current_function() const { return frame_top_ >= 0 ? frames_[frame_top_] : nullptr; }
    const ConstantPool* pool_in_force(const Runner& r) const;
    // Push-body decoding of one item at body[pos]; writes into
    // `out` (untouched for the types it doesn't handle).
    void decode_push_item(const Runner& r, const uint8_t* body, size_t length, size_t& pos, Value& out);
    // a name from a fused action's body (string or constant).
    NameId decode_name(const Runner& r, const uint8_t* body, size_t length, size_t& pos);

    void set_variable(NameId id, const Value& v);
    void define_local(NameId id, const Value& v);
    player::MovieClip* find_clip(NameId id);
    void get_member(Value& obj, const Value& key, bool text_special);
    void set_member(const Value& obj, const Value& key, const Value& v);
    void add2(Slot* a, Slot* b);

    Host& host_;
    std::array<Slot, kSlots> pool_{};
    int cursor_ = 0;
    std::array<Slot*, kSlots> stack_{};
    int sp_ = 0;
    std::array<Object*, 64> targets_{};
    int target_top_ = -1;
    std::array<Function*, 32> frames_{};
    int frame_top_ = 0;  // frames_[0] = null: "not in a function"
    int32_t register_ = 0;
};

}  // namespace as
