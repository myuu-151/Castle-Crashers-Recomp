#include "verify.h"

#include <SDL3/SDL_log.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

#include "as/interpreter.h"
#include "as/value.h"
#include "player/dump.h"
#include "player/movieclip.h"

namespace verify {

namespace fs = std::filesystem;

namespace {

bool skipped(const std::string& movie) { return movie == "loading" || movie == "pause"; }

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        out.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

void write_lines(const fs::path& file, const std::vector<std::string>& lines) {
    if (FILE* f = std::fopen(file.string().c_str(), "w")) {
        for (const auto& l : lines) std::fprintf(f, "%s\n", l.c_str());
        std::fclose(f);
    }
}

}  // namespace

bool Session::load(const std::string& path) {
    path_ = path;
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    static char line[1 << 16];
    Expected* current = nullptr;
    PauseUpdate* pause_hash = nullptr;
    std::map<std::pair<int, int>, int> seen;
    bool cut = false;  // the last line has no newline: the game was closed mid-write
    while (std::fgets(line, sizeof(line), f)) {
        size_t n = std::strlen(line);
        cut = !n || line[n - 1] != '\n';
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        char movie[64];
        unsigned update = 0;
        int frame = 0, total = 0;
        if (std::sscanf(line, "== %63s update %u frame %d/%d", movie, &update, &frame, &total) == 4) {
            last_update_ = update;
            pause_hash = nullptr;
            if (std::string(movie) == "pause") {
                pause_.push_back({update, false, 0});
                pause_hash = &pause_.back();
            }
            if (skipped(movie)) {
                current = nullptr;
                continue;
            }
            Expected e;
            e.total = total;
            e.frame = frame;
            e.occurrence = ++seen[{total, frame}];
            e.movie = movie;
            e.update = update;
            index_[{total, frame, e.occurrence}] = expected_.size();
            expected_.push_back(std::move(e));
            current = &expected_.back();
        } else if (!std::strncmp(line, "!! hash ", 8)) {
            if (pause_hash) {
                pause_hash->hash = std::strtoull(line + 8, nullptr, 16);
                pause_hash->has_hash = true;
            }
            if (current) {
                current->hash = std::strtoull(line + 8, nullptr, 16);
                current->has_hash = true;
            }
        } else if (!std::strncmp(line, "!!", 2)) {
            continue;
        } else if (current && n) {
            current->lines.push_back(line);
        }
    }
    std::fclose(f);
    if (cut && !expected_.empty()) {
        index_.erase({expected_.back().total, expected_.back().frame, expected_.back().occurrence});
        expected_.pop_back();
    }
    // Older captures have the whole tree and no hash line.
    for (Expected& e : expected_) {
        if (e.has_hash || e.lines.empty()) continue;
        std::string text;
        for (const auto& l : e.lines) text += l + "\n";
        e.hash = player::tree_hash(text);
        e.has_hash = true;
    }
    hit_.assign(expected_.size(), false);
    return true;
}

void Session::mismatch(const Expected* e, player::Player& p, const std::string& ours, const char* what) {
    // The first 10 mismatches, and after them the first 5 where the session
    // has the real tree to compare line by line.
    bool full = e && !e->lines.empty();
    if (reported_ >= 10 && !(full && full_reported_ < 5)) return;
    reported_++;
    if (full) full_reported_++;
    std::fflush(stdout);
    fs::create_directories("verify-out");
    auto our_lines = split_lines(ours);
    fs::path ours_file = fs::path("verify-out") / ("ours_" + std::to_string(ours_) + ".txt");
    write_lines(ours_file, our_lines);
    player::MovieClip* root = p.root();
    if (!e) {
        std::printf("EXTRA: our update #%u (%s frame %d/%d) has no real counterpart; tree in %s\n", ours_,
                    p.name().c_str(), root->frame, root->total_frames, ours_file.string().c_str());
        return;
    }
    std::printf("%s: real update #%u (%s frame %d/%d, occurrence %d), our update #%u\n", what, e->update,
                e->movie.c_str(), e->frame, e->total, e->occurrence, ours_);
    if (!e->lines.empty()) {
        fs::path real_file = fs::path("verify-out") / ("real_" + std::to_string(e->update) + ".txt");
        write_lines(real_file, e->lines);
        int shown = 0;
        size_t count = std::max(e->lines.size(), our_lines.size());
        for (size_t i = 0; i < count && shown < 8; i++) {
            const std::string& r = i < e->lines.size() ? e->lines[i] : std::string("(none)");
            const std::string& o = i < our_lines.size() ? our_lines[i] : std::string("(none)");
            if (r == o) continue;
            std::printf("  line %zu\n    real: %s\n    ours: %s\n", i + 1, r.c_str(), o.c_str());
            shown++;
        }
        std::printf("  trees: %s, %s\n", real_file.string().c_str(), ours_file.string().c_str());
    } else {
        std::printf("  the real tree isn't in the session (only its hash); ours is in %s.\n"
                    "  To record the real one, put this in CastleCrashers/castle_hook.txt and run castle.exe:\n"
                    "    replay=%s\n    full_from=%u\n    full_to=%u\n    output=verify_real.txt\n",
                    ours_file.string().c_str(), path_.c_str(), e->update > 5 ? e->update - 5 : 1, e->update + 5);
    }
}

void Session::on_update(player::Player& p) {
    ours_++;
    if (!align_) {
        fs::create_directories("verify-out");
        align_ = std::fopen("verify-out/align.txt", "w");
    }
    // CASTLE_VERIFY_SAVE=from-to: our trees for those updates of ours, to
    // look at what happens around a mismatch.
    static unsigned save_from = 0, save_to = 0;
    static bool save_read = false;
    if (!save_read) {
        save_read = true;
        if (const char* s = std::getenv("CASTLE_VERIFY_SAVE")) std::sscanf(s, "%u-%u", &save_from, &save_to);
    }
    auto save_tree = [&](const std::string& text) {
        if (ours_ >= save_from && ours_ <= save_to)
            write_lines(fs::path("verify-out") / ("ours_" + std::to_string(ours_) + ".txt"), split_lines(text));
    };
    if (p.name() == "pause") {
        save_tree(player::tree_text(p.root()));
        size_t i = pause_seen_++;
        const char* status = "extra";
        if (i < pause_.size() && pause_[i].has_hash) {
            if (player::tree_hash(player::tree_text(p.root())) == pause_[i].hash) {
                pause_matched_++;
                status = "match";
            } else {
                if (!pause_differ_++) {
                    pause_first_real_ = pause_[i].update;
                    pause_first_ours_ = ours_;
                    fs::create_directories("verify-out");
                    write_lines(fs::path("verify-out") / ("ours_" + std::to_string(ours_) + ".txt"),
                                split_lines(player::tree_text(p.root())));
                }
                status = "DIFFER";
            }
        }
        if (align_)
            std::fprintf(align_, "ours %u pause #%zu real %u %s\n", ours_, i + 1, i < pause_.size() ? pause_[i].update : 0,
                         status);
        return;
    }
    if (skipped(p.name())) {
        if (align_) std::fprintf(align_, "ours %u %s (not compared)\n", ours_, p.name().c_str());
        return;
    }
    player::MovieClip* root = p.root();
    int occurrence = ++seen_[{root->total_frames, root->frame}];
    std::string text = player::tree_text(root);
    auto it = index_.find({root->total_frames, root->frame, occurrence});
    save_tree(text);
    if (align_) {
        std::fprintf(align_, "ours %u %s %d/%d #%d", ours_, p.name().c_str(), root->frame, root->total_frames,
                     occurrence);
        if (it == index_.end()) {
            std::fprintf(align_, " extra\n");
        } else {
            const Expected& e = expected_[it->second];
            std::fprintf(align_, " real %u %s\n", e.update,
                         !e.has_hash ? "unchecked" : player::tree_hash(text) == e.hash ? "match" : "DIFFER");
        }
    }
    if (it == index_.end()) {
        extra_++;
        auto& [count, first] = extra_by_movie_[p.name()];
        if (!count++) first = ours_;
        mismatch(nullptr, p, text, "EXTRA");
        return;
    }
    Expected& e = expected_[it->second];
    if (!hit_[it->second]) {
        hit_[it->second] = true;
        consumed_++;
    }
    if (!e.has_hash) {
        unchecked_++;
        return;
    }
    if (player::tree_hash(text) == e.hash) {
        matched_++;
    } else {
        mismatched_++;
        mismatch(&e, p, text, "MISMATCH");
    }
    if (consumed_ % 1000 == 0) {
        std::printf("... %zu of %zu updates checked (%s frame %d/%d, %zu clips)\n", consumed_, expected_.size(),
                    p.name().c_str(), root->frame, root->total_frames, player::live_clips());
        std::fflush(stdout);
    }
}

int Session::report() const {
    size_t missing = expected_.size() - consumed_;
    std::printf("verified %zu real updates: %zu match, %zu differ, %zu without a hash; %zu of ours had no "
                "counterpart, %zu of the real ones never happened\n",
                expected_.size(), matched_, mismatched_, unchecked_, extra_, missing);
    if (!pause_.empty() || pause_seen_) {
        std::printf("pause movie: %zu real updates, %zu of ours: %zu match, %zu differ", pause_.size(), pause_seen_,
                    pause_matched_, pause_differ_);
        if (pause_differ_)
            std::printf(" (first: real #%u, our #%u, tree in verify-out)", pause_first_real_, pause_first_ours_);
        std::printf("\n");
    }
    for (const auto& [movie, extra] : extra_by_movie_)
        std::printf("  %zu of ours in %s had no counterpart, from our update #%u\n", extra.first,
                    movie.c_str(), extra.second);
    int shown = 0;
    for (size_t i = 0; i < expected_.size() && shown < 5; i++)
        if (!hit_[i]) {
            const Expected& e = expected_[i];
            std::printf("  never happened: real update #%u (%s frame %d/%d, occurrence %d)\n", e.update,
                        e.movie.c_str(), e.frame, e.total, e.occurrence);
            shown++;
        }
    return mismatched_ || extra_ || missing ? 1 : 0;
}

// ---- Self-test

namespace {

std::string value_text(const as::Value& v) {
    switch (v.type) {
    case as::kUndefined: return "u";
    case as::kBool: return v.b() ? "b:1" : "b:0";
    case as::kInt: return as::format("i:%d", v.i());
    case as::kFloat: {
        float f = v.f();
        uint32_t bits = 0;
        std::memcpy(&bits, &f, 4);
        return as::format("f:%08x", bits);
    }
    case as::kString: {
        std::string hex = "s:";
        for (unsigned char ch : as::names().str(v.name())) hex += as::format("%02x", ch);
        return hex;
    }
    default: return as::format("o:%x", v.type);
    }
}

uint32_t hex_bits(const char* s) { return uint32_t(std::strtoul(s, nullptr, 16)); }

}  // namespace

int selftest(player::Game& game, const std::string& cases_file, const std::string& out_file) {
    FILE* in = std::fopen(cases_file.c_str(), "r");
    FILE* out = std::fopen(out_file.c_str(), "w");
    if (!in || !out) return 1;
    std::unique_ptr<player::Player> movie = game.load_standalone("logo");
    if (!movie) return 1;
    player::MovieClip* root = movie->root();
    as::Interpreter& interp = movie->interp();
    std::vector<std::vector<uint8_t>> codes;  // kept alive: functions defined by a case point into them
    static char line[1 << 16];
    unsigned count = 0;
    while (std::fgets(line, sizeof(line), in)) {
        char kind[16] = {};
        if (std::sscanf(line, "%15s", kind) != 1) continue;
        if (!std::strcmp(kind, "as")) {
            unsigned id = 0;
            static char hex[1 << 15];
            if (std::sscanf(line, "as %u %32767s", &id, hex) != 2) continue;
            std::vector<uint8_t> code;
            for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
                unsigned b = 0;
                std::sscanf(hex + i, "%2x", &b);
                code.push_back(uint8_t(b));
            }
            code.push_back(0);
            codes.push_back(std::move(code));
            root->runner.enter_code = as::Code{codes.back().data(), codes.back().size()};
            interp.push_target(root);
            interp.run_clip_event(root->runner, false);
            interp.pop_target();
            std::string prefix = as::format("t%u_", id);
            std::string result = "as " + std::to_string(id);
            for (auto& [name_id, cell] : root->props.cells()) {
                std::string name = as::names().str(name_id);
                if (name.compare(0, prefix.size(), prefix) != 0) continue;
                result += " " + name + "=" + value_text(cell.load());
            }
            std::fprintf(out, "%s\n", result.c_str());
            count++;
        } else if (!std::strcmp(kind, "bsp")) {
            char name[64] = {};
            if (std::sscanf(line, "bsp %63s", name) == 1 && !game.bsp.load(game.bsp_path(name)))
                std::fprintf(out, "bsp %s CRASH\n", name);
        } else if (!std::strcmp(kind, "hit")) {
            unsigned id = 0;
            char a[16], b[16], c[16], d[16];
            if (std::sscanf(line, "hit %u %15s %15s %15s %15s", &id, a, b, c, d) != 5) continue;
            uint32_t v[4] = {hex_bits(a), hex_bits(b), hex_bits(c), hex_bits(d)};
            float f[4];
            std::memcpy(f, v, sizeof(f));
            float u = game.bsp.hit_test(f[0], f[1], f[2], f[3]);
            uint32_t ub = 0, sb = 0;
            std::memcpy(&ub, &u, 4);
            std::memcpy(&sb, &game.bsp.last_slope, 4);
            std::fprintf(out, "hit %u %08x %d %d %08x\n", id, ub, game.bsp.last_type, game.bsp.last_index, sb);
            count++;
        }
    }
    std::fclose(in);
    std::fclose(out);
    SDL_Log("ran %u self-test cases into %s", count, out_file.c_str());
    return 0;
}

}  // namespace verify
