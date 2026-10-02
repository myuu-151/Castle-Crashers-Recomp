#include "audio/audio.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xaudio2.h>
#endif

namespace audio {

namespace {

#ifdef _WIN32

// A loaded file: its format, the xWMA packet table ("dpds")
// and the whole data chunk, in memory.
struct SoundData {
    std::vector<uint8_t> format;  // WAVEFORMATEX and its extra bytes
    std::vector<uint32_t> packets;
    std::vector<uint8_t> data;
    int channels = 1;
};

std::unique_ptr<SoundData> load_file(const std::filesystem::path& path) {
    FILE* f = std::fopen(path.string().c_str(), "rb");
    if (!f) return nullptr;
    std::vector<uint8_t> file;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) file.insert(file.end(), buf, buf + n);
    std::fclose(f);
    if (file.size() < 12 || std::memcmp(file.data(), "RIFF", 4) != 0) return nullptr;
    bool xwma = std::memcmp(file.data() + 8, "XWMA", 4) == 0;
    if (!xwma && std::memcmp(file.data() + 8, "WAVE", 4) != 0) return nullptr;
    auto sound = std::make_unique<SoundData>();
    size_t pos = 12;
    while (pos + 8 <= file.size()) {
        uint32_t size = 0;
        std::memcpy(&size, file.data() + pos + 4, 4);
        const uint8_t* body = file.data() + pos + 8;
        size_t avail = std::min<size_t>(size, file.size() - pos - 8);
        if (std::memcmp(file.data() + pos, "fmt ", 4) == 0) {
            sound->format.assign(body, body + avail);
        } else if (std::memcmp(file.data() + pos, "dpds", 4) == 0) {
            sound->packets.resize(avail / 4);
            std::memcpy(sound->packets.data(), body, sound->packets.size() * 4);
        } else if (std::memcmp(file.data() + pos, "data", 4) == 0) {
            sound->data.assign(body, body + avail);
        }
        pos += 8 + size;  // castle.exe ignores the pad byte too
    }
    if (sound->format.size() < 16 || sound->data.empty()) return nullptr;
    uint16_t channels = 0;
    std::memcpy(&channels, sound->format.data() + 2, 2);
    sound->channels = channels;
    // WAVEFORMATEX needs its cbSize field.
    if (sound->format.size() < 18) sound->format.resize(18, 0);
    return sound;
}

#endif

// "sound_toggle_b.wav" -> data/sounds/sound_toggle_b.xma: the name up to its
// first '.'; music names start after their first '/'.
std::string stem(std::string name) {
    auto dot = name.find('.');
    if (dot != std::string::npos) name.resize(dot);
    return name;
}

}  // namespace

// ---- castle.exe's AudioEngine over XAudio2

#ifdef _WIN32

class XAudio2Engine : public Engine {
public:
    ~XAudio2Engine() override {
        for (Voice& v : voices_)
            if (v.voice) v.voice->DestroyVoice();
        if (master_) master_->DestroyVoice();
        if (xaudio_) xaudio_->Release();
    }

    bool init() override {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        (void)hr;  // already initialized in another mode is fine
        if (FAILED(XAudio2Create(&xaudio_, 0, XAUDIO2_DEFAULT_PROCESSOR))) return false;
        // castle.exe asks for 6 channels and only feeds front left/right.
        if (FAILED(xaudio_->CreateMasteringVoice(&master_, 2, 0))) return false;
        return true;
    }

    // Effects: 512 slots; music: 64. The game's .xma files.
    SoundData* effect(uint16_t slot) { return slot < effects_.size() ? effects_[slot].get() : nullptr; }
    int free_effect_slot() const override {
        for (size_t i = 0; i < effects_.size(); i++)
            if (!effects_[i]) return int(i);
        return -1;
    }
    bool load_effect(uint16_t slot, const std::filesystem::path& path) override {
        std::filesystem::path file = path;
        auto d = load_file(file += ".xma");
        if (!d) return false;
        effects_[slot] = std::move(d);
        return true;
    }
    bool load_music(int id, const std::filesystem::path& path) override {
        std::filesystem::path file = path;
        auto d = load_file(file += ".xma");
        if (!d) return false;
        music_[size_t(id)] = std::move(d);
        return true;
    }
    bool has_music(int id) const override { return music_[size_t(id)] != nullptr; }

    // every voice playing it stopped, flushed and destroyed,
    // then the data freed.
    void unload_effect(uint16_t slot) override { unload(effects_[slot]); }
    void unload_music(int id) override { unload(music_[size_t(id)]); }

