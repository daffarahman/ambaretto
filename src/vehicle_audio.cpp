#include "vehicle_audio.hpp"
#include "traffic.hpp"
#include "police.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace ambaretto {
namespace {
void write_integer(std::vector<unsigned char>& bytes, unsigned offset, std::uint32_t value, unsigned size) {
    for (unsigned i = 0; i < size; ++i) bytes[offset + i] = static_cast<unsigned char>(value >> (i * 8));
}
std::string sound_path(const char* filename) {
    const std::string relative = std::string("assets/sounds/") + filename;
    const std::string bundled = std::string(GetApplicationDirectory()) + relative;
    return FileExists(bundled.c_str()) ? bundled : relative;
}
VehicleAudioLoop load_loop(const char* filename) {
    const Wave wave = LoadWave(sound_path(filename).c_str());
    auto loop = make_vehicle_audio_loop(wave);
    if (IsWaveValid(wave)) UnloadWave(wave);
    return loop;
}
float approach(float value, float target, float rate, float dt) {
    return value + (target - value) * (1 - std::exp(-rate * dt));
}
float engine_revs(const Car& car) {
    const float speed = car.velocity().Dot(car.forward());
    return std::clamp(std::abs(speed) / (speed < 0 ? 11.0f : std::max(car.tuning().top_speed, 1.0f)), 0.0f, 1.0f);
}
float engine_throttle(const Car& car, float throttle) {
    return throttle * car.velocity().Dot(car.forward()) >= 0 ? std::clamp(std::abs(throttle), 0.0f, 1.0f) : 0;
}
} // namespace

VehicleAudioLoop make_vehicle_audio_loop(Wave wave, float sustain_threshold) {
    VehicleAudioLoop loop;
    if (!IsWaveValid(wave) || wave.channels > 8 || wave.frameCount < 8) return loop;
    float* decoded = LoadWaveSamples(wave);
    if (!decoded) return loop;
    std::vector<float> samples(wave.frameCount);
    for (unsigned i = 0; i < wave.frameCount; ++i) {
        double value = 0;
        for (unsigned channel = 0; channel < wave.channels; ++channel) value += decoded[i * wave.channels + channel];
        samples[i] = std::isfinite(value) ? float(value / wave.channels) : 0;
    }
    UnloadWaveSamples(decoded);
    const unsigned block = std::max(1u, wave.sampleRate / 100);
    std::vector<float> levels((wave.frameCount + block - 1) / block);
    float peak = 0;
    for (unsigned i = 0; i < levels.size(); ++i) {
        const unsigned end = std::min(wave.frameCount, (i + 1) * block);
        double energy = 0;
        for (unsigned j = i * block; j < end; ++j) energy += samples[j] * samples[j];
        levels[i] = float(std::sqrt(energy / (end - i * block)));
        peak = std::max(peak, levels[i]);
    }
    if (peak < .0001f) return loop;
    const float threshold = peak * std::clamp(sustain_threshold, .05f, .90f);
    unsigned first = 0, last = unsigned(levels.size());
    while (first < last && levels[first] < threshold) ++first;
    while (last > first && levels[last - 1] < threshold) --last;
    loop.first_frame = first * block;
    loop.last_frame = std::min(wave.frameCount, last * block);
    const unsigned frames = loop.last_frame - loop.first_frame;
    if (frames < 8) return {};
    loop.crossfade_frames = std::min(wave.sampleRate / 50, frames / 8);
    const unsigned fade = loop.crossfade_frames, output_frames = frames - fade;
    if (output_frames > (std::numeric_limits<unsigned>::max() - 44) / 2) return {};
    loop.wav.resize(44 + output_frames * 2);
    std::memcpy(loop.wav.data(), "RIFF", 4); write_integer(loop.wav, 4, unsigned(loop.wav.size() - 8), 4);
    std::memcpy(loop.wav.data() + 8, "WAVEfmt ", 8); write_integer(loop.wav, 16, 16, 4);
    write_integer(loop.wav, 20, 1, 2); write_integer(loop.wav, 22, 1, 2); // PCM, mono for positional panning.
    write_integer(loop.wav, 24, wave.sampleRate, 4); write_integer(loop.wav, 28, wave.sampleRate * 2, 4);
    write_integer(loop.wav, 32, 2, 2); write_integer(loop.wav, 34, 16, 2);
    std::memcpy(loop.wav.data() + 36, "data", 4); write_integer(loop.wav, 40, output_frames * 2, 4);
    for (unsigned i = 0; i < output_frames; ++i) {
        float value;
        if (i < frames - 2 * fade) value = samples[loop.first_frame + fade + i];
        else {
            const unsigned j = i - (frames - 2 * fade);
            const float t = float(j + 1) / fade;
            value = samples[loop.last_frame - fade + j] * (1 - t) + samples[loop.first_frame + j] * t;
        }
        const auto pcm = static_cast<std::int16_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767));
        write_integer(loop.wav, 44 + i * 2, static_cast<std::uint16_t>(pcm), 2);
    }
    return loop;
}

