#pragma once
#include "vehicle.hpp"
#include <raylib.h>
#include <array>
#include <vector>

namespace ambaretto {
class Traffic;
class Police;
struct TrafficCar;
struct VehicleAudioLoop {
    std::vector<unsigned char> wav;
    unsigned first_frame = 0, last_frame = 0, crossfade_frames = 0;
};
VehicleAudioLoop make_vehicle_audio_loop(Wave wave, float sustain_threshold = .60f);
struct VehicleAudioPlacement { float volume = 0, pan = 0; };
VehicleAudioPlacement vehicle_audio_placement(Vec3 source, Vec3 listener, Vec3 listener_right, float range);

class VehicleAudio {
public:
    static constexpr int npc_voices = 6;
    VehicleAudio();
    ~VehicleAudio();
    VehicleAudio(const VehicleAudio&) = delete;
    VehicleAudio& operator=(const VehicleAudio&) = delete;
    void update(const Traffic& traffic, const Car& player_car, Vec3 listener, Vec3 listener_right,
                bool player_driving, bool horn_held, bool running, float dt, float player_throttle = 0);
    void update_police(const Police& police, Vec3 listener, Vec3 listener_right, bool running);
    void update_effects(PhysicsWorld& world, Vec3 listener, Vec3 listener_right, bool running);
private:
    struct Voice {
        const TrafficCar* vehicle = nullptr;
        Music engine{}, horn{};
        float engine_gain = 0, horn_gain = 0, pitch = .7f, pan = 0;
        bool engine_paused = false;
    };
    void update_voice(Voice& voice, float engine_volume, float horn_volume, float pitch, float pan, float dt);
    void clear_voice(Voice& voice);
    std::vector<unsigned char> engine_data_, horn_data_;
    Music siren_{};
    static constexpr int effect_voices = 8;
    std::array<Sound, int(SoundEffect::Count)> effects_{};
    // Aliases share sample data and allow automatic fire and nearby blasts to overlap.
    std::array<std::array<Sound, effect_voices>, int(SoundEffect::Count)> effect_aliases_{};
    std::array<unsigned, int(SoundEffect::Count)> next_effect_voice_{};
    Voice player_;
    std::array<Voice, npc_voices> npcs_{};
    std::array<const TrafficCar*, npc_voices> selected_{};
    float selection_time_ = 0;
};
} // namespace ambaretto
