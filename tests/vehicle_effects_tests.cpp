#include "car_renderer.hpp"
#include <rlgl.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cmath>
#include "city.hpp"

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void imported_cars() {
    using namespace ambaretto;
    const auto previous=std::filesystem::current_path(), root=std::filesystem::temp_directory_path()/("ambaretto-glb-"+City::create("test").id);
    std::filesystem::create_directories(root/"assets/cars"); std::filesystem::create_directories(root/"assets/wheels");
    const auto model = [&](const char* path,Vector3 size) {
        Mesh mesh=GenMeshCube(size.x,size.y,size.z); const int count=mesh.triangleCount*3;
        std::vector<float> positions,normals,uv;
        for(int i=0;i<count;++i) {
            const int v=mesh.indices ? mesh.indices[i] : i;
            positions.insert(positions.end(),mesh.vertices+v*3,mesh.vertices+v*3+3);
            normals.insert(normals.end(),mesh.normals+v*3,mesh.normals+v*3+3);
            uv.insert(uv.end(),mesh.texcoords+v*2,mesh.texcoords+v*2+2);
        }
        Image image=GenImageColor(2,2,{240,30,170,255}); ImageDrawPixel(&image,0,0,{25,230,45,255}); ImageDrawPixel(&image,1,1,{25,230,45,255});
        int png_size=0; auto png=ExportImageToMemory(image,".png",&png_size); UnloadImage(image);
        std::vector<unsigned char> binary;
        const auto append=[&](const void* data,std::size_t bytes){const auto* p=static_cast<const unsigned char*>(data); binary.insert(binary.end(),p,p+bytes);};
        append(positions.data(),count*12); append(normals.data(),count*12); append(uv.data(),count*8); append(png,png_size); MemFree(png); UnloadMesh(mesh);
        std::ostringstream json;
        json << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0,"translation":[3,4,5]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"material":0}]}],"materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0},"baseColorFactor":[1,1,1,1]},"doubleSided":true}],"textures":[{"source":0}],"images":[{"bufferView":3,"mimeType":"image/png"}],"buffers":[{"byteLength":)" << binary.size() << R"(}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":)" << count*12
            << R"(},{"buffer":0,"byteOffset":)" << count*12 << R"(,"byteLength":)" << count*12 << R"(},{"buffer":0,"byteOffset":)" << count*24 << R"(,"byteLength":)" << count*8 << R"(},{"buffer":0,"byteOffset":)" << count*32 << R"(,"byteLength":)" << png_size
            << R"(}],"accessors":[{"bufferView":0,"componentType":5126,"count":)" << count << R"(,"type":"VEC3","min":[-1,-1,-1],"max":[1,1,1]},{"bufferView":1,"componentType":5126,"count":)" << count << R"(,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":)" << count << R"(,"type":"VEC2"}]})";
        auto text=json.str(); while(text.size()%4) text+=' '; while(binary.size()%4) binary.push_back(0);
        const std::uint32_t header[]={0x46546c67,2,std::uint32_t(28+text.size()+binary.size()),std::uint32_t(text.size()),0x4e4f534a};
        const std::uint32_t bin[]={std::uint32_t(binary.size()),0x004e4942};
        std::ofstream file(root/path,std::ios::binary); file.write(reinterpret_cast<const char*>(header),sizeof(header)); file.write(text.data(),text.size());
        file.write(reinterpret_cast<const char*>(bin),sizeof(bin)); file.write(reinterpret_cast<const char*>(binary.data()),binary.size());
    };
    model("assets/cars/textured.glb",{1,1,2}); model("assets/wheels/tire.glb",{1,.15f,1});
    std::filesystem::current_path(root);
    {
        CarRenderer renderer; CarDesign design; design.body="textured.glb"; design.wheel="tire.glb"; std::string error;
        require(renderer.available(design,error),error.c_str());
        auto invalid=design; invalid.body="late.glb"; require(!renderer.available(invalid,error),"Missing GLB silently used a stock model");
        model("assets/cars/late.glb",{1,1,2}); renderer.refresh(); require(renderer.available(invalid,error),"Refresh did not load a newly added GLB");
        PhysicsWorld world(false); Car car(world,design); car.reset({0,car.ride_height(),0});
        SceneLighting lighting; GraphicsSettings settings; settings.shadows=0; DayNight day;
        const Camera3D camera{{6,3,6},{0,1,0},{0,1,0},5,CAMERA_ORTHOGRAPHIC}; const auto texture=LoadRenderTexture(256,256);
        const auto render=[&](bool wheels=true) {
            renderer.set_lighting(lighting,camera,day.lighting(),settings); BeginTextureMode(texture); ClearBackground(BLACK); BeginMode3D(camera);
            require(renderer.draw_body(car,camera),"Imported body did not render");
            if(wheels) for(const auto& wheel:car.wheels()) require(renderer.draw_wheel(car,wheel,camera),"Imported wheel did not render");
            EndMode3D(); EndTextureMode(); Image image=LoadImageFromTexture(texture.texture); ImageFlipVertical(&image); return image;
        };
        Image first=render(); design.type=CarType::Police; car.set_design(design); Image second=render(); int pink=0,green=0;
        for(int y=0;y<256;++y) for(int x=0;x<256;++x) {
            const Color a=GetImageColor(first,x,y), b=GetImageColor(second,x,y);
            require(a.r==b.r && a.g==b.g && a.b==b.b,"Police type changed the embedded GLB texture");
            pink+=a.r>a.g*2 && a.b>a.g*2; green+=a.g>a.r*2 && a.g>a.b*2;
        }
        require(pink>100 && green>100,"Embedded GLB texture was lost");
        UnloadImage(first); UnloadImage(second);
        const auto center=[](Image image) {
            int left=image.width, top=image.height, right=0, bottom=0;
            for(int y=0;y<image.height;++y) for(int x=0;x<image.width;++x) {
                const auto color=GetImageColor(image,x,y); if(!color.r && !color.g && !color.b) continue;
                left=std::min(left,x); right=std::max(right,x); top=std::min(top,y); bottom=std::max(bottom,y);
            }
            require(right>left && bottom>top,"Offset body is invisible"); return Vector2{(left+right)/2.f,(top+bottom)/2.f};
        };
        car.set_tuning(car.tuning()); std::array<Vec3,4> wheel_positions;
        for(int i=0;i<4;++i) wheel_positions[i]=car.wheel_center(car.wheels()[i]);
        const auto reference=car.position(); Image before=render(false);
        design.offset={.25f,.15f,-.25f}; car.set_design(design); Image after=render(false);
        const auto a=center(before), b=center(after), projected_origin=GetWorldToScreenEx({0,0,0},camera,256,256),
            projected_offset=GetWorldToScreenEx({design.offset[0],design.offset[1],design.offset[2]},camera,256,256);
        require(std::abs((b.x-a.x)-(projected_offset.x-projected_origin.x))<1.1f
            && std::abs((b.y-a.y)-(projected_offset.y-projected_origin.y))<1.1f,"Rendered X/Y/Z offsets do not match the body position");
        require((car.position()-reference).Length()<.001f,"Body offset moved the car reference");
        for(int i=0;i<4;++i) require((car.wheel_center(car.wheels()[i])-wheel_positions[i]).Length()<.001f,"Body offset moved a wheel");
        UnloadImage(before); UnloadImage(after); UnloadRenderTexture(texture);
        std::cout << "Imported textured GLBs, node transforms, wheel alignment, refresh and unchanged paint passed\n";
    }
    std::filesystem::current_path(previous);
    for(const auto* path:{"assets/cars/textured.glb","assets/cars/late.glb","assets/wheels/tire.glb","assets/cars","assets/wheels","assets"}) std::filesystem::remove(root/path);
    std::filesystem::remove(root);
}
}
int main(int argc, char** argv) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(256, 256, "Vehicle shader checks");
    int result = 0;
    {
        ambaretto::CarRenderer renderer;
        const auto texture = LoadRenderTexture(256, 256);
        const Camera3D camera{{0, 2, 10}, {0, 2, 0}, {0, 1, 0}, 8, CAMERA_ORTHOGRAPHIC};
        const auto render = [&](bool wreck, float age, float time, bool covered = false) {
            BeginTextureMode(texture); ClearBackground(BLACK); BeginMode3D(camera);
            if (covered) DrawCube({0, 2, 2}, 8, 8, .2f, GREEN);
            renderer.draw_damage(camera, {0, 0, 0}, wreck, age, 1, time);
            using GetBoolean = void (*)(unsigned int, unsigned char*);
            const auto get_boolean = reinterpret_cast<GetBoolean>(rlGetProcAddress("glGetBooleanv"));
            unsigned char depth_write = 0;
            get_boolean(0x0B72, &depth_write); // GL_DEPTH_WRITEMASK
            require(depth_write, "Vehicle effects did not restore depth writes");
            EndMode3D(); EndTextureMode();
            Image image = LoadImageFromTexture(texture.texture); ImageFlipVertical(&image); return image;
        };
        const auto pixel = [&](Image image, Vector3 p) {
            const auto screen = GetWorldToScreenEx(p, camera, 256, 256);
            return GetImageColor(image, int(screen.x), int(screen.y));
        };
        try {
            imported_cars();
            Image fire = render(true, 0, 0);
            const Color core = pixel(fire, {0, 0, 0}), edge = pixel(fire, {.55f, 0, 0});
            require(core.r > 120 && core.g > 20 && core.g > core.b, "Explosion shader lost its warm core");
            require(edge.r > 0 && edge.r + 30 < core.r, "Explosion edge is not feathered");
            const Color outside = pixel(fire, {.7f, 0, 0}), corner = pixel(fire, {.55f, -.55f, 0});
            require(outside.r == 0 && corner.r == 0, "Explosion has a visible square billboard edge");
            UnloadImage(fire);
            Image smoke = render(false, 0, 0), later = render(false, 0, .25f);
            int faint = 0, dense = 0, changed = 0;
            for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x) {
                const Color a = GetImageColor(smoke, x, y), b = GetImageColor(later, x, y);
                faint += a.r > 0 && a.r < 8;
                dense += a.r > 16;
                changed += a.r != b.r;
            }
            require(faint > 50 && dense > 50, "Smoke shader lost its translucent soft edges");
            require(changed > 100, "Smoke shader and puffs stopped animating");
            if (argc == 2) ExportImage(later, argv[1]);
            UnloadImage(smoke); UnloadImage(later);
            Image covered = render(true, .2f, 0, true);
            const Color wall = pixel(covered, {0, 0, 0});
            require(wall.r == GREEN.r && wall.g == GREEN.g && wall.b == GREEN.b, "Vehicle effects bleed through opaque scenery");
            UnloadImage(covered);
            Image expired = render(true, 8, 0);
            for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x)
                require(GetImageColor(expired, x, y).r == 0, "Expired wreck effects remain visible");
            UnloadImage(expired);
            std::cout << "Vehicle shader softness, animation, occlusion and lifetime checks passed\n";
        } catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
        UnloadRenderTexture(texture);
    }
    CloseWindow();
    return result;
}