VehicleAudioPlacement vehicle_audio_placement(Vec3 source, Vec3 listener, Vec3 listener_right, float range) {
    if (!std::isfinite(range) || range <= 0) return {};
    const Vec3 offset = source - listener;
    const float distance = offset.Length();
    if (!std::isfinite(distance) || distance >= range) return {};
    const float volume = 1 - distance / range;
    const float right_length = listener_right.Length();
    const float pan = distance > .001f && right_length > .001f ? offset.Dot(listener_right) / (distance * right_length) : 0;
    return {volume * volume, std::clamp(pan, -1.0f, 1.0f)};
}

VehicleAudio::VehicleAudio() {
    if (!IsAudioDeviceReady()) return;
    auto engine = load_loop("car-engine.wav"), horn = load_loop("car-horn.wav");
    engine_data_ = std::move(engine.wav); horn_data_ = std::move(horn.wav);
    SetAudioStreamBufferSizeDefault(8192);
    siren_ = LoadMusicStream(sound_path("siren.wav").c_str());
    if (IsMusicValid(siren_)) { siren_.looping = true; SetMusicVolume(siren_, 0); }
    const char* files[] = {"9mm.wav", "smg.wav", "ak47.wav", "explosion.wav"};
    for (std::size_t i = 0; i < effects_.size(); ++i) {
        effects_[i] = LoadSound(sound_path(files[i]).c_str());
        if (!IsSoundValid(effects_[i])) { TraceLog(LOG_WARNING, "AUDIO: %s unavailable", files[i]); continue; }
        // ponytail: eight overlapping voices per clip; increase if dense fire cuts off tails.
        for (auto& voice : effect_aliases_[i]) voice = LoadSoundAlias(effects_[i]);
        TraceLog(LOG_INFO, "AUDIO: %s effect ready (%i voices)", files[i], effect_voices);
    }
    if (IsMusicValid(siren_)) TraceLog(LOG_INFO, "AUDIO: Police siren ready (siren.wav)");
    else TraceLog(LOG_WARNING, "AUDIO: Police siren unavailable");
    const auto load = [&](Voice& voice) {
        if (!engine_data_.empty()) voice.engine = LoadMusicStreamFromMemory(".wav", engine_data_.data(), int(engine_data_.size()));
        if (!horn_data_.empty()) voice.horn = LoadMusicStreamFromMemory(".wav", horn_data_.data(), int(horn_data_.size()));
        if (IsMusicValid(voice.engine)) { voice.engine.looping = true; SetMusicVolume(voice.engine, 0); }
        if (IsMusicValid(voice.horn)) { voice.horn.looping = true; SetMusicVolume(voice.horn, 0); }
    };
    load(player_);
    for (auto& voice : npcs_) load(voice);
    if (IsMusicValid(player_.engine)) TraceLog(LOG_INFO, "AUDIO: Car engine loop ready");
    else TraceLog(LOG_WARNING, "AUDIO: Engine sound unavailable; continuing without it");
    if (IsMusicValid(player_.horn)) TraceLog(LOG_INFO, "AUDIO: Horn loop ready (%.2f s sustain, quiet tail removed, %i NPC voices)", GetMusicTimeLength(player_.horn), npc_voices);
    else TraceLog(LOG_WARNING, "AUDIO: Horn sound unavailable; continuing without it");
}

