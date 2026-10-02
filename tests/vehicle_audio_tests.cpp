#include "vehicle_audio.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#ifndef VEHICLE_AUDIO_ASSETS
#define VEHICLE_AUDIO_ASSETS "assets/sounds/"
#endif

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
void check_loop(const char* filename, bool horn, bool playback) {
    const Wave original = LoadWave((std::string(VEHICLE_AUDIO_ASSETS) + filename).c_str());
    require(IsWaveValid(original), "The supplied vehicle WAV must decode");
    const auto loop = forza::make_vehicle_audio_loop(original);
    require(!loop.wav.empty(), "A loud vehicle WAV must produce a loop");
    const Wave decoded = LoadWaveFromMemory(".wav", loop.wav.data(), int(loop.wav.size()));
    require(IsWaveValid(decoded) && decoded.channels == 1 && decoded.sampleSize == 16, "The in-memory loop must be native mono PCM WAV");
    require(decoded.frameCount == loop.last_frame - loop.first_frame - loop.crossfade_frames, "The loop length must match its active audio region");
    require(loop.crossfade_frames == original.sampleRate / 50, "The sustain loop must overlap its seam by 20 ms");
    const float seconds = float(decoded.frameCount) / decoded.sampleRate;
    if (horn) require(seconds > 1.5f && seconds < 1.9f && loop.last_frame < original.frameCount * .8f, "The horn's long quiet tail must be removed");
    float* samples = LoadWaveSamples(decoded);
    float* source = LoadWaveSamples(original);
    require(samples && source, "The PCM loop must expose its sample data");
    float maximum_step = 0, minimum_rms = 1;
    for (unsigned i = 1; i < decoded.frameCount; ++i) maximum_step = std::max(maximum_step, std::abs(samples[i] - samples[i - 1]));
    const float seam = std::abs(samples[0] - samples[decoded.frameCount - 1]);
    require(seam <= maximum_step + .0001f, "The loop seam must not introduce an extra sample discontinuity");
    const auto mono = [&](unsigned frame) {
        float sample = 0;
        for (unsigned channel = 0; channel < original.channels; ++channel) sample += source[frame * original.channels + channel];
        return sample / original.channels;
    };
    require(std::abs(samples[0] - mono(loop.first_frame + loop.crossfade_frames)) < .0001f &&
            std::abs(samples[decoded.frameCount - 1] - mono(loop.first_frame + loop.crossfade_frames - 1)) < .0001f,
            "The circular crossfade must join adjacent samples of the original recording");
    const unsigned block = decoded.sampleRate / 100;
    for (unsigned i = 0; i + block <= decoded.frameCount; i += block) {
        double energy = 0;
        for (unsigned j = 0; j < block; ++j) energy += samples[i + j] * samples[i + j];
        minimum_rms = std::min(minimum_rms, float(std::sqrt(energy / block)));
    }
    require(minimum_rms > .025f, "The sustain loop must not retain a quiet gap");
    std::printf("%s: %.3f s loop, minimum 10 ms RMS %.3f, seam step %.4f\n", filename, seconds, minimum_rms, seam);
    UnloadWaveSamples(samples); UnloadWaveSamples(source); UnloadWave(decoded); UnloadWave(original);
    if (playback && horn) {
        InitAudioDevice(); require(IsAudioDeviceReady(), "A working audio device is required for the optional playback check");
        Music music = LoadMusicStreamFromMemory(".wav", loop.wav.data(), int(loop.wav.size()));
        require(IsMusicValid(music), "The prepared horn must load as a native stream");
        music.looping = true; SetMusicVolume(music, 0); PlayMusicStream(music);
        for (float elapsed = 0; elapsed < seconds * 2.5f; elapsed += .01f) {
            UpdateMusicStream(music); require(IsMusicStreamPlaying(music), "Holding the horn must keep its stream playing across multiple loops");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        StopMusicStream(music); require(!IsMusicStreamPlaying(music), "Releasing a horn stream must stop it");
        UnloadMusicStream(music); CloseAudioDevice();
        std::puts("Native held-horn streaming passed across 2.5 loops");
    }
}
}

int main(int argc, char** argv) {
    SetTraceLogLevel(LOG_WARNING);
    const bool playback = argc > 1 && std::string(argv[1]) == "--playback";
    check_loop("car-horn.wav", true, playback); check_loop("car-engine.wav", false, false);
    const auto origin = forza::Vec3::sZero(), right = forza::Vec3::sAxisX();
    const auto near = forza::vehicle_audio_placement(origin, origin, right, 100);
    require(near.volume == 1 && near.pan == 0, "A car beside the listener must be centered and audible");
    const auto left = forza::vehicle_audio_placement(forza::Vec3(-50, 0, 0), origin, right, 100);
    const auto right_car = forza::vehicle_audio_placement(forza::Vec3(50, 0, 0), origin, right, 100);
    require(std::abs(left.volume - .25f) < .0001f && left.pan == -1 && right_car.pan == 1, "Distance falloff and left/right panning must match the listener's orientation");
    require(forza::vehicle_audio_placement(forza::Vec3(50, 0, 0), origin, -right, 100).pan == -1, "Turning around must reverse the stereo placement");
    require(forza::vehicle_audio_placement(forza::Vec3(100, 0, 0), origin, right, 100).volume == 0, "Distant vehicles must be inaudible");
    require(forza::make_vehicle_audio_loop({}).wav.empty(), "Missing or invalid audio must fail safely");
    short silence[100]{};
    require(forza::make_vehicle_audio_loop(Wave{100, 1000, 16, 1, silence}).wav.empty(), "A silent recording must not become a playing voice");
    std::puts("Vehicle loop trimming, seams, and positional audio checks passed");
}
