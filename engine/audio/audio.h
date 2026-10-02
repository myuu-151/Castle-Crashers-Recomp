// castle.exe's sound: the manager `*(0x651f68)` (the original's and the
// functions around it) over an XAudio2 engine, which plays the
// game's xWMA files (data/sounds, data/music, "*.xma") straight from memory.
// See docs/engine/audio.md.
//
// Effects are found by name through a map per movie (the movie updating when
// attachSound and start run); a movie's effects are unloaded, and their
// voices cut, when the movie goes. One music track plays at a time, with
// linear fades. Nothing here is visible to the scripts except the volume
// getters, so the display trees don't depend on it.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace audio {

// What plays the sound (castle.exe's AudioEngine): XAudio2 on
// Windows (audio.cpp), or a platform's own (Manager::make_engine). Effects
// are loaded into slots (512) and music into ids (64); a voice is a number
// the engine gives.
class Engine {
public:
    virtual ~Engine() = default;
    virtual bool init() = 0;
    // `path` is the file's without its extension: dir/sounds/NAME,
    // dir/music/NAME. False if it can't be loaded.
    virtual bool load_effect(uint16_t slot, const std::filesystem::path& path) = 0;
    virtual bool load_music(int id, const std::filesystem::path& path) = 0;
    virtual int free_effect_slot() const = 0;  // -1 if none
    virtual bool has_music(int id) const = 0;
    // Every voice playing it stopped, then the data freed.
    virtual void unload_effect(uint16_t slot) = 0;
    virtual void unload_music(int id) = 0;
    // A voice for an effect; -1 when all are busy.
    virtual int16_t play_effect(uint16_t slot, float volume, float pan, bool loop) = 0;
    virtual bool play_music(int id, float volume, bool loop) = 0;
    virtual void set_music_volume(float v) = 0;
    virtual void stop_music() = 0;
    // Only stopped: it keeps its number until its sound goes.
    virtual void stop_voice(int16_t voice) = 0;
    virtual void set_voice(int16_t voice, float volume, float pan) = 0;
    // Finished voices go.
    virtual void update() = 0;
};

class Manager {
public:
    Manager();
    ~Manager();

    // A platform's engine, made by init; unset, XAudio2 on Windows.
    static inline std::unique_ptr<Engine> (*make_engine)() = nullptr;

    // Starts the engine and loads the menus' two sounds (keys 0 and 1).
    // Without it (captures, replays, no device) everything but the volumes
    // is off.
    bool init(const std::filesystem::path& audio_dir);
    void shutdown();
    bool enabled() const { return enabled_; }

    // The movie updating (the original's pushes and pops its node), and a
    // movie going away.
    void push(const void* movie);
    void pop();
    void unload(const void* movie);

    // Effects.
    void attach(uint32_t key, const std::string& name);
    int16_t play(uint32_t key, float volume, float pan, bool loop);
    void stop_loop(int16_t voice);
    void set_voice(int16_t voice, float volume, float pan);
    void stop_all_loops();
    void set_effects_paused(bool paused);
    void set_focus(bool active);
    // Ours: silent as if unfocused, whatever the focus, while held (a jump's
    // fast-forward); let go, the focus is back.
    void hold_silent(bool hold);

    // Music (natives 0x35-0x42).
    void register_music(int id, const std::string& name);
    void unregister_music(int id);
    void clear_all_music();
    void play_music(int id, bool loop);
    void stop_music();
    void pause_music(bool pause);
    void fade_in(int frames, int id, bool loop);
    void fade_out(int frames, bool clear);
    // The native menus' 3 s fade-out (starting a game, leaving a level).
    void menu_fade_out(bool clear);

    // The pause menu's mute.
    void pause_menu(bool open);

    // Volumes as the scripts see them: 0-100 (truncated), set from ints.
    int master_volume() const { return percent(master_); }
    int music_volume() const { return percent(music_); }
    int sfx_volume() const { return percent(sfx_); }
    void set_master_volume(int32_t n);
    void set_music_volume(int32_t n);
    void set_sfx_volume(int32_t n);

    // Once per main-loop iteration: fades, finished voices.
    void update(float dt);

private:
    static int percent(float v);
    std::map<uint32_t, uint16_t>& current_map();
    void music_voice_volume(float v);

    std::unique_ptr<Engine> engine_;
    std::filesystem::path dir_;
    bool enabled_ = false;             // +0x00
    bool fading_ = false;              // +0x01
    bool fade_clear_ = false;          // +0x02
    bool effects_paused_ = false;      // +0x03
    std::vector<const void*> stack_;   // +0x08, depth +0x48
    std::map<const void*, std::map<uint32_t, uint16_t>> maps_;  // +0x4c
    int music_state_ = 0;              // +0x60: 0 stopped, 1 playing, 2 muted, 3 playing while muted
    bool music_loop_ = false;          // +0x64
    int music_id_ = 0;                 // +0x68
    float master_ = 0.9f;              // +0x6c
    float sfx_ = 0.9f;                 // +0x70
    float music_ = 0.9f;               // +0x74
    float focus_ = 1.0f;               // +0x7c
    bool held_silent_ = false;         // (ours: hold_silent)
    float fade_level_ = 0, fade_target_ = 0, fade_rate_ = 0;  // +0x80, +0x84, +0x88
    struct Loop {
        int16_t voice;
        float volume, pan;
    };
    std::vector<Loop> loops_;  // +0x8c
};

// The one manager.
Manager& manager();

}  // namespace audio