VehicleAudio::~VehicleAudio() {
    for (auto& voices : effect_aliases_) for (auto voice : voices) if (IsSoundValid(voice)) UnloadSoundAlias(voice);
    for (auto sound : effects_) if (IsSoundValid(sound)) UnloadSound(sound);
    if (IsMusicValid(siren_)) UnloadMusicStream(siren_);
    const auto unload = [](Voice& voice) {
        if (IsMusicValid(voice.engine)) UnloadMusicStream(voice.engine);
        if (IsMusicValid(voice.horn)) UnloadMusicStream(voice.horn);
    };
    unload(player_);
    for (auto& voice : npcs_) unload(voice);
}

void VehicleAudio::update_police(const Police& police, Vec3 listener, Vec3 listener_right, bool running) {
    if (!IsMusicValid(siren_)) return;
    const Car* nearest = nullptr; float distance = 200 * 200;
    if (running && police.wanted().stars()) for (const auto& unit : police.units()) if (unit.active && !unit.claimed && !unit.car->destroyed()) {
        const float candidate = (unit.car->position() - listener).LengthSq();
        if (candidate < distance) { distance = candidate; nearest = unit.car.get(); }
    }
    if (!nearest) { StopMusicStream(siren_); return; }
    const auto placement = vehicle_audio_placement(nearest->position(), listener, listener_right, 200);
    SetMusicVolume(siren_, placement.volume * .65f); SetMusicPan(siren_, placement.pan);
    if (!IsMusicStreamPlaying(siren_)) PlayMusicStream(siren_);
    UpdateMusicStream(siren_);
}
void VehicleAudio::update_effects(PhysicsWorld& world, Vec3 listener, Vec3 listener_right, bool running) {
    const auto events = world.take_sound_events();
    if (!running) {
        for (auto& voices : effect_aliases_) for (auto voice : voices) if (IsSoundValid(voice)) StopSound(voice);
        return;
    }
    if (!IsAudioDeviceReady()) return;
    for (const auto& event : events) {
        const int kind = int(event.effect);
        if (kind < 0 || kind >= int(effects_.size())) continue;
        const bool explosion = event.effect == SoundEffect::Explosion;
        const auto placement = vehicle_audio_placement(event.position, listener, listener_right, explosion ? 200 : 120);
        if (placement.volume <= 0) continue;
        auto& voice = effect_aliases_[kind][next_effect_voice_[kind]++ % effect_voices];
        if (!IsSoundValid(voice)) continue;
        StopSound(voice);
        SetSoundVolume(voice, placement.volume * (explosion ? .9f : .6f));
        SetSoundPan(voice, placement.pan);
        PlaySound(voice);
    }
}
void VehicleAudio::clear_voice(Voice& voice) {
    if (IsMusicValid(voice.engine)) StopMusicStream(voice.engine);
    if (IsMusicValid(voice.horn)) StopMusicStream(voice.horn);
    voice.vehicle = nullptr; voice.engine_gain = voice.horn_gain = voice.pan = 0;
    voice.pitch = .7f; voice.engine_paused = false;
}

void VehicleAudio::update_voice(Voice& voice, float engine_volume, float horn_volume, float pitch, float pan, float dt) {
    voice.engine_gain = approach(voice.engine_gain, engine_volume, 12, dt);
    voice.horn_gain = approach(voice.horn_gain, horn_volume, 80, dt);
    voice.pitch = approach(voice.pitch, pitch, 8, dt); voice.pan = approach(voice.pan, pan, 12, dt);
    if (IsMusicValid(voice.engine)) {
        SetMusicVolume(voice.engine, voice.engine_gain); SetMusicPitch(voice.engine, voice.pitch); SetMusicPan(voice.engine, voice.pan);
        if (engine_volume > 0 && !IsMusicStreamPlaying(voice.engine)) {
            if (voice.engine_paused) ResumeMusicStream(voice.engine); else PlayMusicStream(voice.engine);
            voice.engine_paused = false;
        } else if (engine_volume == 0 && voice.engine_gain < .001f && IsMusicStreamPlaying(voice.engine)) {
            PauseMusicStream(voice.engine); voice.engine_paused = true;
        }
        UpdateMusicStream(voice.engine);
    }
    if (IsMusicValid(voice.horn)) {
        SetMusicVolume(voice.horn, voice.horn_gain); SetMusicPan(voice.horn, voice.pan);
        if (horn_volume > 0 && !IsMusicStreamPlaying(voice.horn)) PlayMusicStream(voice.horn);
        else if (horn_volume == 0 && voice.horn_gain < .001f && IsMusicStreamPlaying(voice.horn)) StopMusicStream(voice.horn);
        UpdateMusicStream(voice.horn);
    }
}

