#include "day_night.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

void require(bool condition) { if (!condition) throw std::runtime_error("day/night cycle regression"); }
int main() {
    try {
        ambaretto::DayNight time;
        require(std::string(time.clock().data()) == "08:00");
        time.advance(60);
        require(std::string(time.clock().data()) == "09:00");
        time.advance(60, false);
        time.advance(std::numeric_limits<double>::infinity());
        require(std::string(time.clock().data()) == "09:00");
        time.advance(ambaretto::DayNight::cycle_seconds);
        require(std::string(time.clock().data()) == "09:00");
        require(time.set_time("23:59")); time.advance(1);
        require(std::string(time.clock().data()) == "00:00");
        for (const char* bad : {"24:00", "09:60", "6:00", "aa:bb", "12-30", "12:00x"}) require(!time.set_time(bad));
        require(time.set_time("00:00") && time.lighting().night > .99f);
        require(time.set_time("12:00") && time.lighting().day > .99f);
        require(time.set_time("06:00")); const auto dawn = time.lighting();
        require(dawn.horizon.x > dawn.horizon.y && dawn.horizon.z > dawn.horizon.y && dawn.zenith.z > dawn.zenith.y);
        require(time.set_time("18:00")); const auto sunset = time.lighting();
        require(sunset.horizon.x > sunset.horizon.y && sunset.horizon.y > sunset.horizon.z);
        require(time.set_time("00:00"));
        for (int i = 0; i < 24 * 60 * 60; ++i) time.advance(1.0 / 60);
        require(std::string(time.clock().data()) == "00:00");
        std::cout << "24-minute cycle, pause, clock rollover, input and sky palette checks passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
