#include "as/object.h"

#include <algorithm>

namespace as {

namespace {

auto lower(std::vector<std::pair<NameId, Cell>>& cells, NameId name) {
    return std::lower_bound(cells.begin(), cells.end(), name,
                            [](const std::pair<NameId, Cell>& e, NameId n) { return e.first < n; });
}

}  // namespace

Cell* Properties::find(NameId name) {
    auto it = lower(cells_, name);
    return it != cells_.end() && it->first == name ? &it->second : nullptr;
}

Cell& Properties::get_or_add(NameId name) {
    auto it = lower(cells_, name);
    if (it == cells_.end() || it->first != name) it = cells_.insert(it, {name, Cell{}});
    return it->second;
}

bool Properties::erase(NameId name) {
    auto it = lower(cells_, name);
    if (it == cells_.end() || it->first != name) return false;
    it->second.reset();
    cells_.erase(it);
    return true;
}

void Properties::clear() {
    // Take the cells out first: releasing can reset objects that point back.
    auto cells = std::move(cells_);
    cells_.clear();
    for (auto& [name, cell] : cells) cell.reset();
}

bool Object::get_member(Interpreter&, NameId name, Value& out) {
    if (Cell* c = props.find(name)) {
        out = c->load();
        return true;
    }
    return false;
}

void Object::set_member(Interpreter&, NameId name, const Value& v) { props.get_or_add(name).store(v); }

void Object::call_method(Interpreter&, NameId, Args, Value&, bool&) {}

void retain_pushed(const Value& v) {
    if ((v.type & kCounted) && v.obj && v.obj->refcount != 0) v.obj->refcount++;
}

void release(uint32_t type, Object* obj) {
    if (!(type & kCounted) || !obj || obj->refcount == 0) return;
    if (obj->refcount == 1) obj->on_last_release();
    if (obj->type() & kCounted) obj->refcount--;
}

}  // namespace as