    // a voice for an effect; its index, or -1 when all 256
    // are busy (it plays anyway, untracked).
    int16_t play_effect(uint16_t slot, float volume, float pan, bool loop) override {
        SoundData* d = effect(slot);
        if (!d) return -1;
        IXAudio2SourceVoice* v = start(*d, loop);
        if (!v) return -1;
        v->SetVolume(volume);
        set_pan(v, *d, pan);
        return track(v, d, false);
    }


    bool play_music(int id, float volume, bool loop) override {
        SoundData* d = music_[size_t(id)].get();
        if (!d) return false;
        IXAudio2SourceVoice* v = start(*d, loop);
        if (!v) return false;
        set_pan(v, *d, 0.0f);
        v->SetVolume(volume);
        music_voice_ = track(v, d, true);
        return true;
    }


    void set_music_volume(float v) override {
        if (IXAudio2SourceVoice* voice = music_voice()) voice->SetVolume(v);
    }
    void stop_music() override {
        if (IXAudio2SourceVoice* voice = music_voice()) {
            voice->Stop(0);
            voice->FlushSourceBuffers();
        }
    }

    // only stopped, so it keeps its entry until its sound goes.
    void stop_voice(int16_t id) override {
        if (id >= 0 && size_t(id) < voices_.size() && voices_[size_t(id)].voice) voices_[size_t(id)].voice->Stop(0);
    }

    void set_voice(int16_t id, float volume, float pan) override {
        if (id < 0 || size_t(id) >= voices_.size() || !voices_[size_t(id)].voice) return;
        Voice& v = voices_[size_t(id)];
        v.voice->SetVolume(volume);
        set_pan(v.voice, *v.data, pan);
    }

    // finished voices go.
    void update() override {
        for (size_t i = 0; i < voices_.size(); i++) {
            Voice& v = voices_[i];
            if (!v.voice) continue;
            XAUDIO2_VOICE_STATE state;
            v.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (state.BuffersQueued == 0) {
                v.voice->DestroyVoice();
                v = Voice{};
                if (music_voice_ == int(i)) music_voice_ = -1;
            }
        }
    }

private:
    struct Voice {
        IXAudio2SourceVoice* voice = nullptr;
        SoundData* data = nullptr;
    };

    IXAudio2SourceVoice* start(SoundData& d, bool loop) {
        IXAudio2SourceVoice* v = nullptr;
        auto* fmt = reinterpret_cast<const WAVEFORMATEX*>(d.format.data());
        if (FAILED(xaudio_->CreateSourceVoice(&v, fmt, 0, 2.0f))) return nullptr;
        XAUDIO2_BUFFER buffer = {};
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.AudioBytes = UINT32(d.data.size());
        buffer.pAudioData = d.data.data();
        buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;
        XAUDIO2_BUFFER_WMA wma = {};
        wma.pDecodedPacketCumulativeBytes = d.packets.data();
        wma.PacketCount = UINT32(d.packets.size());
        if (FAILED(v->SubmitSourceBuffer(&buffer, d.packets.empty() ? nullptr : &wma)) || FAILED(v->Start(0))) {
            v->DestroyVoice();
            return nullptr;
        }
        return v;
    }

    // The balance: the far side falls linearly, the near one
    // stays at 1; only the front left and right outputs are used.
    void set_pan(IXAudio2SourceVoice* v, const SoundData& d, float pan) {
        float left = pan == 0.0f ? 1.0f : std::min(1.0f, 1.0f - pan);
        float right = pan == 0.0f ? 1.0f : std::min(1.0f, 1.0f + pan);
        if (d.channels == 1) {
            float m[2] = {left, right};
            v->SetOutputMatrix(nullptr, 1, 2, m);
        } else {
            float m[4] = {left, 0.0f, 0.0f, right};
            v->SetOutputMatrix(nullptr, 2, 2, m);
        }
    }

    int16_t track(IXAudio2SourceVoice* v, SoundData* d, bool) {
        for (size_t i = 0; i < voices_.size(); i++) {
            if (!voices_[i].voice) {
                voices_[i] = {v, d};
                return int16_t(i);
            }
        }
        return -1;
    }

    IXAudio2SourceVoice* music_voice() {
        return music_voice_ >= 0 ? voices_[size_t(music_voice_)].voice : nullptr;
    }

    void unload(std::unique_ptr<SoundData>& d) {
        if (!d) return;
        for (size_t i = 0; i < voices_.size(); i++) {
            Voice& v = voices_[i];
            if (v.data != d.get() || !v.voice) continue;
            v.voice->Stop(0);
            v.voice->FlushSourceBuffers();
            v.voice->DestroyVoice();
            v = Voice{};
            if (music_voice_ == int(i)) music_voice_ = -1;
        }
        d.reset();
    }

