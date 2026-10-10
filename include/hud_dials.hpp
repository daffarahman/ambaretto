#pragma once
#include <raylib.h>

namespace ambaretto {
void draw_speedometer(Vector2 center, float kilometers_per_hour);
void draw_clock(Vector2 center, float minutes_of_day);
}
