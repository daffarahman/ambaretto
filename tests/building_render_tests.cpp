#include "building_renderer.hpp"
#include <rlgl.h>
#include <raymath.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace {
using namespace ambaretto;
void require(bool ok,const std::string& message) { if (!ok) throw std::runtime_error(message); }
Vector3 vector(Vec3 p) { return {p.GetX(),p.GetY(),p.GetZ()}; }
Color pixel(Image image,Camera3D camera,Vec3 point) {
    const auto p = GetWorldToScreenEx(vector(point),camera,image.width,image.height);
    require(p.x>=0 && p.y>=0 && p.x<image.width && p.y<image.height,"Sample left the test viewport");
    return GetImageColor(image,int(p.x),int(p.y));
}
int brightness(Color color) { return int(color.r)+color.g+color.b; }
bool checker(Color color) { return color.r>color.b*2 || color.b>color.r*2; }
bool red(Color color) { return color.r>color.b*2; }
Image render(BuildingRenderer& renderer,RenderTexture2D target,Camera3D camera,
             const Daylight& daylight,const SceneLighting& lighting,const GraphicsSettings& settings) {
    BeginTextureMode(target); ClearBackground({18,28,45,255}); BeginMode3D(camera);
    renderer.draw(camera,daylight,lighting,settings);
    EndMode3D(); EndTextureMode();
    Image image = LoadImageFromTexture(target.texture); ImageFlipVertical(&image); return image;
}
std::vector<BuildingTriangle> quad(const std::string& texture,bool decal,float y,float half) {
    const std::array<Vec3,4> p{{Vec3(-half,y,-half),Vec3(-half,y,half),Vec3(half,y,half),Vec3(half,y,-half)}};
    const std::array<BuildingUV,4> uv{{{0,0},{0,1},{1,1},{1,0}}};
    return {{{p[0],p[1],p[2]},{uv[0],uv[1],uv[2]},texture,decal,0},
            {{p[0],p[2],p[3]},{uv[0],uv[2],uv[3]},texture,decal,0}};
}
void cutout_shadows(RenderTexture2D target,const std::filesystem::path& output) {
    std::string error; BuildingRenderer caster,receiver;
    require(caster.build(quad("window.png",true,4,2),error,false),error);
    require(receiver.build(quad("",false,0,5),error,false),error);
    GraphicsSettings settings; settings.apply_preset(GraphicsPreset::Low); settings.shadows = 1; settings.soft_shadows = false;
    Daylight daylight = DayNight().lighting(); daylight.day = 1; daylight.sun_direction = {0,1,0};
    daylight.ambient = {.15f,.15f,.15f}; daylight.sun_color = {1,1,1};
    SceneLighting lighting;
    require(lighting.begin_shadow({0,0,0},daylight,settings),"Shadow framebuffer could not be created");
    caster.draw_shadow({0,0,0},90); lighting.end_shadow();
    require(caster.stats().shadow_draws==1,"Cutout shadow caster did not use one array mesh");
    const Camera3D camera{{0,20,0},{0,0,0},{0,0,-1},12,CAMERA_ORTHOGRAPHIC};
    Image shadowed = render(receiver,target,camera,daylight,lighting,settings);
    const auto centre = pixel(shadowed,camera,Vec3(0,0,0)), edge = pixel(shadowed,camera,Vec3(1.7f,0,0));
    require(brightness(edge)>brightness(centre)+100,"Transparent decal borders cast an opaque shadow or opaque texels lost their shadow");
    require(ExportImage(shadowed,(output/"building-editor-cutout-shadow.png").string().c_str()),"Shadow preview export failed");
    UnloadImage(shadowed);
}
void building_preview_and_benchmark(RenderTexture2D target,const std::filesystem::path& output) {
    std::string error; BuildingMesh sample; sample.name = "Three part benchmark";
    require(sample.set_footprint(2,2,error,true),error);
    require(sample.add_shape(BuildingShape::Box,Vec3(4,12,11.2f),Vec3(-7.6f,0,0),error)>=0,error);
    require(sample.add_shape(BuildingShape::Box,Vec3(4,12,11.2f),Vec3(7.6f,0,0),error)>=0,error);
    std::vector<int> faces(sample.faces.size()); std::iota(faces.begin(),faces.end(),0);
    const int wall = sample.add_material("wall.png",error), roof = sample.add_material("roof.png",error),
        base = sample.add_material("base.png",error), trim = sample.add_material("trim.png",error);
    require(wall>=0 && roof>=0 && base>=0 && trim>=0,error);
    require(sample.set_face_material(faces,wall,error) && sample.set_face_material({4,10,16},roof,error)
        && sample.set_face_material({6},base,error) && sample.set_face_material({12},trim,error),error);
    require(sample.set_uv(faces,BuildingProjection::Planar,2,{0,0},{1,1},0,error),error);
    auto baked = sample.baked();
    require(baked.faces.size()==14,"Bake retained the two internal box joins");
    BuildingRenderer preview; require(preview.build(baked.triangles(),error,false),error);
    const auto initial_textures = preview.stats().gpu_texture_bytes;
    require(preview.stats().meshes==1 && preview.stats().array_pages==1,"Multi-texture preview was split by material");
    GraphicsSettings settings; settings.apply_preset(GraphicsPreset::Low);
    Daylight daylight = DayNight().lighting(); daylight.ambient = {1,1,1}; daylight.sun_color = {0,0,0}; daylight.day = 1;
    SceneLighting lighting;
    const Camera3D wall_camera{{0,6,-40},{0,6,0},{0,1,0},15,CAMERA_ORTHOGRAPHIC};
    Image before = render(preview,target,wall_camera,daylight,lighting,settings);
    const auto a = pixel(before,wall_camera,Vec3(-4.9f,.8f,-5.6f)),
        b = pixel(before,wall_camera,Vec3(-3.9f,.8f,-5.6f)), c = pixel(before,wall_camera,Vec3(-2.9f,.8f,-5.6f));
    require(checker(a) && checker(b) && checker(c) && red(a)==red(c) && red(a)!=red(b),
        "Two metre world-space repeats stretched across the wall");
    const auto left = pixel(before,wall_camera,Vec3(-7.6f,6,-5.6f)), right = pixel(before,wall_camera,Vec3(7.6f,6,-5.6f));
    require(left.r>left.g*2 && right.r>right.b*2 && right.g>right.b*2,"Face material layers did not render on the composed parts");
    require(ExportImage(before,(output/"building-editor-tiling.png").string().c_str()),"Tiling preview export failed");
    UnloadImage(before);
    for (float y : {2.5f,5.f,7.5f,10.f}) {
        const int decal = sample.place_decal(0,Vec3(-4, y,-5.6f),"window.png",1,1.5f,error);
        require(decal>=0 && sample.repeat_decal(decal,6,1.6f,false,error),error);
    }
    require(sample.decals.size()==24,"Window arrays did not produce four rows of six");
    baked = sample.baked(); const auto triangles = baked.triangles();
    require(preview.build(triangles,error,false),error);
    require(preview.stats().gpu_texture_bytes>initial_textures && preview.stats().meshes==1 && preview.stats().array_pages==1,
        "Repeated windows duplicated texture pages or split the building mesh");
    Image windows = render(preview,target,wall_camera,daylight,lighting,settings);
    const auto centre = pixel(windows,wall_camera,Vec3(-4,2.5f,-5.6f)), border = pixel(windows,wall_camera,Vec3(-3.55f,2.5f,-5.6f));
    require(centre.g>centre.r*2 && centre.g>centre.b*2 && checker(border),"Window placement or transparent borders hid the wall");
    UnloadImage(windows);
    const Camera3D shape_camera{{-28,23,-35},{0,6,0},{0,1,0},24,CAMERA_ORTHOGRAPHIC};
    Image shapes = render(preview,target,shape_camera,daylight,lighting,settings);
    require(ExportImage(shapes,(output/"building-editor-shapes.png").string().c_str()),"Composed preview export failed"); UnloadImage(shapes);
    require(preview.stats().color_draws==1,"Four materials and twenty-four decals require more than one editor draw");
    const auto one = preview.stats();
    std::vector<BuildingTriangle> copies; copies.reserve(triangles.size()*100);
    for (int z = 0; z<10; ++z) for (int x = 0; x<10; ++x) for (auto triangle : triangles) {
        for (auto& point : triangle.points) point += Vec3(16+x*24,0,16+z*24);
        copies.push_back(std::move(triangle));
    }
    BuildingRenderer renderer; require(renderer.build(copies,error),error);
    const auto stats = renderer.stats();
    require(stats.gpu_texture_bytes==one.gpu_texture_bytes,"A hundred building copies duplicated texture storage");
    require(stats.emitted_triangles==one.emitted_triangles*100 && stats.gpu_geometry_bytes==one.gpu_geometry_bytes*100,
        "Baked copies changed geometry payload unexpectedly");
    require(stats.gpu_geometry_bytes<=BuildingRenderer::geometry_budget && stats.gpu_texture_bytes<=BuildingRenderer::texture_budget,
        "Benchmark exceeded the eight MiB geometry or texture budget");
    require(stats.array_pages==1 && stats.meshes<=4,"Benchmark used more than four spatial meshes for one texture page");
    const Camera3D camera{{360,250,-100},{128,6,128},{0,1,0},310,CAMERA_ORTHOGRAPHIC};
    using Finish = void (*)();
    const auto finish = reinterpret_cast<Finish>(rlGetProcAddress("glFinish"));
    require(finish!=nullptr,"Synchronized benchmark needs glFinish");
    std::vector<double> frame_ms;
    for (int frame = 0; frame<65; ++frame) {
        const auto start = std::chrono::steady_clock::now();
        BeginTextureMode(target); ClearBackground({18,28,45,255}); BeginMode3D(camera);
        renderer.draw(camera,daylight,lighting,settings); EndMode3D(); EndTextureMode(); finish();
        if (frame>=5) frame_ms.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    }
    require(renderer.stats().color_draws==stats.meshes,"Color draw count no longer matches visible chunk meshes");
    Image overview = LoadImageFromTexture(target.texture); ImageFlipVertical(&overview);
    require(ExportImage(overview,(output/"building-editor-100-copies.png").string().c_str()),"Benchmark preview export failed"); UnloadImage(overview);
    std::sort(frame_ms.begin(),frame_ms.end());
    std::cout << "Sample: 3 parts, 4 surface textures, 24 decals, 100 copies; triangles=" << stats.emitted_triangles
        << ", vertices=" << stats.vertices << ", color_draws=" << renderer.stats().color_draws << ", pages=" << stats.array_pages
        << ", GPU_geometry_bytes=" << stats.gpu_geometry_bytes << ", CPU_geometry_bytes=" << stats.cpu_geometry_bytes
        << ", GPU_texture_bytes=" << stats.gpu_texture_bytes << ", bake_ms=" << stats.bake_ms
        << ", synchronized_720p_median_ms=" << frame_ms[frame_ms.size()/2] << ", p95_ms=" << frame_ms[frame_ms.size()*95/100]
        << "\nResource bytes are tracked logical payload; frame timings describe this host, not the target GPU's VRAM residency or FPS.\n";
    Image large = GenImageColor(2048,2048,WHITE);
    const bool exported = ExportImage(large,"assets/textures/oversized.png"); UnloadImage(large);
    require(exported,"Over-budget texture fixture export failed");
    BuildingRenderer optional;
    require(optional.build(copies,error,true,{"oversized.png"}) && optional.stats().gpu_texture_bytes<=BuildingRenderer::texture_budget
        && optional.warning().find("oversized.png")!=std::string::npos,"An unused oversized thumbnail disabled a valid building");
    require(!optional.thumbnail("oversized.png",{0,0,16,16}),"Skipped oversized thumbnail unexpectedly allocated a texture layer");
    auto heavy_texture = copies; heavy_texture.front().texture = "oversized.png";
    require(!renderer.build(heavy_texture,error) && renderer.stats().gpu_texture_bytes==stats.gpu_texture_bytes,
        "Over-budget texture replaced the last valid building batch");
    auto oversized = copies; while (oversized.size()*3*36<=BuildingRenderer::geometry_budget) oversized.insert(oversized.end(),copies.begin(),copies.end());
    require(!renderer.build(oversized,error) && renderer.stats().gpu_geometry_bytes==stats.gpu_geometry_bytes,
        "Over-budget bake replaced the last valid building batch");
}
}
int main() {
    SetTraceLogLevel(LOG_WARNING); SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(1280,720,"Building editor rendering checks");
    if (!IsWindowReady()) { std::cerr << "Building checks require an OpenGL graphics context\n"; return 1; }
    const auto previous = std::filesystem::current_path(), output = std::filesystem::path(GetApplicationDirectory());
    const auto root = std::filesystem::temp_directory_path()/("ambaretto-building-render-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto folder = root/"assets/textures";
    const auto cleanup = [&] {
        std::filesystem::current_path(previous); std::error_code ignored;
        for (const char* file : {"wall.png","roof.png","base.png","trim.png","window.png","oversized.png"}) std::filesystem::remove(folder/file,ignored);
        for (const auto& dir : {folder,root/"assets",root}) std::filesystem::remove(dir,ignored);
    };
    int result = 0;
    try {
        std::filesystem::create_directories(folder);
        Image wall = GenImageChecked(128,128,64,64,RED,BLUE), roof = GenImageColor(128,128,{50,65,90,255}),
            base = GenImageColor(128,128,RED), trim = GenImageColor(128,128,YELLOW), window = GenImageColor(128,128,BLANK);
        ImageDrawRectangle(&window,32,16,64,96,GREEN);
        for (const auto& item : {std::pair{"wall.png",wall},std::pair{"roof.png",roof},std::pair{"base.png",base},std::pair{"trim.png",trim},std::pair{"window.png",window}}) {
            const bool saved = ExportImage(item.second,(folder/item.first).string().c_str()); UnloadImage(item.second);
            require(saved,"Building texture fixture export failed");
        }
        std::filesystem::current_path(root);
        const auto target = LoadRenderTexture(1280,720);
        require(IsRenderTextureValid(target),"Building preview framebuffer could not be created");
        try { building_preview_and_benchmark(target,output); cutout_shadows(target,output); }
        catch (...) { UnloadRenderTexture(target); throw; }
        UnloadRenderTexture(target);
        std::cout << "Building texture arrays, tiling, surface decals, cutout shadows and bounded bake checks passed\n";
    } catch (const std::exception& error) { std::cerr << "Building rendering check failed: " << error.what() << '\n'; result = 1; }
    cleanup(); CloseWindow(); return result;
}