    IXAudio2* xaudio_ = nullptr;
    IXAudio2MasteringVoice* master_ = nullptr;
    std::vector<std::unique_ptr<SoundData>> effects_ = std::vector<std::unique_ptr<SoundData>>(512);
    std::vector<std::unique_ptr<SoundData>> music_ = std::vector<std::unique_ptr<SoundData>>(64);
    std::vector<Voice> voices_ = std::vector<Voice>(256);
    int music_voice_ = -1;  // engine+0x1100
};

#endif

std::unique_ptr<Engine> default_engine() {
#ifdef _WIN32
    return std::make_unique<XAudio2Engine>();
#else
    return nullptr;  // a platform without one sets Manager::make_engine
#endif
}

// ---- Manager

Manager::Manager() = default;
Manager::~Manager() = default;

Manager& manager() {
    static Manager m;
    return m;
}

bool Manager::init(const std::filesystem::path& audio_dir) {
    dir_ = audio_dir;
    engine_ = make_engine ? make_engine() : default_engine();
    if (!engine_ || !engine_->init()) {
        SDL_Log("audio: no sound device, the game plays silent");
        engine_.reset();
        return false;
    }
    enabled_ = true;
    // The menus' sounds, keys 0 and 1 of the map outside any movie
    //.
    attach(0, "sound_toggle_b.wav");
    attach(1, "sound_player_select.wav");
    return true;
}

void Manager::shutdown() {
    enabled_ = false;
    maps_.clear();
    loops_.clear();
    engine_.reset();
}

int Manager::percent(float v) {
    // FLD, FMUL 100, FISTP with rounding set to chop, at single precision.
    float p = v * 100.0f;
    return int(std::trunc(p));
}

std::map<uint32_t, uint16_t>& Manager::current_map() {
    return maps_[stack_.empty() ? nullptr : stack_.back()];
}

void Manager::push(const void* movie) { stack_.push_back(movie); }

void Manager::pop() {
    if (!stack_.empty()) stack_.pop_back();
}

void Manager::unload(const void* movie) {
    auto it = maps_.find(movie);
    if (it == maps_.end()) return;
    if (engine_)
        for (const auto& [key, slot] : it->second) engine_->unload_effect(slot);
    maps_.erase(it);
}

void Manager::attach(uint32_t key, const std::string& name) {
    if (!enabled_) return;
    auto& map = current_map();
    if (map.count(key)) return;
    int slot = engine_->free_effect_slot();
    if (slot < 0) return;
    if (!engine_->load_effect(uint16_t(slot), dir_ / "sounds" / stem(name))) return;
    map[key] = uint16_t(slot);
}

int16_t Manager::play(uint32_t key, float volume, float pan, bool loop) {
    if (!enabled_) return 1;
    auto& map = current_map();
    auto it = map.find(key);
    if (it == map.end()) return 1;
    int16_t v = engine_->play_effect(it->second, sfx_ * volume * master_ * focus_, pan, loop);
    if (loop) loops_.push_back({v, volume, pan});
    return v;
}

void Manager::stop_loop(int16_t voice) {
    for (auto it = loops_.begin(); it != loops_.end(); ++it) {
        if (it->voice != voice) continue;
        loops_.erase(it);
        if (engine_) engine_->stop_voice(voice);
        return;
    }
}

void Manager::set_voice(int16_t voice, float volume, float pan) {
    if (engine_) engine_->set_voice(voice, sfx_ * master_ * focus_ * volume, pan);
}

void Manager::stop_all_loops() {
    if (engine_)
        for (const Loop& l : loops_) engine_->stop_voice(l.voice);
    loops_.clear();
}

void Manager::set_effects_paused(bool paused) {
    effects_paused_ = paused;
    if (!engine_) return;
    for (const Loop& l : loops_)
        engine_->set_voice(l.voice, paused ? 0.0f : sfx_ * l.volume * master_ * focus_, l.pan);
}

void Manager::hold_silent(bool hold) {
    held_silent_ = false;
    set_focus(!hold);
    held_silent_ = hold;
}

void Manager::set_focus(bool active) {
    if (held_silent_) return;
    float f = active ? 1.0f : 0.0f;
    if (f == focus_) return;
    focus_ = f;
    if (!engine_) return;
    if (music_state_ == 1 || music_state_ == 3) engine_->set_music_volume(music_ * master_ * focus_);
    else if (music_state_ == 2) engine_->set_music_volume(0.0f);
    for (const Loop& l : loops_)
        engine_->set_voice(l.voice, l.volume * (effects_paused_ ? 0.0f : sfx_ * master_ * focus_), l.pan);
}

