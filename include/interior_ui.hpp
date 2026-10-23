#pragma once
#include "interior.hpp"

namespace ambaretto {
class Character;
class ControllerMapping;
struct GraphicsSettings;
void draw_entrance_marker(Vec3 position);
void draw_city_entrances(const City& city, Vec3 focus, float distance);
// The exterior stays paused; only this room has an active renderer and simulation.
void visit_interior(const Interior& room, const Character& outside, ControllerMapping& controls, const GraphicsSettings& graphics,
                    const std::string& screenshot = {});
} // namespace ambaretto
