#include "city.hpp"
#include "environment.hpp"
#include "race.hpp"
#include "player.hpp"
#include "pedestrians.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace ambaretto;
namespace {
void require(bool condition,const std::string& message) {if(!condition)throw std::runtime_error(message);}
CityArea area(AreaKind kind,std::initializer_list<std::array<float,2>> points) {
    CityArea result;result.kind=kind;result.path.closed=true;
    for(auto p:points)result.path.nodes.push_back({Vec3(p[0],City::level,p[1]),Vec3::sZero()});
    return result;
}
PathRoad line(Vec3 a,Vec3 b) {PathRoad road;road.path.nodes={{a,Vec3::sZero()},{b,Vec3::sZero()}};return road;}
void junctions() {
    std::string error;City city=City::create("Road connections");city.make_freeform();
    const auto p=[](float x,float z) {return Vec3(x,City::level,z);};
    auto road=line(p(-60,0),p(60,0));road.path.nodes.insert(road.path.nodes.begin()+1,{p(3.25f,0),Vec3::sZero()});
    require(city.add_path_road(road,error),error);
    require(city.snap_road_point(p(4.5f,1),3)==std::optional<Vec3>(p(3.25f,0)),"Snapping did not prefer the existing editable node");
    require(!city.snap_road_point(p(0,20),3) && !city.snap_road_point(p(3.25f,0),3,0),"Snapping caught a distant road or the road being edited");
    const auto edge_snap=city.snap_road_point(p(17,4.5f),.5f);
    require(edge_snap && (*edge_snap-p(17,0)).Length()<.001f,"Zooming in prevented a branch on the pavement from connecting");
    require(!city.snap_road_point(p(0,0),std::numeric_limits<float>::infinity()),"Invalid snap radius was accepted");
    const auto connection=city.snap_road_point(p(17.37f,1),3);
    require(connection && (*connection-p(17.37f,0)).Length()<.001f,"Branch did not snap onto the middle of a road");
    require(city.add_path_road(line(p(17.37f,-60),*connection),error),error);
    const auto degree=[&](Vec3 point) {
        int links=0;for (const auto& edge:city.road_graph) for (auto end:{edge.a,edge.b}) if ((end-point).Length()<.001f) ++links;
        return links;
    };
    require(degree(*connection)==3,"T-junction does not have three connected arms");
    RaceMap race;race.checkpoints={p(-50,0),p(17.37f,-50)};std::vector<RaceLeg> legs;
    require(build_race_route(city,race,legs,error),"Race cannot turn through a T-junction: "+error);
    const Environment bridge(city);PhysicsWorld world(bridge);GroundHit hit;
    require(!world.cast_ray(p(17.37f,-15)+Vec3(0,.5f,0),Vec3::sAxisZ(),18,hit),"A bridge railing blocks the T-junction");
    require(world.cast_ray(p(-30,-15)+Vec3(0,.5f,0),Vec3::sAxisZ(),18,hit),"Trimming the junction removed an unrelated bridge railing");
    require(city.add_path_road(line(p(17.37f,-60),p(17.37f,60)),error,1),error);
    require(degree(*connection)==4 && build_race_route(city,race,legs,error),"Crossing roads are not a connected four-way junction");
    auto curve=line(p(-50,80),p(50,80));curve.path.nodes[0].tangent=p(30,30);curve.path.nodes[1].tangent=p(30,-30);
    require(city.add_path_road(curve,error),error);
    const Vec3 middle=city.paths.back().centerline[city.paths.back().centerline.size()/2];
    const auto curved_snap=city.snap_road_point(middle+Vec3(0,0,1),3);
    require(curved_snap && city.road_at(*curved_snap,-4.8f),"Snapping onto a curved centerline failed");
}
}
int main(int argc,char** argv) {
    try {
        std::string error;
        junctions();
        City city=City::create("Freeform coast");city.make_freeform();
        auto island=area(AreaKind::Land,{{-200,-200},{200,-200},{200,200},{-200,200}});
        require(polygon_triangles(island.path.sample()).size()==2,"Straight Bezier spans introduced spurious texture triangles");
        island.path.nodes={{Vec3(200,City::level,0),Vec3(0,0,110.46f)},
            {Vec3(0,City::level,200),Vec3(-110.46f,0,0)},
            {Vec3(-200,City::level,0),Vec3(0,0,-110.46f)},
            {Vec3(0,City::level,-200),Vec3(110.46f,0,0)}};
        require(city.add_area(island,error),error);
        require(City::level==Environment::water_level && city.land_at(Vec3(.37f,0,.53f)) && city.terrain_height(0,0)==Environment::water_level,"Land is not level with the water");
        require(std::abs(city.terrain_height(200,0))<.0001f && city.terrain_height(198,0)==Environment::water_level && city.terrain_height(202,0)<0,"Beach does not meet water continuously");
        auto lake=area(AreaKind::Water,{{60,65},{120,65},{120,125},{60,125}});
        require(city.add_area(lake,error),error);
        require(!city.land_at(Vec3(90,0,90)) && city.terrain_height(90,90)<-7,"Lake did not cut through the island");
        auto river=area(AreaKind::Water,{{-170,-200},{-166,-200},{-166,160},{-170,160}});
        require(city.add_area(river,error) && city.terrain_height(-168,50)<0,"A narrow river dried up in the beach transition");
        auto overlapping_land=area(AreaKind::Land,{{-20,-20},{30,-20},{30,30},{-20,30}});
        require(city.add_area(overlapping_land,error) && city.terrain_height(0,0)==City::level,"Overlapping land introduced an internal beach seam");
        auto concave=area(AreaKind::Texture,{{-80,-110},{80,-110},{80,-70},{-20,-70},{-20,-30},{-80,-30}});
        concave.texture="grass.png";
        require(city.add_area(concave,error),error);
        float surface=0;
        for(auto t:polygon_triangles(concave.path.sample()))surface+=(t[1]-t[0]).Cross(t[2]-t[0]).Length()/2;
        require(std::abs(surface-8800)<.1f,"Concave texture fill has gaps or covers its notch");
        const auto before=city.areas.size();
        require(!city.add_area(area(AreaKind::Land,{{-50,-50},{50,50},{-50,50},{50,-50}}),error) && city.areas.size()==before,"Self crossing shape changed the city");
        PathRoad curve=line(Vec3(-100,City::level,0),Vec3(100,City::level,0));
        curve.path.nodes[0].tangent=Vec3(60,0,80);curve.path.nodes[1].tangent=Vec3(60,0,-80);
        require(city.add_path_road(curve,error),error);
        const auto sampled=city.paths.front().centerline;
        require(std::any_of(sampled.begin(),sampled.end(),[](Vec3 p){return (p-Vec3(0,City::level,60)).Length()<.01f;}),"Road is not a cubic Bezier curve");
        require(city.road_at(Vec3(0,City::level,60)) && !city.road_at(Vec3(0,City::level,0)),"Curved road picking still uses tile occupancy");
        require(city.add_path_road(line(Vec3(0,City::level,-150),Vec3(0,City::level,150)),error),error);
        RaceMap race;race.name="Curve crossing";race.checkpoints={sampled.front(),Vec3(0,City::level,-120)};
        std::vector<RaceLeg> legs;
        require(build_race_route(city,race,legs,error) && legs.size()==2,"Road crossings are not connected for races: "+error);
        const auto routes=city.traffic_routes();
        require(!routes.empty(),"Curved roads have no traffic circuits");
        for(const auto& route:routes)for(std::size_t i=0;i<route.size();++i)
            require((route[(i+1)%route.size()]-route[i]).Length()<12,"Traffic jumps between disconnected street ends");
        City overlap=city;CityBuilding crossing;crossing.position=Vec3(0,City::level,60);
        require(overlap.add_building(crossing,error) && overlap.add_path_road(line(Vec3(-120,City::level,-140),Vec3(120,City::level,-140)),error),
            "A freely placed building blocked unrelated road edits: "+error);
        CityBuilding b;b.position=Vec3(40.375f,City::level,-145.625f);b.yaw=37.5f;
        require(city.add_building(b,error),error);
        require(city.buildings.back().base(City::level)==*b.position && city.building_at(*b.position)==0,"Building position was snapped");
        const auto rotation=Quat::sRotation(Vec3::sAxisY(),37.5f*JPH::JPH_PI/180);
        const Vec3 corner=*b.position+rotation*Vec3(City::half_block-.01f,0,City::half_block-.01f);
        require(city.building_at(corner)==0,"Rotated building cannot be selected at a corner");
        require(city.brush_trees(Vec3(140,City::level,-100),32,3,false,error)>0,"Existing tree brush stopped working");
        require(city.set_spawn(Vec3(-100,city.height(-100,0),0),error),error);
        city.races.push_back(race);
        const auto directory=argc>1 ? std::filesystem::absolute(argv[1]) : std::filesystem::temp_directory_path()/("ambaretto-freeform-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(city.save(directory,error),error);
        const auto path=directory/(city.id+".city");City loaded;
        require(City::load(path,loaded,error) && loaded.freeform && loaded.areas.size()==city.areas.size() && loaded.paths.size()==2 && loaded.buildings[0].yaw==37.5f && loaded.buildings[0].position==b.position,error);
        Environment environment(loaded);PhysicsWorld world(environment);GroundHit hit;
        const Vec3 wall=*b.position+rotation*Vec3(0,5,-City::half_block);
        require(world.cast_ray(wall-rotation*Vec3(0,0,5),rotation*Vec3::sAxisZ(),10,hit) && (hit.point-wall).Length()<.02f,"Rotated wall rendering and collision disagree");
        require(world.cast_ray(Vec3(90,10,90),-Vec3::sAxisY(),30,hit) && hit.point.GetY()<-6,"Lake has invisible ground at the old land level");
        require(std::abs(environment.terrain_height(195.37f,1.25f)-environment.height(195.37f,1.25f))<.001f,"Height queries disagree away from a road");
        {
            Car car(world);Player player(world,car,environment);Pedestrians pedestrians(world,environment);
            player.respawn_near(Vec3::sZero());
            require(std::abs(player.position().GetY()-.08f)<.01f && player.position().Length()<50,"Respawn rejected sea-level land");
            require(std::any_of(pedestrians.people().begin(),pedestrians.people().end(),[](const auto& p){return p.enabled;}),"Pedestrians rejected sea-level sidewalks");
        }
        City bridge=City::create("Freeform bridge sidewalk");bridge.make_freeform();
        require(bridge.add_path_road(line(Vec3(-30,City::level,0),Vec3(30,City::level,0)),error),error);
        Environment bridge_environment(bridge);PhysicsWorld bridge_world(bridge_environment);
        require(bridge_environment.height(0,5.55f)==City::level && bridge_world.cast_ground(Vec3(0,5,5.55f),-Vec3::sAxisY(),10,hit)
            && std::abs(hit.point.GetY()-City::level)<.01f,"Bridge sidewalk has no matching collision deck");
        require(!loaded.add_path_road(line(Vec3(1e20f,0,0),Vec3(0,0,0)),error),"Out of bounds road accepted");
        loaded.paths[0].path.nodes[0].tangent.SetX(std::numeric_limits<float>::quiet_NaN());
        require(!loaded.save(directory,error) && City::load(path,loaded,error),"Invalid curve damaged the previous save");
        City legacy=City::create("Legacy flat conversion");
        require(legacy.add_land({60,60},{68,68},error) && legacy.add_road(City::road_stroke({61,64},{67,64}),error),error);
        require(legacy.add_building({{61,61},1,12},error),error);legacy.make_freeform();
        require(legacy.freeform && legacy.validate(error) && legacy.land_at(City::center({63,63})) && legacy.road_at(City::center({64,64})),"Legacy conversion failed: "+error);
        for(std::uint32_t seed:{0u,1u,1234u,4294967295u}) {
            City generated=City::create("Generated flat islands");
            require(generated.generate_terrain(128,128,seed,error,{0,35,true,true}),error);generated.make_freeform(true);
            require(generated.validate(error),"Generated coastline conversion failed: "+error);
            require(std::all_of(generated.areas.begin(),generated.areas.end(),[](const auto& a){return std::all_of(a.path.nodes.begin(),a.path.nodes.end(),[](const auto& n){return n.tangent.LengthSq()>0;});}),"Generated coastline still has square corners");
        }
        for(CityCell size:{CityCell{1,1},{1,128},{128,1},{8,32},{128,128}}) {
            City generated=City::create("Smooth terrain limits");
            require(generated.generate_terrain(size.x,size.z,4294967295u,error),error);generated.make_freeform(true);
            require(generated.validate(error),"Smoothing broke a supported starter size: "+error);
        }
        City preview=City::create("Generated preview");
        require(preview.generate_terrain(48,32,1234,error,{0,35}),error);preview.make_freeform(true);
        require(preview.validate(error),"Rectangular starter coastline conversion failed: "+error);
        for(int i=0;i<City::width*City::width;++i)
            require(preview.land({i%City::width,i/City::width})==(preview.tiles[i]==CityTile::Land),"Coastline conversion changed the displayed land mask");
        City budget=City::create("Path budget");budget.freeform=true;
        budget.areas.assign(24,area(AreaKind::Land,{{-700,-700},{700,-700},{700,700},{-700,700}}));budget.rebuild_paths();
        require(!budget.validate_paths(error),"Simplified shore caches bypassed the saved path geometry budget");
        PathRoad loop; loop.path.closed=true;
        loop.path.nodes={{Vec3(-40,City::level,-40),Vec3(24,0,-24)},{Vec3(40,City::level,-40),Vec3(24,0,24)},
            {Vec3(40,City::level,40),Vec3(-24,0,24)},{Vec3(-40,City::level,40),Vec3(-24,0,-24)}};
        City circuit=City::create("Closed road");circuit.make_freeform();
        require(circuit.add_path_road(loop,error) && circuit.paths[0].centerline.front()==circuit.paths[0].centerline.back(),"Road loop is open");
        for(float offset:{6.2f,-6.2f}) {
            const auto edge=offset_path(circuit.paths[0].centerline,offset);
            require((edge.front()-edge.back()).LengthSq()<.0001f,"Closed road or sidewalk has a seam");
        }
        require(circuit.save(directory,error) && City::load(directory/(circuit.id+".city"),loaded,error) && loaded.paths[0].path.closed,"Road loop did not survive save/load");
        const Environment sea_level(preview);PhysicsWorld sea_level_world(sea_level);
        for(const auto& a:preview.areas)for(const auto& n:a.path.nodes) {
            require(std::abs(preview.terrain_height(n.position.GetX(),n.position.GetZ()))<.0001f,"Coastline leaves the water plane");
            require(sea_level_world.cast_ground(n.position+Vec3(0,5,0),-Vec3::sAxisY(),10,hit) && std::abs(hit.point.GetY())<.002f,"Sea-level coastline collision differs from rendering");
        }
        std::cout<<"Freeform loops, smooth sea-level shores, lakes, traffic, races, buildings, collision and save checks passed\n";
        if(argc>1)std::cout<<path.string()<<'\n';
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
