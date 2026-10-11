#include "race.hpp"
#include "environment.hpp"
#include "traffic.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <random>

namespace ambaretto {
namespace {
Vec3 flat(Vec3 p) { p.SetY(0); return p; }
float yaw(Vec3 direction) { return std::atan2(-direction.GetX(),-direction.GetZ()); }
Vec3 side(Vec3 direction) { return direction.Cross(Vec3::sAxisY()); }
bool fail(std::string& error, const std::string& message) { error = message; return false; }
struct Edge { int a, b; float width; };
struct RoadGraph {
    std::vector<Vec3> points;
    std::vector<Edge> edges;
    explicit RoadGraph(const City& city) {
        const auto nodes = city.road_network();
        std::vector<std::vector<int>> ends(nodes.size());
        std::map<std::pair<int,int>,int> indices;
        const auto add = [&](Vec3 p) {
            auto key = std::make_pair(int(std::round(p.GetX()*1000)),int(std::round(p.GetZ()*1000)));
            auto result = indices.emplace(key,int(points.size()));
            if (result.second) { p.SetY(city.height(p.GetX(),p.GetZ())); points.push_back(p); }
            return result.first->second;
        };
        for (std::size_t n = 0; n < nodes.size(); ++n) {
            const auto& node = nodes[n]; auto path = node.path();
            float width = std::numeric_limits<float>::max();
            for (int d = 0; d < 4; ++d) if (node.mask&(1u<<d)) width = std::min(width,node.port(d).width-1.4f);
            if (path.size()==2 && std::abs(path[1].GetX()-path[0].GetX())>.001f && std::abs(path[1].GetZ()-path[0].GetZ())>.001f) {
                const auto inner = node.path(0,.7f), outer = node.path(1,-.7f);
                const Vec3 across = side(flat(path[1]-path[0]).Normalized());
                width = std::min(std::abs((inner[0]-outer[0]).Dot(across)),std::abs((inner[1]-outer[1]).Dot(across)));
            }
            if (!path.empty()) {
                int previous = add(path.front()); ends[n].push_back(previous);
                for (std::size_t i = 1; i < path.size(); ++i) {
                    const int next = add(path[i]); if (next!=previous) edges.push_back({previous,next,width}); previous = next;
                }
                ends[n].push_back(previous);
            } else {
                const int center = add(node.center());
                for (int d = 0; d < 4; ++d) if (node.mask&(1u<<d)) {
                    const auto port = node.port(d); const int end = add(port.center);
                    if (end!=center) edges.push_back({center,end,port.width-1.4f});
                    ends[n].push_back(end);
                }
            }
        }
        for (std::size_t n = 0; n < nodes.size(); ++n) for (int neighbor : nodes[n].edges) if (neighbor>int(n)) {
            float best = std::numeric_limits<float>::max(); int a = -1, b = -1;
            for (int from : ends[n]) for (int to : ends[neighbor]) {
                const float distance = flat(points[from]-points[to]).LengthSq();
                if (distance<best) { best=distance; a=from; b=to; }
            }
            if (a>=0 && b>=0 && a!=b) edges.push_back({a,b,City::block-1.4f});
        }
    }
    int snap(Vec3 click, Vec3& point, float& fraction) const {
        if (!std::isfinite(click.GetX()) || !std::isfinite(click.GetY()) || !std::isfinite(click.GetZ()) || std::abs(click.GetX())>=City::extent || std::abs(click.GetZ())>=City::extent) return -1;
        float best = std::numeric_limits<float>::max(); int found = -1;
        for (std::size_t i = 0; i < edges.size(); ++i) {
            const auto edge = edges[i]; const Vec3 delta = flat(points[edge.b]-points[edge.a]);
            const float t = std::clamp(flat(click-points[edge.a]).Dot(delta)/std::max(delta.LengthSq(),.001f),0.f,1.f);
            const Vec3 p = points[edge.a]+(points[edge.b]-points[edge.a])*t;
            const float distance = flat(click-p).LengthSq();
            if (distance<best && distance<std::pow(edge.width/2-.2f,2)) { found=int(i); best=distance; point=p; fraction=t; }
        }
        return found;
    }
};
std::size_t segment_at(const RaceLeg& leg, float distance) {
    return std::min(leg.points.size()-2,std::size_t(std::max<std::ptrdiff_t>(0,std::upper_bound(leg.distances.begin(),leg.distances.end(),distance)-leg.distances.begin()-1)));
}
void append(RaceLeg& leg, Vec3 p, float width, const City& city) {
    p.SetY(city.height(p.GetX(),p.GetZ()));
    if (leg.points.empty()) { leg.points.push_back(p); leg.distances.push_back(0); return; }
    const Vec3 from = leg.points.back(), delta = p-from;
    if (delta.LengthSq()<.000001f) return;
    const int steps = std::max(1,int(std::ceil(delta.Length()/2)));
    for (int i = 1; i <= steps; ++i) {
        Vec3 q = from+delta*(float(i)/steps); q.SetY(city.height(q.GetX(),q.GetZ()));
        leg.distances.push_back(leg.distances.back()+(q-leg.points.back()).Length());
        leg.widths.push_back(width); leg.points.push_back(q);
    }
}
bool on_leg(const RaceLeg& leg, Vec3 point, float hint) {
    const float distance=leg.locate(point,hint);
    return flat(point-leg.point(distance)).LengthSq()<std::pow(leg.width(distance)/2-.2f,2);
}
}
Vec3 RaceLeg::point(float distance) const {
    distance = std::clamp(distance,0.f,length()); const auto i = segment_at(*this,distance);
    return points[i]+(points[i+1]-points[i])*((distance-distances[i])/(distances[i+1]-distances[i]));
}
Vec3 RaceLeg::direction(float distance) const {
    const auto i = segment_at(*this,std::clamp(distance,0.f,length())); return flat(points[i+1]-points[i]).NormalizedOr(Vec3(0,0,-1));
}
float RaceLeg::width(float distance) const { return widths[segment_at(*this,std::clamp(distance,0.f,length()))]; }
float RaceLeg::locate(Vec3 position, float hint) const {
    float best = std::numeric_limits<float>::max(), result = hint;
    for (std::size_t i = 0; i+1 < points.size(); ++i) {
        if (distances[i+1]<hint-12 || distances[i]>hint+50) continue;
        const Vec3 delta = points[i+1]-points[i];
        const float t = std::clamp((position-points[i]).Dot(delta)/delta.LengthSq(),0.f,1.f);
        const float distance = (position-points[i]-delta*t).LengthSq();
        if (distance<best) { best=distance; result=distances[i]+(distances[i+1]-distances[i])*t; }
    }
    return result;
}
bool snap_race_point(const City& city, Vec3 click, Vec3& point) {
    RoadGraph graph(city); float fraction; return graph.snap(click,point,fraction)>=0;
}
bool build_race_route(const City& city, const RaceMap& race, std::vector<RaceLeg>& legs, std::string& error) {
    legs.clear();
    if (race.name.empty() || race.name.size()>48 || race.name.find_first_not_of(' ')==std::string::npos
        || std::any_of(race.name.begin(),race.name.end(),[](unsigned char c){return c<32 || c>=127;})) return fail(error,"Race name must be 1-48 visible characters.");
    if (race.laps<1 || race.laps>99) return fail(error,"Race laps must be between 1 and 99.");
    if (race.checkpoints.size()<2 || race.checkpoints.size()>256) return fail(error,"Add a start and at least one checkpoint (maximum 256 points).");
    RoadGraph graph(city); const auto original = graph.edges;
    std::vector<std::vector<std::pair<float,int>>> cuts(original.size()); std::vector<int> checkpoints;
    for (Vec3 click : race.checkpoints) {
        Vec3 point; float fraction; const int edge = graph.snap(click,point,fraction);
        if (edge<0) return fail(error,"Every race checkpoint must be on the road. Edit or delete the race before removing its road.");
        const int index = int(graph.points.size()); graph.points.push_back(point); checkpoints.push_back(index); cuts[edge].push_back({fraction,index});
    }
    graph.edges.clear();
    for (std::size_t e = 0; e < original.size(); ++e) {
        auto cut = cuts[e]; cut.push_back({0,original[e].a}); cut.push_back({1,original[e].b}); std::sort(cut.begin(),cut.end());
        for (std::size_t i = 1; i < cut.size(); ++i) graph.edges.push_back({cut[i-1].second,cut[i].second,original[e].width});
    }
    struct Link { int node; float length, width; };
    std::vector<std::vector<Link>> links(graph.points.size());
    for (auto edge : graph.edges) {
        const float length = (graph.points[edge.a]-graph.points[edge.b]).Length();
        links[edge.a].push_back({edge.b,length,edge.width}); links[edge.b].push_back({edge.a,length,edge.width});
    }
    for (std::size_t leg_index = 0; leg_index < checkpoints.size(); ++leg_index) {
        const int start = checkpoints[leg_index], end = checkpoints[(leg_index+1)%checkpoints.size()];
        std::vector<float> distances(links.size(),std::numeric_limits<float>::max()), widths(links.size());
        std::vector<int> previous(links.size(),-1); distances[start]=0;
        std::priority_queue<std::pair<float,int>,std::vector<std::pair<float,int>>,std::greater<std::pair<float,int>>> pending; pending.push({0,start});
        while (!pending.empty()) {
            const auto current = pending.top(); pending.pop(); const int node = current.second;
            if (current.first>distances[node]) continue;
            if (node==end) break;
            for (auto link : links[node]) if (distances[node]+link.length<distances[link.node]) {
                distances[link.node]=distances[node]+link.length; previous[link.node]=node; widths[link.node]=link.width; pending.push({distances[link.node],link.node});
            }
        }
        if (previous[end]<0) return fail(error,"Race checkpoints must share connected roads, including the return to the start.");
        if (distances[end]<8) return fail(error,"Place consecutive checkpoints at least 8 metres apart along the road.");
        std::vector<int> path{end}; while (path.back()!=start) path.push_back(previous[path.back()]); std::reverse(path.begin(),path.end());
        RaceLeg leg; append(leg,graph.points[start],widths[path[1]],city);
        for (std::size_t i = 1; i < path.size(); ++i) {
            const Vec3 p = graph.points[path[i]]; const float width = widths[path[i]];
            if (i+1<path.size()) {
                const Vec3 before = p-graph.points[path[i-1]], after = graph.points[path[i+1]]-p;
                if (before.Length()>1 && after.Length()>1 && flat(before).Normalized().Dot(flat(after).Normalized())<.98f) {
                    const float reach = std::min({3.f,before.Length()*.3f,after.Length()*.3f});
                    const Vec3 a = p-before.Normalized()*reach, b = p+after.Normalized()*reach;
                    append(leg,a,width,city);
                    for (int k = 1; k <= 8; ++k) { const float t = k/8.f; append(leg,a*((1-t)*(1-t))+p*(2*t*(1-t))+b*(t*t),std::min(width,widths[path[i+1]]),city); }
                    continue;
                }
            }
            append(leg,p,width,city);
        }
        legs.push_back(std::move(leg));
    }
    error.clear(); return true;
}
Race::Race(PhysicsWorld& world, const Environment& environment, Traffic& traffic, Car& player, RaceMap map)
    : world_(world), environment_(environment), traffic_(traffic), map_(std::move(map)) { cars_[0]=&player; }
Race::~Race() { traffic_.remove_racers(); }
std::unique_ptr<Race> Race::start(PhysicsWorld& world, const Environment& environment, Traffic& traffic,
    Car& player, const RaceMap& map, const std::vector<CarDesign>& designs, std::string& error) {
    if (!environment.city() || !player.design() || player.destroyed()) { fail(error,"Enter a working car before starting a race."); return {}; }
    if (std::any_of(traffic.cars().begin(),traffic.cars().end(),[](const auto& car){return car.racing;})) { fail(error,"Finish the current race first."); return {}; }
    auto race = std::unique_ptr<Race>(new Race(world,environment,traffic,player,map));
    if (!build_race_route(*environment.city(),map,race->legs_,error)) return {};
    for (std::size_t i = 0; i < race->legs_.size(); ++i) race->map_.checkpoints[i]=race->legs_[i].points.front();
    std::vector<CarDesign> candidates;
    for (const auto& design : designs) if (design.type==CarType::Civilian) candidates.push_back(design);
    if (player.type()==CarType::Civilian) {
        const auto found=std::find_if(candidates.begin(),candidates.end(),[&](const auto& design){return design.name==player.design()->name;});
        if (found==candidates.end()) candidates.push_back(*player.design()); else *found=*player.design();
    }
    if (candidates.empty()) { fail(error,"Create at least one civilian car for the opponents."); return {}; }
    std::mt19937 random(std::random_device{}()); std::uniform_int_distribution<std::size_t> pick(0,candidates.size()-1);
    for (int i = 1; i < 5; ++i) race->cars_[i]=&traffic.add_racer(candidates[pick(random)]);
    float width = player.body_size().GetX(), length = player.body_size().GetZ();
    for (const auto* car : race->cars_) { width=std::max(width,car->body_size().GetX()); length=std::max(length,car->body_size().GetZ()); }
    const auto& leg = race->legs_.front(); const float spacing = length+3;
    const bool pairs = leg.width(0)>width*2+2 && leg.width(spacing*2)>width*2+2;
    std::array<Vec3,5> positions; std::array<float,5> headings;
    const auto grid = [&](bool paired) {
        if (leg.length()<spacing*(paired ? 2 : 4)+length+5) return fail(error,"Move the first checkpoint farther from the start to fit five cars.");
        for (int i = 0; i < 5; ++i) {
            const float distance = i==0 ? 0 : spacing*(paired ? 2-(i-1)/2 : 5-i);
            const Vec3 direction = leg.direction(distance); const float offset = i==0 || !paired ? 0 : ((i%2) ? -1.f : 1.f)*(width/2+.6f);
            race->lane_offsets_[i]=offset;
            Vec3 position = leg.point(distance)+side(direction)*offset; const auto& car = *race->cars_[i];
            for (float longitudinal : {-car.body_size().GetZ()/2,car.body_size().GetZ()/2}) for (float lateral : {-car.body_size().GetX()/2,car.body_size().GetX()/2}) {
                const Vec3 corner = position+Quat::sRotation(Vec3::sAxisY(),yaw(direction))*flat(car.body_offset())+direction*longitudinal+side(direction)*lateral; Vec3 road;
                if (!snap_race_point(*environment.city(),corner,road) || std::abs(environment.surface_height(corner)-position.GetY())>.6f) return fail(error,"The starting grid does not fit this road. Move the start to a wider, flatter section.");
            }
            position.SetY(environment.surface_height(position)+car.ride_height()); headings[i]=yaw(direction); positions[i]=position;
        }
        for (int i = 0; i < 5; ++i) for (int j = i+1; j < 5; ++j) {
            const auto a=Quat::sRotation(Vec3::sAxisY(),headings[i]), b=Quat::sRotation(Vec3::sAxisY(),headings[j]);
            const Vec3 delta=positions[j]+b*flat(race->cars_[j]->body_offset())-positions[i]-a*flat(race->cars_[i]->body_offset());
            bool separated=false;
            for (Vec3 axis : {a*Vec3::sAxisX(),a*Vec3::sAxisZ(),b*Vec3::sAxisX(),b*Vec3::sAxisZ()}) {
                const auto extent=[&](int car,Quat rotation) { const Vec3 size=race->cars_[car]->body_size(); return (std::abs(axis.Dot(rotation*Vec3::sAxisX()))*size.GetX()+std::abs(axis.Dot(rotation*Vec3::sAxisZ()))*size.GetZ())/2+.2f; };
                separated|=std::abs(delta.Dot(axis))>extent(i,a)+extent(j,b);
            }
            if (!separated) return fail(error,"The grid overlaps on this turn. Move the start along the road.");
        }
        return true;
    };
    if (!grid(pairs) && (!pairs || !grid(false))) return {};
    std::vector<Car*> cleared;
    for (const auto& vehicle : traffic.cars()) if (vehicle.npc && vehicle.car.get()!=&player && vehicle.car->simulated()) {
        for (Vec3 p : positions) if ((vehicle.car->position()-p).LengthSq()<12*12) { cleared.push_back(vehicle.car.get()); vehicle.car->set_simulated(false); break; }
    }
    for (int i = 0; i < 5; ++i) {
        if (!race->cars_[i]->can_spawn_at(positions[i],Quat::sRotation(Vec3::sAxisY(),headings[i]))) {
            for (Car* car : cleared) car->set_simulated(true);
            fail(error,"The starting grid is occupied. Move the start or clear the parked vehicles."); return {};
        }
    }
    for (int i = 0; i < 5; ++i) {
        race->cars_[i]->set_simulated(true); race->cars_[i]->reset(positions[i],headings[i]); race->progress_[i].previous=positions[i];
        race->progress_[i].distance=leg.locate(positions[i],0);
    }
    error.clear(); return race;
}
void Race::recover_player() {
    auto& p = progress_[0]; const auto& leg = legs_[(p.next+legs_.size()-1)%legs_.size()];
    float distance = std::max(0.f,p.distance-4); Vec3 position = leg.point(distance); const Vec3 direction = leg.direction(distance);
    position.SetY(environment_.surface_height(position)+cars_[0]->ride_height());
    if (cars_[0]->can_spawn_at(position,Quat::sRotation(Vec3::sAxisY(),yaw(direction)))) {
        cars_[0]->repair(); cars_[0]->reset(position,yaw(direction)); p.previous=position; p.distance=distance;
    }
}
void Race::step(float dt) {
    countdown_=std::max(0.f,countdown_-dt); if (countdown_>0 || done() || failed_) {
        for (int i = 1; i < 5; ++i) cars_[i]->step({0,0,false,true},dt);
        return;
    }
    elapsed_+=dt; plan_time_-=dt;
    if (plan_time_<=0) {
        plan_time_=.05f;
        for (int i = 1; i < 5; ++i) {
            auto& p = progress_[i]; Car& car = *cars_[i];
            if (car.destroyed()) { inputs_[i]={0,0,false,true}; continue; }
            const auto& leg = legs_[(p.next+legs_.size()-1)%legs_.size()];
            const Vec3 position=car.position(), direction=flat(car.forward()).NormalizedOr(leg.direction(p.distance));
            const float speed=flat(car.velocity()).Length(), look=std::clamp(3+speed*.4f,3.f,16.f);
            const auto sample = [&](float ahead) {
                const float distance = p.distance+ahead;
                return distance<=leg.length() ? leg.point(distance) : legs_[p.next].point(distance-leg.length());
            };
            if (pass_blockers_[i] && flat(pass_blockers_[i]->position()-position).Dot(leg.direction(p.distance))<-(car.body_size().GetZ()+pass_blockers_[i]->body_size().GetZ())/2-3) pass_blockers_[i]=nullptr;
            const float lane_limit=std::max(0.f,leg.width(p.distance+look)/2-car.body_size().GetX()/2-.6f);
            Vec3 target=sample(look)+side(flat(sample(look+2)-sample(look)).NormalizedOr(leg.direction(p.distance)))*std::clamp(pass_blockers_[i] ? pass_offsets_[i] : lane_offsets_[i],-lane_limit,lane_limit);
            const Vec3 delta=flat(target-position);
            float curvature=2*delta.Dot(-side(direction))/std::max(delta.LengthSq(),1.f);
            float desired=car.tuning().top_speed;
            const float grip=std::min(2.8f,car.tuning().tire_grip*2.f);
            const float braking_distance=std::max(80.f,speed*speed/10+look+15);
            for (float ahead = 0; ahead <= braking_distance; ahead += 3) {
                const Vec3 a=flat(sample(ahead+3)-sample(ahead)).NormalizedOr(direction), b=flat(sample(ahead+6)-sample(ahead+3)).NormalizedOr(a);
                const float bend=std::acos(std::clamp(a.Dot(b),-1.f,1.f))/3;
                const float corner_speed=std::sqrt(grip/std::max(bend,.0001f));
                desired=std::min(desired,std::sqrt(corner_speed*corner_speed+2*5.f*ahead));
            }
            desired=std::min(desired,std::sqrt(grip/std::max(std::abs(curvature),.0001f)));
            const Vec3 road_direction=leg.direction(p.distance), across=side(road_direction);
            const Car* blocker=nullptr; float nearest=std::max(20.f,speed*speed/10+15);
            const auto consider=[&](const Car& other) {
                if (&other==&car || !other.simulated() || std::abs(other.position().GetY()-position.GetY())>3) return;
                const Vec3 difference=flat(other.position()-position); const float ahead=difference.Dot(road_direction);
                if (ahead>0 && ahead<nearest && std::abs(difference.Dot(across))<(car.body_size().GetX()+other.body_size().GetX())/2+.5f) { blocker=&other; nearest=ahead; }
            };
            consider(*cars_[0]); for (const auto& other : traffic_.cars()) consider(*other.car);
            if (blocker) {
                // shortcut: pass only within this road corridor; a fully blocked street needs detour planning.
                const float limit=std::min(leg.width(p.distance),leg.width(p.distance+look))/2-car.body_size().GetX()/2-.5f;
                const float blocker_side=flat(blocker->position()-leg.point(p.distance+nearest)).Dot(across);
                const float clearance=(car.body_size().GetX()+blocker->body_size().GetX())/2+.8f;
                bool passing=false;
                for (float offset : {blocker_side-clearance,blocker_side+clearance}) {
                    if (std::abs(offset)>limit) continue;
                    const Vec3 pass=sample(look)+across*offset; bool clear=true;
                    for (float fraction : {.25f,.5f,.75f,1.f}) {
                        const Vec3 probe=position+(pass-position)*fraction;
                        const auto paved=[&](Vec3 sample) { return on_leg(leg,sample,p.distance) || on_leg(legs_[p.next],sample,0); };
                        if (!paved(probe+across*(car.body_size().GetX()/2)) || !paved(probe-across*(car.body_size().GetX()/2))) clear=false;
                    }
                    for (float edge : {-car.body_size().GetX()/2,0.f,car.body_size().GetX()/2})
                        if (world_.camera_fraction(position+Vec3(0,.7f,0)+across*edge,pass-position,car.body_id())<.98f) clear=false;
                    const auto occupied=[&](const Car& other) {
                        if (&other==&car || !other.simulated()) return false;
                        const Vec3 future=other.position()+other.velocity()*std::min(1.f,look/std::max(speed,4.f));
                        const Vec3 d=flat(pass-position); const float t=std::clamp(flat(future-position).Dot(d)/std::max(d.LengthSq(),.001f),0.f,1.f);
                        return std::abs(future.GetY()-position.GetY())<3 && flat(future-position-d*t).LengthSq()<clearance*clearance;
                    };
                    if (occupied(*cars_[0])) clear=false;
                    for (const auto& other : traffic_.cars()) if (other.car.get()!=blocker && occupied(*other.car)) clear=false;
                    if (!clear) continue;
                    target=pass; curvature=2*flat(target-position).Dot(-side(direction))/std::max(flat(target-position).LengthSq(),1.f);
                    pass_blockers_[i]=blocker; pass_offsets_[i]=offset; passing=true; break;
                }
                if (!passing) {
                    const float other_speed=std::max(0.f,flat(blocker->velocity()).Dot(road_direction));
                    const float gap=std::max(0.f,nearest-(car.body_size().GetZ()+blocker->body_size().GetZ())/2-3);
                    desired=std::min(desired,std::min(std::sqrt(other_speed*other_speed+2*5.f*gap),other_speed+gap*1.2f));
                }
            }
            desired=std::min(desired,std::sqrt(grip/std::max(std::abs(curvature),.0001f)));
            const Vec3 approach=flat(target-position).NormalizedOr(direction)*(3+speed*1.2f);
            const float clear=world_.camera_fraction(position+Vec3(0,.7f,0),approach,car.body_id());
            if (clear<.98f) desired=std::min(desired,std::sqrt(2*5.f*std::max(0.f,clear*approach.Length()-car.body_size().GetZ()/2-2)));
            const float steer_limit=std::min(car.tuning().max_steer,std::atan(car.tuning().wheelbase*16/std::max(speed*speed,1.f)));
            inputs_[i]={speed>desired+.5f ? 0.f : std::clamp((desired+(desired-speed)*.8f)/car.tuning().top_speed,0.f,1.f),
                std::clamp(std::atan(car.tuning().wheelbase*curvature)/steer_limit,-1.f,1.f),false,speed>desired+.5f || desired<.1f};
            const float alignment=direction.Dot(flat(target-position).NormalizedOr(road_direction));
            if (alignment<-.5f) turning_back_[i]=true;
            if (turning_back_[i] && alignment>.8f) { turning_back_[i]=false; reversing_[i]=false; }
            if (turning_back_[i]) {
                const Vec3 motion=direction*(reversing_[i] ? -1.f : 1.f), probe=position+motion*(car.body_size().GetZ()/2+2);
                bool room=world_.camera_fraction(position+Vec3(0,.7f,0),motion*4,car.body_id())>.98f;
                for (float edge : {-car.body_size().GetX()/2,car.body_size().GetX()/2})
                    room&=on_leg(leg,probe+side(direction)*edge,p.distance);
                if (!room && speed<.5f) reversing_[i]=!reversing_[i];
                inputs_[i]={!room || speed>2.5f ? 0.f : reversing_[i] ? -2.f/11 : 2.f/car.tuning().top_speed,
                    reversing_[i] ? -1.f : 1.f,false,!room || speed>2.5f};
            }
            stuck_[i]=speed<.5f && desired>2 ? stuck_[i]+.05f : 0;
            if (stuck_[i]>8 && (position-cars_[0]->position()).LengthSq()>35*35) {
                Vec3 recovery=leg.point(p.distance); recovery.SetY(environment_.surface_height(recovery)+car.ride_height());
                const float heading=yaw(leg.direction(p.distance));
                if (car.can_spawn_at(recovery,Quat::sRotation(Vec3::sAxisY(),heading))) { car.reset(recovery,heading); p.previous=recovery; stuck_[i]=0; }
            }
        }
    }
    for (int i = 1; i < 5; ++i) cars_[i]->step(inputs_[i],dt);
}
void Race::finish(float dt) {
    if (countdown_>0 || done() || failed_) return;
    failed_=cars_[0]->destroyed() || environment_.submerged(cars_[0]->position());
    std::vector<int> crossed;
    for (int i = 0; i < 5; ++i) {
        auto& p=progress_[i]; if (cars_[i]->destroyed()) continue;
        const auto& leg=legs_[(p.next+legs_.size()-1)%legs_.size()]; const Vec3 current=cars_[i]->position();
        p.distance=leg.locate(current-Vec3(0,cars_[i]->ride_height(),0),p.distance);
        const Vec3 gate=leg.points.back(), direction=leg.direction(leg.length());
        const float before=flat(p.previous-gate).Dot(direction), after=flat(current-gate).Dot(direction);
        if (before<=0 && after>=0 && after>before && std::abs(current.GetY()-gate.GetY())<3) {
            const float t=-before/(after-before); const Vec3 crossing=p.previous+(current-p.previous)*t;
            if (std::abs(flat(crossing-gate).Dot(side(direction)))<=leg.width(leg.length())/2 && p.distance>leg.length()-12) {
                if (p.next==0 && !p.finished) ++p.lap;
                p.next=(p.next+1)%int(legs_.size()); p.distance=0;
                if (!p.finished && p.lap>=map_.laps) { p.finished=true; p.time=elapsed_-dt*(1-t); crossed.push_back(i); }
            }
        }
        p.previous=current;
    }
    std::sort(crossed.begin(),crossed.end(),[&](int a,int b){return progress_[a].time<progress_[b].time;});
    for (int i : crossed) progress_[i].place=++finishers_;
}
int Race::position() const {
    if (progress_[0].finished) return progress_[0].place;
    const auto score=[&](const RaceProgress& p) {
        float total=0; for (const auto& leg : legs_) total+=leg.length();
        float distance=p.lap*total+p.distance; const int leg=(p.next+int(legs_.size())-1)%int(legs_.size());
        for (int i = 0; i < leg; ++i) distance+=legs_[i].length();
        return distance;
    };
    int place=1; for (int i = 1; i < 5; ++i) if (progress_[i].finished || score(progress_[i])>score(progress_[0])) ++place; return place;
}
} // namespace ambaretto
