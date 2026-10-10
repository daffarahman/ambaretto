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
        require(time.minutes_of_day() == 480);
        time.advance(.5);
        require(time.minutes_of_day() == 480.5f);
        time.advance(ambaretto::DayNight::cycle_seconds - .5);
        time.advance(60);
        require(std::string(time.clock().data()) == "09:00");
        time.advance(60, false);
        time.advance(std::numeric_limits<double>::infinity());
        require(std::string(time.clock().data()) == "09:00");
        time.advance(ambaretto::DayNight::cycle_seconds);
        require(std::string(time.clock().data()) == "09:00");
        require(time.set_time("23:59")); time.advance(1);
        require(std::string(time.clock().data()) == "00:00");
        require(time.minutes_of_day() == 0);
        for (const char* bad : {"24:00", "09:60", "6:00", "aa:bb", "12-30", "12:00x"}) require(!time.set_time(bad));
        require(time.set_time("00:00") && time.lighting().night > .99f);
        const auto midnight = time.lighting();
        require(midnight.horizon.z > midnight.horizon.y && midnight.horizon.y > midnight.horizon.x);
        require(midnight.ambient.z > midnight.ambient.y && midnight.ambient.y > midnight.ambient.x);
        require(time.set_time("12:00") && time.lighting().day > .99f);
        const auto noon = time.lighting();
        require(noon.horizon.z > noon.horizon.y && noon.horizon.y > noon.horizon.x);
        require(noon.zenith.z > noon.zenith.y && noon.zenith.y > noon.zenith.x);
        require(noon.ambient.y > noon.ambient.x && noon.ambient.y > noon.ambient.z);
        require(noon.sun_color.y > .95f && noon.sun_color.z > .9f);
        require(time.set_time("06:00")); const auto dawn = time.lighting();
        require(dawn.horizon.x > dawn.horizon.y && dawn.horizon.y > dawn.horizon.z);
        require(dawn.zenith.z > dawn.zenith.y && dawn.zenith.y > dawn.zenith.x);
        require(dawn.sun_color.y >= .79f && dawn.sun_color.z >= .54f);
        require(time.set_time("18:00")); const auto sunset = time.lighting();
        require(sunset.horizon.x > sunset.horizon.y && sunset.horizon.y > sunset.horizon.z);
        require(sunset.zenith.z > sunset.zenith.y && sunset.zenith.y > sunset.zenith.x);
        for (const char* hour : {"05:00", "07:00", "09:00", "16:00", "19:00", "20:00", "22:00"}) {
            require(time.set_time(hour)); const auto light = time.lighting();
            require(light.horizon.z > light.horizon.y && light.horizon.y > light.horizon.x);
            require(light.zenith.z > light.zenith.y && light.zenith.y > light.zenith.x);
        }
        require(time.set_time("00:00"));
        for (int i = 0; i < 24 * 60 * 60; ++i) time.advance(1.0 / 60);
        require(std::string(time.clock().data()) == "00:00");
        std::cout << "24-minute cycle, pause, clock rollover, input and sky palette checks passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
