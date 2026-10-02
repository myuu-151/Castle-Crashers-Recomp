// castle.exe's built-in script objects other than movie clips: Function,
// Array, Sound, Color, Math and Key, and the fixed pools they come from.
// See docs/engine/actionscript-semantics.md and movieclips-and-frames.md.
#pragma once

#include <array>
#include <memory>
#include <vector>

#include "as/object.h"
#include "as/runner.h"

namespace as {

// A script function (DefineFunction). One set of parameter cells and one
// `var` map (props), shared by every activation, so recursion clobbers them.
class Function : public Object {
public:
    static constexpr int kMaxParams = 12;
    Function() : Object(kFunction) {}

    void on_last_release() override;
    bool get_member(Interpreter&, NameId, Value&) override { return false; }
    void set_member(Interpreter&, NameId, const Value&) override {}

    NameId name = 0;
    int param_count = 0;
    std::array<NameId, kMaxParams> param_names{};
    std::array<Cell, kMaxParams> params{};
    Code code;
    Object* defining_clip = nullptr;            // `this` while it runs
    std::shared_ptr<ConstantPool> pool;         // the pool in force when defined
};

// An array of fixed length: elements are stored raw, and assigning past the
// end is ignored.
class Array : public Object {
public:
    Array() : Object(kArray) {}

    void resize(int length);
    void on_last_release() override;
    bool get_member(Interpreter& in, NameId name, Value& out) override;
    void set_member(Interpreter&, NameId, const Value&) override {}
    void call_method(Interpreter& in, NameId name, Args args, Value& result, bool& abort) override;

    int length() const { return int(elements.size()); }
    // release element i, make it undefined.
    void clear_element(int i);
    // store v raw at i, growing the length to i + 1 if needed.
    void store_element(int i, const Value& v);

    std::vector<Value> elements;
};

// A Sound object (vtable 0x49b3c4). Playback is not implemented yet; the
// state the scripts see is.
class Sound : public Object {
public:
    Sound() : Object(kSound) {}

    void on_last_release() override;
    bool get_member(Interpreter&, NameId, Value&) override { return false; }
    void set_member(Interpreter&, NameId, const Value&) override {}
    void call_method(Interpreter& in, NameId name, Args args, Value& result, bool& abort) override;

    NameId sound = 0;
    bool attached = false;
    int voice = -1;
    float volume = 1.0f;
    float pan = 0.0f;
};

// A colour transform (CXformA, the script `Color`, also each clip's own).
// Multipliers default to 255, not 256.
class Color : public Object {
public:
    Color() : Object(kColor) {}

    void reset_values();
    void on_last_release() override;
    void call_method(Interpreter& in, NameId name, Args args, Value& result, bool& abort) override;
    // copy the values into the linked clip's own transform.
    void apply_to_clip();

    int16_t mul[4] = {255, 255, 255, 255};  // r, g, b, a (+0x20 +0x22 +0x24 +0x2c)
    int16_t add[4] = {0, 0, 0, 0};          // r, g, b, a (+0x26 +0x28 +0x2a +0x2e)
    Object* clip = nullptr;                 // +0x30
};

// The Math object (type 0x1000, not reference counted).
class MathObject : public Object {
public:
    MathObject() : Object(kMath) {}
    bool get_member(Interpreter&, NameId, Value&) override { return false; }
    void set_member(Interpreter&, NameId, const Value&) override {}
    void call_method(Interpreter& in, NameId name, Args args, Value& result, bool& abort) override;
};

// The Key object (type 0x2000). isDown asks the host for controller state.
class KeyObject : public Object {
public:
    KeyObject() : Object(kKey) {}
    bool get_member(Interpreter& in, NameId name, Value& out) override;
    void set_member(Interpreter&, NameId, const Value&) override {}
    void call_method(Interpreter& in, NameId name, Args args, Value& result, bool& abort) override;
};

// A fixed pool: hands out the first object at or after the cursor whose
// count is 0, leaving the cursor on it (so an object created but not yet
// stored can be handed out again). A slot's object is made the first time
// the slot is handed out; until then it counts as a free, clean one.
template <typename T, size_t N>
class Pool {
public:
    Pool() : objects_(N) {}
    T* allocate() {
        for (size_t n = 0; n < N; n++) {
            size_t i = (cursor_ + n) % N;
            if (!objects_[i]) {
                cursor_ = i;
                objects_[i] = std::make_unique<T>();
                return objects_[i].get();
            }
            if (objects_[i]->refcount == 0) {
                cursor_ = i;
                objects_[i]->on_last_release();  // start clean
                return objects_[i].get();
            }
        }
        return nullptr;
    }

private:
    std::vector<std::unique_ptr<T>> objects_;
    size_t cursor_ = 0;
};

}  // namespace as
