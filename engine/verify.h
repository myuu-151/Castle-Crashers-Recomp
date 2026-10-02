// Checking the engine against castle.exe without playing:
//
//   castle --verify SESSION   replays a session the capture hook recorded
//                             (every key and pad reading and every
//                             update's tree hash) and compares each update's
//                             tree with the real game's
//   castle --selftest CASES OUT
//                             runs tools/selftest.py's cases (bytecode, level
//                             collision) in the engine, for comparison with
//                             the hook's results from castle.exe itself
#pragma once

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "player/game.h"

namespace verify {

// Before game.start(): reads the session's expected updates. Then the
// returned object checks each update (call on_update from
// Player::on_updated) and reports.
class Session {
public:
    bool load(const std::string& path);
    void on_update(player::Player& p);
    // Every real update seen, or ours well past the session's end.
    bool done() const { return consumed_ >= expected_.size() || ours_ > last_update_ + 100; }
    unsigned last_update() const { return last_update_; }
    int report() const;  // prints the summary; 0 if everything matched

private:
    struct Expected {
        int total = 0, frame = 0, occurrence = 0;
        std::string movie;
        unsigned update = 0;
        bool has_hash = false;
        unsigned long long hash = 0;
        std::vector<std::string> lines;  // when the hook wrote the whole tree
    };
    void mismatch(const Expected* e, player::Player& p, const std::string& ours, const char* what);

    std::string path_;
    std::vector<Expected> expected_;
    std::map<std::tuple<int, int, int>, size_t> index_;
    std::map<std::pair<int, int>, int> seen_;
    FILE* align_ = nullptr;  // verify/align.txt: each of our updates and its real counterpart
    std::map<std::string, std::pair<size_t, unsigned>> extra_by_movie_;  // count, first of our updates
    std::vector<bool> hit_;
    // The pause movie's updates, compared in order (the nth of ours with the
    // nth real one): its frames repeat, so they can't be keyed like the rest.
    struct PauseUpdate {
        unsigned update = 0;
        bool has_hash = false;
        unsigned long long hash = 0;
    };
    std::vector<PauseUpdate> pause_;
    size_t pause_seen_ = 0, pause_matched_ = 0, pause_differ_ = 0;
    unsigned pause_first_real_ = 0, pause_first_ours_ = 0;
    size_t consumed_ = 0, matched_ = 0, mismatched_ = 0, extra_ = 0, unchecked_ = 0;
    unsigned ours_ = 0, last_update_ = 0;
    int reported_ = 0, full_reported_ = 0;
};

int selftest(player::Game& game, const std::string& cases, const std::string& out);

}  // namespace verify
