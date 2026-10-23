#include "interior_ui.hpp"
#include "city.hpp"
#include "building_renderer.hpp"
#include "character_renderer.hpp"
#include "controller_mapping.hpp"
#include "third_person_camera.hpp"
#include "player.hpp"
#include "ui_font.hpp"
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ambaretto {
namespace {
Vector3 vector(Vec3 p) { return {p.GetX(),p.GetY(),p.GetZ()}; }
void begin_markers() {
    rlDrawRenderBatchActive(); rlDisableDepthMask(); rlDisableBackfaceCulling();
    rlSetTexture(0); rlBegin(RL_TRIANGLES);
}
void end_markers() {
    rlEnd(); rlDrawRenderBatchActive(); rlEnableBackfaceCulling(); rlEnableDepthMask();
}
void marker_vertices(Vec3 position) {
    const float bob=.08f*std::sin(float(GetTime())*2.5f);
    const Vec3 tip=position+Vec3(0,.12f+bob,0);
    const Vec3 corners[]={Vec3(-.48f,2.8f,-.48f),Vec3(.48f,2.8f,-.48f),Vec3(.48f,2.8f,.48f),Vec3(-.48f,2.8f,.48f)};
    for (int i=0;i<4;++i) {
        rlColor4ub(255,220,20,235); rlVertex3f(tip.GetX(),tip.GetY(),tip.GetZ());
        for (int j : {i,(i+1)%4}) {
            const auto p=position+corners[j]+Vec3(0,bob,0);
            rlColor4ub(255,205,0,35); rlVertex3f(p.GetX(),p.GetY(),p.GetZ());
        }
    }
}
}
void draw_entrance_marker(Vec3 position) { begin_markers(); marker_vertices(position); end_markers(); }
void draw_city_entrances(const City& city, Vec3 focus, float distance) {
    bool drawing=false;
    for (const auto& b:city.buildings) if (building_entrance(b)) {
        const auto p=entrance_position(city,b);
        if ((p-focus).LengthSq()<distance*distance) {
            if (!drawing) {begin_markers();drawing=true;}
            marker_vertices(p);
        }
    }
    if (drawing) end_markers();
}
void fade_interior_screen() {
    auto screen=LoadImageFromScreen(); const auto texture=LoadTextureFromImage(screen); UnloadImage(screen);
    if (!texture.id) return;
    for (float t=0;t<.25f;) {
        if (WindowShouldClose()) break;
        t+=std::clamp(GetFrameTime(),.001f,.05f);
        BeginDrawing(); ClearBackground(BLACK);
        DrawTexturePro(texture,{0,0,float(texture.width),float(texture.height)},
            {0,0,float(GetScreenWidth()),float(GetScreenHeight())},{0,0},0,WHITE);
        const float alpha=std::clamp(t/.25f,0.f,1.f);
        DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),Fade(BLACK,alpha));
        EndDrawing();
    }
    UnloadTexture(texture);
}
void visit_interior(const Interior& room, const Character& outside, ControllerMapping& controls, const GraphicsSettings& graphics,
                    const std::string& screenshot) {
    if (screenshot.empty()) fade_interior_screen();
    std::string error;
    if (!room.validate(error)) throw std::runtime_error(error);
    SetWindowTitle(("Ambaretto - "+room.name).c_str());
    PhysicsWorld world(false);
    BuildingRenderer renderer;
    {
        const auto triangles=room.triangles(); std::vector<std::array<Vec3,3>> collision;
        for (const auto& t:triangles) if (!t.decal) collision.push_back(t.points);
        world.add_static_mesh(collision);
        if (!renderer.build(triangles,error,false)) throw std::runtime_error(error);
    }
    Character player(world,nullptr,true);
    if (outside.design()) player.set_design(*outside.design());
    player.reset(room.spawn());
    CharacterRenderer characters;
    SceneLighting lighting;
    Daylight light=DayNight{}.lighting(); light.ambient={.8f,.76f,.65f}; light.sun_color={.18f,.16f,.12f}; light.night=0;
    auto settings=graphics; settings.shadows=0;
    ThirdPersonCamera orbit; orbit.reset();
    Camera3D camera{{0,2,3},{0,1,0},{0,1,0},60,CAMERA_PERSPECTIVE};
    double accumulator=0;
    bool first=true, exit_armed=false, captured=screenshot.empty(), jump=false;
    float fade=1; int frames=0;
    if (captured) DisableCursor();
    while (!WindowShouldClose()) {
        const float dt=std::clamp(GetFrameTime(),0.f,.1f);
        const bool focused=IsWindowFocused();
        if (screenshot.empty()) controls.update(read_controllers());
        const bool near_exit=near_interior_marker(player.position(),room.exit());
        if (!near_exit) exit_armed=true;
        if (!first && screenshot.empty() && focused && (IsKeyPressed(KEY_ESCAPE)
            || (near_exit && controls.pressed(Action::EnterVehicle)) || (near_exit && exit_armed))) break;
        if (!focused && captured) { EnableCursor(); captured=false; accumulator=0; }
        if (!first && focused && !captured && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { captured=true; DisableCursor(); first=true; }
        FootInput walking;
        if (focused && captured && !first) {
            const auto mouse=GetMouseDelta();
            orbit.look(mouse.x+(controls.value(Action::FootLookRight)-controls.value(Action::FootLookLeft))*700*dt,
                mouse.y+(controls.value(Action::FootLookDown)-controls.value(Action::FootLookUp))*700*dt,
                GetMouseWheelMove(),false,player.forward(),player.velocity().Length(),dt);
            walking.direction=orbit.move_direction(controls.value(Action::FootForward)-controls.value(Action::FootBackward),
                controls.value(Action::FootRight)-controls.value(Action::FootLeft));
            walking.sprint=controls.value(Action::Sprint)>.5f;
            jump|=controls.pressed(Action::Jump);
            accumulator+=dt;
        }
        while (accumulator>=fixed_step) {
            walking.jump=jump; world.step(); player.step(walking); jump=false; accumulator-=fixed_step;
        }
        if (player.position().GetY()<-2) player.reset(room.spawn());
        const auto target=player.position()+Vec3(0,1.35f,0), offset=orbit.desired_position(target,false)-target;
        camera.target=vector(target); camera.position=vector(target+offset*world.camera_fraction(target,offset));
        characters.set_lighting(lighting,camera,light,settings);
        BeginDrawing(); ClearBackground({26,24,20,255}); BeginMode3D(camera);
        renderer.draw(camera,light,lighting,settings); characters.draw(player,camera);
        // A marker crossing the camera's near plane would cover the entire room.
        if ((room.exit()-Vec3(camera.position.x,camera.position.y,camera.position.z)).LengthSq()>9) draw_entrance_marker(room.exit());
        EndMode3D();
        const char* hint=near_exit ? "Walk into marker to leave / E: exit" : "Walk: normal controls / Esc: leave";
        const float panel_width=std::min(float(GetScreenWidth()-32),float(std::max(ui::measure_text(room.name.c_str(),22),ui::measure_text(hint,16))+24));
        ui::draw_panel({16,16,panel_width,78});
        ui::draw_text(room.name.c_str(),28,26,22,ui::ink);
        ui::draw_text(hint,28,60,16,ui::ink);
        if (!captured) ui::draw_text("Click to resume",GetScreenWidth()/2-90,GetScreenHeight()/2,22,WHITE);
        if (fade>0 && screenshot.empty()) DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),Fade(BLACK,fade));
        EndDrawing(); first=false;
        fade=std::max(0.f,fade-dt/.25f);
        if (!screenshot.empty() && ++frames>=3) {
            auto screen=LoadImageFromScreen(); ExportImage(screen,screenshot.c_str()); UnloadImage(screen); break;
        }
    }
    if (screenshot.empty()) fade_interior_screen();
}
} // namespace ambaretto
