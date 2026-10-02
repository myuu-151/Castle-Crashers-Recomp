#include "player/dump.h"

#include <cstdlib>
#include <set>
#include <string>

#include "as/value.h"

namespace player {

namespace {

// CASTLE_VARS=name,name,...: those clips' variables after their line, in the
// capture hook's format ("!! vars NAME name=value ...").
const std::set<std::string>& var_clips() {
    static const std::set<std::string> names = [] {
        std::set<std::string> out;
        if (const char* v = std::getenv("CASTLE_VARS")) {
            std::string all = v;
            for (size_t start = 0; start <= all.size();) {
                size_t comma = all.find(',', start);
                if (comma == std::string::npos) comma = all.size();
                if (comma > start) out.insert(all.substr(start, comma - start));
                start = comma + 1;
            }
        }
        return out;
    }();
    return names;
}

std::string var_line(MovieClip* mc, const std::string& name) {
    std::string line = "!! vars " + name;
    for (auto& [id, cell] : mc->props.cells()) {
        as::Value v = cell.load();
        std::string text;
        switch (v.type) {
        case as::kBool: text = v.b() ? "true" : "false"; break;
        case as::kInt: text = as::format("%d", v.i()); break;
        case as::kFloat: text = as::format("%.4f", double(v.f())); break;
        case as::kString: text = "'" + as::names().str(v.name()) + "'"; break;
        case as::kMovieClip:
            // A clip: its path, and whether it is still in use (a reference
            // to a clip taken apart, as after its movie went).
            if (MovieClip* target = MovieClip::from(as::object_of(v)); target && (target->refcount == 0 || !target->player)) {
                // (gone: its parent and movie may be gone too, not to be followed;
                // one without a movie is kept only for this reference)
                text = as::format("clip:(gone)#%u", target->serial);
            } else if (target) {
                std::string path;
                int up = 0;  // (a parent chain that loops, from a clip since reused, ends here)
                for (MovieClip* c = target; c && up < 24; c = c->parent, up++)
                    path = (c->name ? as::names().str(c->name) : std::string("?")) + (path.empty() ? "" : ".") + path;
                if (up == 24) path = "(loop)." + path;
                text = "clip:" + path + as::format("#%u", target->serial) + (target->refcount ? "" : "(gone)") + "@" +
                       (target->player ? target->player->name() : std::string("?"));
            } else {
                text = "clip:?";
            }
            break;
        default: text = as::format("<type %x>", v.type); break;
        }
        line += " " + as::names().str(id) + "=" + text;
    }
    return line + "\n";
}

void build(MovieClip* clip, int indent, bool with_text, std::string& text, std::string* vars) {
    for (Character* c = clip->first_child; c; c = c->next) {
        MovieClip* mc = MovieClip::from(c);
        const char* kind = mc ? "clip" : c->type() == as::kText ? "text" : "graphic";
        std::string line(size_t(indent) * 2, ' ');
        line += as::format("depth %d id %d %s", c->depth, c->character_id, kind);
        if (c->name) line += " '" + as::names().str(c->name) + "'";
        if (mc) line += as::format(" frame %d/%d flags %02x", mc->frame, mc->total_frames, mc->flags);
        line += as::format(" x %.3f y %.3f a %.3f d %.3f b %.3f c %.3f", c->matrix.tx, c->matrix.ty, c->matrix.a,
                           c->matrix.d, c->matrix.b, c->matrix.c);
        if (c->cxform)
            line += as::format(" cx mul %d %d %d %d add %d %d %d %d", c->cxform->mul[0], c->cxform->mul[1],
                               c->cxform->mul[2], c->cxform->mul[3], c->cxform->add[0], c->cxform->add[1],
                               c->cxform->add[2], c->cxform->add[3]);
        if (with_text && !c->text.empty()) line += " text '" + c->text + "'";
        text += line;
        text += '\n';
        if (vars && mc && c->name && !var_clips().empty()) {
            std::string name = as::names().str(c->name);
            if (var_clips().count(name)) *vars += var_line(mc, name);
        }
        if (mc) build(mc, indent + 1, with_text, text, vars);
    }
}

}  // namespace

void dump_tree(FILE* out, MovieClip* clip, int indent, bool with_text) {
    std::string text, vars;
    build(clip, indent, with_text, text, &vars);
    std::fputs(text.c_str(), out);
    std::fputs(vars.c_str(), out);
}

std::string tree_text(MovieClip* root, std::string* vars) {
    std::string text;
    build(root, 0, false, text, vars);
    return text;
}

uint64_t tree_hash(const std::string& text) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '-' && text.compare(i, 6, "-0.000") == 0 &&
            (i + 6 == text.size() || text[i + 6] < '0' || text[i + 6] > '9'))
            continue;
        h = (h ^ uint8_t(text[i])) * 1099511628211ull;
    }
    return h;
}

namespace {

// Every named clip's variables, by path from the root.
void all_vars(MovieClip* clip, const std::string& path, std::string& text) {
    for (Character* c = clip->first_child; c; c = c->next) {
        MovieClip* mc = MovieClip::from(c);
        if (!mc) continue;
        std::string name = c->name ? as::names().str(c->name) : as::format("(depth %d)", c->depth);
        std::string here = path + "." + name;
        if (c->name && !mc->props.cells().empty()) text += var_line(mc, here);
        // A loaded movie's root: whose it is, and whether its scripts'
        // functions are there to be called (a level's once weren't).
        if (mc->flags & MovieClip::kLoadedMovieRoot) {
            text += "!! loaded " + here + as::format(" #%u", mc->serial) + " of " +
                    (mc->player ? mc->player->name() : std::string("?")) + ", functions:";
            for (const char* f : {"f_Init", "f_Main", "f_LoadLevelClips", "f_SetPortals"})
                text += std::string(" ") + f + "=" + (mc->runner.functions.count(as::names().intern(f)) ? "yes" : "no");
            text += "\n";
        }
        all_vars(mc, here, text);
    }
}

}  // namespace

void dump_state(FILE* out, MovieClip* root) {
    std::string tree, vars;
    build(root, 0, true, tree, nullptr);
    vars = var_line(root, "_root");
    all_vars(root, "_root", vars);
    std::fputs(tree.c_str(), out);
    std::fputs(vars.c_str(), out);
}

void dump_update(FILE* out, Player& p, unsigned update) {
    MovieClip* root = p.root();
    std::string vars;
    std::string text = tree_text(root, &vars);
    std::fprintf(out, "== %s update %u frame %d/%d\n", p.name().c_str(), update, root->frame, root->total_frames);
    std::fprintf(out, "!! hash %016llx\n", static_cast<unsigned long long>(tree_hash(text)));
    std::fputs(text.c_str(), out);
    std::fputs(vars.c_str(), out);
}

}  // namespace player