void VehicleAudio::update(const Traffic& traffic, const Car& player_car, Vec3 listener, Vec3 listener_right,
                          bool player_driving, bool horn_held, bool running, float dt, float player_throttle) {
    if (!IsAudioDeviceReady()) return;
    dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, .1f) : 0;
    selection_time_ -= dt;
    if (selection_time_ <= 0 && running) {
        selection_time_ = .1f;
        std::array<float, npc_voices> distances; distances.fill(std::numeric_limits<float>::infinity());
        selected_.fill(nullptr);
        // ponytail: six nearby vehicles at 10 Hz; raise the cap only if dense traffic audibly loses voices.
        for (const auto& vehicle : traffic.cars()) {
            if (!vehicle.npc || !vehicle.car->simulated() || vehicle.car->destroyed() || vehicle.car.get() == &player_car) continue;
            const float distance = (vehicle.car->position() - listener).LengthSq();
            const float range = vehicle.horn_time > 0 ? 120.0f : 70.0f;
            if (distance >= range * range) continue;
            float priority = distance;
            for (const auto& voice : npcs_) if (voice.vehicle == &vehicle) { priority *= .85f; break; }
            for (int i = 0; i < npc_voices; ++i) if (priority < distances[i]) {
                for (int j = npc_voices - 1; j > i; --j) { distances[j] = distances[j - 1]; selected_[j] = selected_[j - 1]; }
                distances[i] = priority; selected_[i] = &vehicle; break;
            }
        }
    }
    const float revs = engine_revs(player_car), throttle = engine_throttle(player_car, player_throttle);
    const auto placement = vehicle_audio_placement(player_car.position(), listener, listener_right, 70);
    update_voice(player_, running && player_driving && !player_car.destroyed() ? placement.volume * (.22f + .18f * revs + .25f * throttle) : 0,
                 running && player_driving && !player_car.destroyed() && horn_held ? placement.volume * .70f : 0,
                 .7f + 1.5f * revs + .2f * throttle, placement.pan, dt);
    for (auto& voice : npcs_) {
        if (voice.vehicle && voice.vehicle->car.get() == &player_car) clear_voice(voice); // Stealing must not duplicate the player's engine.
        if (!voice.vehicle) for (const auto* candidate : selected_) {
            if (!candidate || !candidate->npc || candidate->car->destroyed() || candidate->car.get() == &player_car) continue;
            bool used = false;
            for (const auto& other : npcs_) if (other.vehicle == candidate) { used = true; break; }
            if (!used) { voice.vehicle = candidate; break; }
        }
        bool selected = voice.vehicle && voice.vehicle->npc && voice.vehicle->car->simulated() && !voice.vehicle->car->destroyed();
        if (selected) selected = std::find(selected_.begin(), selected_.end(), voice.vehicle) != selected_.end();
        float engine_volume = 0, horn_volume = 0, pitch = voice.pitch, pan = voice.pan;
        if (selected && running) {
            const auto& car = *voice.vehicle->car;
            const float npc_revs = engine_revs(car), npc_throttle = engine_throttle(car, voice.vehicle->input.throttle);
            const auto engine_position = vehicle_audio_placement(car.position(), listener, listener_right, 70);
            const auto horn_position = vehicle_audio_placement(car.position(), listener, listener_right, 120);
            engine_volume = engine_position.volume * (.13f + .12f * npc_revs + .10f * npc_throttle);
            horn_volume = voice.vehicle->horn_time > 0 ? horn_position.volume * .45f : 0;
            pitch = .7f + 1.5f * npc_revs + .2f * npc_throttle; pan = horn_position.pan;
        }
        update_voice(voice, engine_volume, horn_volume, pitch, pan, dt);
        if (!selected && voice.engine_gain < .001f && voice.horn_gain < .001f && voice.vehicle) clear_voice(voice);
    }
}
} // namespace ambaretto
