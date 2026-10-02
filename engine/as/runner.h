// The script host every movie clip owns (castle.exe clip+0xa4, the `runner`
// each action handler receives): its constant pools, clip-event code, frame
// scripts and the functions its scripts defined. See
// docs/engine/actionscript-semantics.md 3.1.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "as/names.h"

namespace as {

class Object;
class Function;

// Bytecode inside a movie's data.
struct Code {
    const uint8_t* data = nullptr;
    size_t size = 0;  // bytes available (the loops stop at opcode 0 anyway)
};

struct ConstantPool {
    std::vector<NameId> ids;
};

// A map made on its first insert. Every pooled clip has a runner and most
// never run a script, so its maps cost only a pointer until they're used.
// Only what the runner needs: find, count, operator[], clear.
template <typename K, typename V>
class LazyMap {
public:
    using Map = std::map<K, V>;
    using iterator = typename Map::iterator;

    iterator find(const K& key) { return map_ ? map_->find(key) : none().end(); }
    iterator end() { return map_ ? map_->end() : none().end(); }
    size_t count(const K& key) const { return map_ ? map_->count(key) : 0; }
    V& operator[](const K& key) {
        if (!map_) map_ = std::make_unique<Map>();
        return (*map_)[key];
    }
    void clear() { map_.reset(); }
    template <typename Fn>
    void for_each(Fn fn) {
        if (map_)
            for (auto& [k, v] : *map_) fn(k, v);
    }

private:
    static Map& none() {
        static Map empty;
        return empty;
    }
    std::unique_ptr<Map> map_;
};

struct Runner {
    Object* owner = nullptr;  // the clip

    std::shared_ptr<ConstantPool> pool;  // [1], the pool in force
    std::shared_ptr<ConstantPool> load_pool, enter_pool;  // [2], [3]
    Code load_code, enter_code;                           // [4], [5]
    LazyMap<int, std::shared_ptr<ConstantPool>> frame_pools;  // +0x18
    LazyMap<NameId, Function*> functions;                    // +0x2c
    LazyMap<int, std::vector<Code>> frame_actions;           // +0x40

    // A pool built by ConstantPool during the current run (to be cached).
    std::shared_ptr<ConstantPool> built;

    // The functions its scripts defined, let go (each is counted: one kept
    // holds its slot in the interpreter's pool of 1000). Clearing the map
    // alone kept them all: a slot a function per level, until the pool was
    // full and every function defined after that was skipped (a level whose
    // own functions weren't there: no players, no portals).
    void release_functions();
};

}  // namespace as
