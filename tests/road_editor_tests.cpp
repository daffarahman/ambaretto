#include "environment_renderer.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace ambaretto;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
City sample_city() {
    City city=City::create("Road style checks"); std::string error;
    require(city.add_land({55,58},{71,70},error)
        && city.add_road(City::road_stroke({59,59},{59,68}),error)
        && city.add_road(City::road_stroke({55,64},{73,64}),error)
        && city.add_road(City::road_stroke({59,68},{64,66}),error)
        && city.add_road(City::road_stroke({64,64},{70,58},false,true),error),error.c_str());
    return city;
}
void geometry() {
    for (unsigned mask=0;mask<16;++mask) {
        CityRoadNode node{{64,64},{64,64},{},mask};
        const auto triangles=node.sidewalk_triangles();
        require(!triangles.empty(),"A road shape has no sidewalk");
        for (const auto& t : triangles) {
            require((t[1]-t[0]).Cross(t[2]-t[0]).GetY()>0,"Sidewalk triangle faces down or is degenerate");
            for (const auto p : t) require(std::isfinite(p.GetX()) && std::isfinite(p.GetZ()),"Sidewalk has invalid coordinates");
            const auto p=(t[0]+t[1]+t[2])/3-node.center();
            if (node.bend().empty()) require(std::hypot(p.GetX(),p.GetZ())>2.8f,"Sidewalk crosses the driving lane at a junction");
        }
    }
    const auto nodes=sample_city().road_network(); bool diagonal=false;
    for (const auto& node : nodes) {
        diagonal|=node.first!=node.last;
        // Connected nodes must expose identical sidewalk edges at their shared gate.
        for (int next : node.edges) {
            const auto& other=nodes[next];
            for (int d=0;d<4;++d) if ((node.mask&(1u<<d)) && (other.mask&(1u<<((d+2)%4)))
                && (node.port(d).center-other.port((d+2)%4).center).LengthSq()<.001f) {
                const auto gate=node.port(d); const Vec3 out((d==1)-(d==3),0,(d==2)-(d==0));
                std::vector<Vec3> first,second;
                for (const auto& t : node.sidewalk_triangles()) for (const auto p : t)
                    if (std::abs((p-gate.center).Dot(out))<.001f) first.push_back(p);
                for (const auto& t : other.sidewalk_triangles()) for (const auto p : t)
                    if (std::abs((p-gate.center).Dot(out))<.001f) second.push_back(p);
                require(!first.empty() && !second.empty(),"Sidewalk stops before a connected gate");
                for (auto p : first) require(std::any_of(second.begin(),second.end(),[&](Vec3 q){return (p-q).LengthSq()<.001f;}),"Sidewalk seam does not match at a turn, junction or diagonal");
            }
        }
    }
    require(diagonal,"Preview does not include a diagonal road");
    City street=City::create("Walking caps"); std::string error;
    require(street.add_land({63,63},{65,67},error) && street.add_road(City::road_stroke({64,64},{64,66}),error),error.c_str());
    for (const auto& route : street.traffic_routes(1.2f)) for (auto p : route) {
        const auto a=City::center({64,64}), b=City::center({64,66});
        require(p.GetZ()>=a.GetZ()-City::half_block+1.2f-.001f && p.GetZ()<=b.GetZ()+City::half_block-1.2f+.001f,
            "Pedestrian's dead-end turn leaves the sidewalk");
    }
}
void persistence(const std::filesystem::path& root) {
    City city=sample_city(), loaded; std::string error;
    require(city.save(root,error) && City::load(root/(city.id+".city"),loaded,error) && loaded.road_style==RoadStyle{},"Old/default map lost the default road style");
    city.road_style={"road red.png","walk green.png",false};
    require(city.save(root,error) && City::load(root/(city.id+".city"),loaded,error) && loaded.road_style==city.road_style,"Global style or disabled sidewalks did not survive save/load");
    const auto good=city.road_style;
    city.road_style.sidewalk_texture="../outside.png";
    require(!city.save(root,error) && City::load(root/(city.id+".city"),loaded,error) && loaded.road_style==good,"Invalid texture path overwrote a valid city");
    auto path=root/(city.id+".city"); std::ifstream input(path); std::string data((std::istreambuf_iterator<char>(input)),{}); input.close();
    const auto flag=data.rfind("\"road red.png\" 0"); require(flag!=std::string::npos,"Road style was not written");
    data[flag+15]='2'; std::ofstream(path)<<data;
    require(!City::load(path,loaded,error) && loaded.road_style==good,"Invalid sidewalk flag was accepted or partially loaded");
    std::filesystem::remove(path);
}
Image render(const City& city,RenderTexture2D target,Camera3D camera) {
    Environment environment(city); EnvironmentRenderer renderer(environment);
    DayNight day; day.set_time("12:00"); SceneLighting lighting; GraphicsSettings settings; settings.shadows=0; settings.local_lights=false; settings.view_distance=6000;
    while (!renderer.update_streaming(camera,settings)) {}
    BeginTextureMode(target); ClearBackground(BLACK); BeginMode3D(camera);
    renderer.draw(camera,0,day.lighting(),lighting,settings); EndMode3D(); EndTextureMode();
    Image result=LoadImageFromTexture(target.texture); ImageFlipVertical(&result); return result;
}
Color at(Image image,Camera3D camera,Vec3 p) {
    const auto screen=GetWorldToScreenEx({p.GetX(),p.GetY()+.1f,p.GetZ()},camera,image.width,image.height);
    return GetImageColor(image,int(screen.x),int(screen.y));
}
bool red(Color c) { return c.r>c.g*3 && c.r>c.b*3; }
bool green(Color c) { return c.g>c.r*3 && c.g>c.b*3; }
bool blue(Color c) { return c.b>c.r*3 && c.b>c.g*3; }
void freeform_rendering(const std::filesystem::path& root,const std::filesystem::path& output) {
    const auto textures=root/"freeform/assets/textures";std::filesystem::create_directories(textures);
    for(const auto& fixture:{std::pair<const char*,Color>{"soil.png",{0,0,255,255}},{"beach-sand.png",RED},{"grass.png",GREEN},{"asphalt.png",RED},{"brick08.png",GREEN}}) {
        auto image=GenImageColor(16,16,fixture.second);const bool saved=ExportImage(image,(textures/fixture.first).string().c_str());UnloadImage(image);
        require(saved,"Freeform texture fixture could not be written");
    }
    const auto previous=std::filesystem::current_path();std::filesystem::current_path(root/"freeform");
    City city=City::create("Freeform render checks");city.make_freeform();std::string error;
    CityArea island;island.path.closed=true;
    for(auto p:{Vec3(-70,City::level,-70),Vec3(70,City::level,-70),Vec3(70,City::level,70),Vec3(-70,City::level,70)})island.path.nodes.push_back({p,Vec3::sZero()});
    require(city.add_area(island,error),error.c_str());
    CityArea paint;paint.kind=AreaKind::Texture;paint.texture="grass.png";paint.path.closed=true;
    for(auto p:{Vec3(-45,City::level,-45),Vec3(45,City::level,-45),Vec3(45,City::level,-30),Vec3(-25,City::level,-30),Vec3(-25,City::level,10),Vec3(-45,City::level,10)})paint.path.nodes.push_back({p,Vec3::sZero()});
    require(city.add_area(paint,error),error.c_str());
    PathRoad road;road.path.nodes={{Vec3(-50,City::level,15),Vec3(30,0,30)},{Vec3(50,City::level,15),Vec3(30,0,-30)}};
    require(city.add_path_road(road,error),error.c_str());
    const Camera3D camera{{0,250,0},{0,0,0},{0,0,-1},160,CAMERA_ORTHOGRAPHIC};
    const auto target=LoadRenderTexture(1200,800);auto image=render(city,target,camera);
    require(ExportImage(image,(output/"freeform-textures-top.png").string().c_str()),"Could not export freeform texture preview");
    require(blue(at(image,camera,Vec3(0,City::level,-20))),"Concave texture fill covered its notch");
    require(green(at(image,camera,Vec3(-40,City::level,-20))),"Pen texture is missing inside its outline");
    const auto beach=at(image,camera,Vec3(68,city.terrain_height(68,0),0));
    require(beach.r>beach.b*2,"Default ground does not blend into the sand texture at the shore");
    require(red(at(image,camera,Vec3(0,City::level,40))),"Bezier pavement is missing or faces down");
    require(green(at(image,camera,Vec3(0,City::level,43))),"Bezier sidewalk is missing or faces down");
    UnloadImage(image);
    auto junction_camera=camera;junction_camera.fovy=80;
    for (bool crossing:{false,true}) for (bool reverse:{false,true}) {
        City junction=city;junction.paths.clear();junction.road_style={"asphalt.png","brick08.png",true};
        PathRoad through,branch;through.path.nodes={{{-55,City::level,0},Vec3::sZero()},{{55,City::level,0},Vec3::sZero()}};
        branch.path.nodes={{{0,City::level,-55},Vec3::sZero()},{{0,City::level,crossing ? 55.f : 0.f},Vec3::sZero()}};
        require(junction.add_path_road(reverse ? branch : through,error) && junction.add_path_road(reverse ? through : branch,error),error.c_str());
        image=render(junction,target,junction_camera);
        require(ExportImage(image,(output/(crossing ? "freeform-cross-junction.png" : "freeform-t-junction.png")).string().c_str()),"Could not export junction preview");
        for (float z=-14;z<=14;z+=.7f) for (float x=-14;x<=14;x+=.7f) {
            const Vec3 point(x,City::level,z);
            if (junction.road_at(point,-.35f)) require(!green(at(image,junction_camera,point)),"A sidewalk crosses a junction driving lane");
        }
        require(red(at(image,junction_camera,Vec3(0,City::level,0))),"Lane markings cross the middle of a junction");
        const auto stripe=at(image,junction_camera,Vec3(20,City::level,0));
        require(stripe.r>stripe.b*1.3f && stripe.g>stripe.b*1.3f,"Junction trimming removed the approaching center line");
        require(red(at(image,junction_camera,Vec3(1.5f,City::level,-5.55f))),"A curb blocks the branch entrance");
        require(crossing ? red(at(image,junction_camera,Vec3(1.5f,City::level,5.55f))) : green(at(image,junction_camera,Vec3(1.5f,City::level,5.55f))),"T-junction lost its opposite curb or crossing kept an internal curb");
        require(green(at(image,junction_camera,Vec3(12,City::level,-5.55f))),"Junction trimming removed the approaching sidewalk");
        UnloadImage(image);
    }
    {
        Environment environment(city);EnvironmentRenderer renderer(environment);WorldMapView view;view.scale=2;
        const Rectangle viewport{0,0,1200,800};
        BeginTextureMode(target);ClearBackground(BLACK);
        renderer.world_map(view,viewport,Vec3::sZero(),Vec3::sAxisZ());EndTextureMode();
        image=LoadImageFromTexture(target.texture);ImageFlipVertical(&image);
        const auto land=view.project(Vec3(55,0,-55),viewport),sea=view.project(Vec3(90,0,-55),viewport);
        const auto land_color=GetImageColor(image,int(land.x),int(land.y)),sea_color=GetImageColor(image,int(sea.x),int(sea.y));
        require(land_color.r==96 && land_color.g==96 && land_color.b==96,"Sea-level land disappeared from the map");
        require(sea_color.r==24 && sea_color.g==44 && sea_color.b==70,"Map land mask covered the water");
        UnloadImage(image);
    }
    UnloadRenderTexture(target);std::filesystem::current_path(previous);
    for(const auto& name:{"soil.png","beach-sand.png","grass.png","asphalt.png","brick08.png"})std::filesystem::remove(textures/name);
    std::filesystem::remove(textures);std::filesystem::remove(root/"freeform/assets");std::filesystem::remove(root/"freeform");
}
void rendering(const std::filesystem::path& root,const std::filesystem::path& output) {
    const auto textures=root/"assets/textures"; std::filesystem::create_directories(textures);
    for (const auto& fixture : {std::pair<const char*,Color>{"road red.png",{255,0,0,255}},{"walk green.png",{0,255,0,255}},{"asphalt.png",{0,0,255,255}},{"brick08.png",{0,255,0,255}}}) {
        auto image=GenImageColor(16,16,fixture.second); const bool saved=ExportImage(image,(textures/fixture.first).string().c_str()); UnloadImage(image);
        require(saved,"Road texture fixture could not be written");
    }
    const auto previous=std::filesystem::current_path(); std::filesystem::current_path(root);
    City city=sample_city(); std::string error; city.road_style={"road red.png","walk green.png",true};
    require(city.paint_ground({57,60},{57,60},CityGround::Asphalt,error),error.c_str());
    const Vec3 focus=City::center({64,64});
    const Camera3D camera{{focus.GetX(),250,focus.GetZ()},{focus.GetX(),City::level,focus.GetZ()},{0,0,-1},160,CAMERA_ORTHOGRAPHIC};
    const auto target=LoadRenderTexture(1200,800);
    Image enabled=render(city,target,camera);
    require(ExportImage(enabled,(output/"roads-sidewalks-top.png").string().c_str()),"Could not export road preview");
    require(blue(at(enabled,camera,City::center({57,60}))),"Changing road style recolored painted asphalt terrain");
    const auto nodes=city.road_network(); int bends=0,junctions=0,diagonals=0,bridges=0;
    for (const auto& node : nodes) {
        if (!node.bend().empty()) ++bends;
        if (!node.outline().empty()) ++junctions;
        if (node.first!=node.last) ++diagonals;
        if (city.tile(node.first)==CityTile::Bridge) ++bridges;
        bool visible=false;
        for (const auto& t : node.sidewalk_triangles()) visible|=green(at(enabled,camera,(t[0]+t[1]+t[2])/3));
        require(visible,"Sidewalk is missing on a straight, bend, junction, diagonal, dead end or bridge");
        const auto path=node.path(.4f);
        Vec3 pavement=path.empty() ? node.center()+Vec3(.4f,0,.4f) : path[path.size()/2];
        require(red(at(enabled,camera,pavement)),"Global road texture did not reach every road shape");
    }
    require(bends && junctions && diagonals && bridges,"Render sample is missing a required road shape");
    city.road_style.sidewalks=false; Image disabled=render(city,target,camera);
    for (const auto& node : nodes) for (const auto& t : node.sidewalk_triangles())
        require(!green(at(disabled,camera,(t[0]+t[1]+t[2])/3)),"Disabling sidewalks left visible paving");
    city.road_style={"missing.png","missing.png",true}; Image fallback=render(city,target,camera);
    require(blue(at(fallback,camera,City::center({59,62})+Vec3(.4f,0,0))),"Missing road image did not fall back to asphalt");
    City hill=City::create("Sidewalk ramps"); hill.road_style={"road red.png","walk green.png",true};
    require(hill.add_land({60,61},{70,67},error) && hill.change_elevation({64,61},{70,67},1,error)
        && hill.add_road(City::road_stroke({60,64},{70,64}),error),error.c_str());
    Image slope=render(hill,target,camera); bool ramp=false;
    for (const auto& node : hill.road_network()) {
        const auto t=node.sidewalk_triangles().front(); Vec3 p=(t[0]+t[1]+t[2])/3; p.SetY(hill.height(p.GetX(),p.GetZ()));
        require(green(at(slope,camera,p)),"Sidewalk cuts through or disappears on a ramp");
        ramp|=hill.ramp_axis(node.first)!=0;
    }
    require(ramp,"Sidewalk test has no sloped road");
    UnloadImage(enabled); UnloadImage(disabled); UnloadImage(fallback); UnloadImage(slope); UnloadRenderTexture(target);
    std::filesystem::current_path(previous);
    for (const auto& fixture : {"road red.png","walk green.png","asphalt.png","brick08.png"}) std::filesystem::remove(textures/fixture);
    for (const auto& folder : {textures,root/"assets"}) std::filesystem::remove(folder);
}
}
int main() {
    SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(1200,800,"Road editor checks"); SetTraceLogLevel(LOG_WARNING);
    const auto root=std::filesystem::temp_directory_path()/("ambaretto-roads-"+ambaretto::City::create("Tests").id);
    std::filesystem::create_directories(root);
    try {
        geometry(); persistence(root); rendering(root,std::filesystem::current_path());freeform_rendering(root,std::filesystem::current_path());
        std::filesystem::remove(root); CloseWindow();
        std::cout<<"Global road styles, safe persistence, sidewalk seams, every road shape and texture fallbacks passed\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; CloseWindow(); return 1; }
}