void Manager::music_voice_volume(float v) {
    if (engine_) engine_->set_music_volume(v);
}

void Manager::register_music(int id, const std::string& name) {
    if (!enabled_ || id < 0 || id >= 64 || engine_->has_music(id)) return;
    // "music/NG90433.wav" -> data/music/NG90433.xma
    std::string s = name;
    auto slash = s.find('/');
    if (slash != std::string::npos) s = s.substr(slash + 1);
    engine_->load_music(id, dir_ / "music" / stem(s));
}

void Manager::unregister_music(int id) {
    if (!enabled_) return;
    if (id == music_id_) stop_music();
    if (id >= 0 && id < 64) engine_->unload_music(id);
}

void Manager::clear_all_music() {
    if (!enabled_) return;
    fading_ = false;
    music_voice_volume(0.0f);
    engine_->stop_music();
    music_id_ = 0;
    music_state_ = 0;
    for (int i = 0; i < 64; i++) engine_->unload_music(i);
}

void Manager::play_music(int id, bool loop) {
    if (!enabled_) return;
    if (music_state_ == 1 && music_id_ == id) return;
    if (music_state_ != 0) stop_music();
    engine_->stop_music();
    if (id >= 0 && id < 64 && engine_->play_music(id, music_ * master_ * focus_, loop)) {
        music_id_ = id;
        music_state_ = 1;
        music_loop_ = loop;
    }
}

void Manager::stop_music() {
    if (!enabled_) return;
    fading_ = false;
    music_voice_volume(0.0f);
    engine_->stop_music();
    music_id_ = 0;
    music_state_ = 0;
}

void Manager::pause_music(bool pause) {
    if (!enabled_) return;
    if ((music_state_ == 1 || music_state_ == 3) && pause) {
        music_voice_volume(0.0f);
        music_state_ = 2;
    } else if (music_state_ == 2 && !pause) {
        music_state_ = 1;
        music_voice_volume(music_ * master_ * focus_);
    }
}

void Manager::fade_in(int frames, int id, bool loop) {
    if (!enabled_) return;
    float duration = float(uint16_t(frames)) * 0.033333335f;
    float rate = music_ / duration;
    float saved = music_;
    music_ = 0.0f;  // it starts silent
    play_music(id, loop);
    music_ = saved;
    fade_target_ = music_;
    fade_level_ = 0.0f;
    fade_rate_ = rate;
    fading_ = true;
}

void Manager::fade_out(int frames, bool clear) {
    if (!enabled_) return;
    float duration = float(uint16_t(frames)) / 30.0f;
    fade_target_ = 0.0f;
    fade_level_ = music_;
    fade_rate_ = music_ / duration;
    fading_ = true;
    fade_clear_ = clear;
}

void Manager::menu_fade_out(bool clear) {
    if (!enabled_) return;
    fade_target_ = 0.0f;
    fade_level_ = music_;
    fade_rate_ = music_ * 0.33333334f;
    fading_ = true;
    fade_clear_ = clear;
}

void Manager::pause_menu(bool open) {
    if (!enabled_) return;
    if (open) {
        if (music_state_ == 1 || music_state_ == 3) {
            music_voice_volume(0.0f);
            music_state_ = 2;
        }
        set_effects_paused(true);
    } else {
        if (music_state_ == 2) {
            music_state_ = 1;
            music_voice_volume(music_ * master_ * focus_);
        }
        set_effects_paused(false);
    }
}

void Manager::set_master_volume(int32_t n) {
    float v = float(uint32_t(n)) * 0.01f;
    if (v == master_) return;
    master_ = v;
    if (music_state_ == 1 || music_state_ == 3) music_voice_volume(music_ * master_ * focus_);
}

void Manager::set_music_volume(int32_t n) {
    float v = float(uint32_t(n)) * 0.01f;
    if (v == music_) return;
    music_ = v;
    music_voice_volume(music_ * master_ * focus_);
}

void Manager::set_sfx_volume(int32_t n) { sfx_ = float(uint32_t(n)) * 0.01f; }

void Manager::update(float dt) {
    if (!enabled_) return;
    if (fading_) {
        if (fade_target_ == 0.0f) {
            fade_level_ -= fade_rate_ * dt;
            if (fade_level_ < 0.0f) {
                fade_level_ = 0.0f;
                fading_ = false;
                if (fade_clear_) clear_all_music();
            }
        } else {
            fade_level_ += fade_rate_ * dt;
            if (fade_level_ > music_) {
                fade_level_ = music_;
                fading_ = false;
            }
        }
        music_voice_volume(master_ * fade_level_ * focus_);
    }
    engine_->update();
}

}  // namespace audio
