#include "city.hpp"
#include "race.hpp"
#include "environment.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <istream>
#include <limits>
#include <ostream>
#include <set>

namespace ambaretto {
namespace {
float cross(Vec3 a,Vec3 b) {return a.GetX()*b.GetZ()-a.GetZ()*b.GetX();}
bool finite(Vec3 p) {return std::isfinite(p.GetX()) && std::isfinite(p.GetY()) && std::isfinite(p.GetZ());}
bool fail(std::string& error,const char* text) {error=text; return false;}
std::vector<float> cuts(Vec3 a,Vec3 b,const std::vector<std::array<Vec3,2>>& edges) {
    std::vector<float> result{0,1};
    for (const auto& edge : edges) {
        const Vec3 c=edge[0], d=edge[1]; const float denominator=cross(b-a,d-c);
        if (std::max(a.GetX(),b.GetX())+.0001f<std::min(c.GetX(),d.GetX()) || std::min(a.GetX(),b.GetX())-.0001f>std::max(c.GetX(),d.GetX())
            || std::max(a.GetZ(),b.GetZ())+.0001f<std::min(c.GetZ(),d.GetZ()) || std::min(a.GetZ(),b.GetZ())-.0001f>std::max(c.GetZ(),d.GetZ())) continue;
        if (std::abs(denominator)<.00001f) {
            if (std::abs(cross(c-a,b-a))>.0001f) continue;
            for (auto p : {c,d}) result.push_back(std::clamp((p-a).Dot(b-a)/std::max((b-a).LengthSq(),.000001f),0.f,1.f));
        } else {
            const float t=cross(c-a,d-c)/denominator, u=cross(c-a,b-a)/denominator;
            if (t>0 && t<1 && u>=0 && u<=1) result.push_back(t);
        }
    }
    std::sort(result.begin(),result.end());
    result.erase(std::unique(result.begin(),result.end(),[](float a,float b){return std::abs(a-b)<.00001f;}),result.end());
    return result;
}
CityPath rectangle(float x0,float z0,float x1,float z1) {
    CityPath path; path.closed=true;
    for (auto p : {Vec3(x0,City::level,z0),Vec3(x1,City::level,z0),Vec3(x1,City::level,z1),Vec3(x0,City::level,z1)}) path.nodes.push_back({p,Vec3::sZero()});
    return path;
}
void write_path(std::ostream& file,const CityPath& path) {
    file<<path.closed<<' '<<path.nodes.size()<<'\n';
    for (const auto& n : path.nodes) file<<n.position.GetX()<<' '<<n.position.GetZ()<<' '<<n.tangent.GetX()<<' '<<n.tangent.GetZ()<<'\n';
}
bool read_path(std::istream& file,CityPath& path,std::string& error,bool area) {
    int closed,count;
    if (!(file>>closed>>count) || closed<0 || closed>1 || count<2 || count>1024) return fail(error,"Invalid path point count.");
    path.closed=closed!=0;
    for (int i=0;i<count;++i) {
        float x,z,tx,tz;
        if (!(file>>x>>z>>tx>>tz)) return fail(error,"Invalid path coordinates.");
        path.nodes.push_back({Vec3(x,City::level,z),Vec3(tx,0,tz)});
    }
    return path.validate(error,area);
}
}
bool City::land_at(Vec3 p) const {
    if (!finite(p) || std::abs(p.GetX())>extent || std::abs(p.GetZ())>extent) return false;
    if (!freeform) return land(cell(p.GetX(),p.GetZ()));
    for (auto i=areas.rbegin();i!=areas.rend();++i) {
        if (i->kind==AreaKind::Texture || p.GetX()<i->low.GetX() || p.GetZ()<i->low.GetZ() || p.GetX()>i->high.GetX() || p.GetZ()>i->high.GetZ()) continue;
        if (polygon_contains(i->boundary,p)) return i->kind==AreaKind::Land;
    }
    return false;
}
bool City::road_at(Vec3 p,float margin) const {
    if (!finite(p)) return false;
    if (!freeform) return road(cell(p.GetX(),p.GetZ()));
    for (const auto& road : paths) for (std::size_t i=1;i<road.centerline.size();++i)
        if (path_distance(p,road.centerline[i-1],road.centerline[i])<=road.width/2+margin) return true;
    return false;
}
std::optional<Vec3> City::snap_road_point(Vec3 point,float radius,int exclude) const {
    if (!finite(point) || !std::isfinite(radius) || radius<=0) return {};
    point.SetY(level);
    std::optional<Vec3> result;
    float best=std::numeric_limits<float>::max();
    const auto consider=[&](Vec3 candidate,float range) {
        const float distance=(candidate-point).LengthSq();
        if (distance<=range*range && distance<=best) {best=distance;result=candidate;}
    };
    for (int r=0;r<int(paths.size());++r) if (r!=exclude)
        for (const auto& node:paths[r].path.nodes) consider(node.position,radius);
    if (result) return result;
    for (int r=0;r<int(paths.size());++r) if (r!=exclude) {
        const auto& points=paths[r].centerline;
        for (std::size_t i=1;i<points.size();++i) {
            const Vec3 delta=points[i]-points[i-1];
            consider(points[i-1]+delta*std::clamp((point-points[i-1]).Dot(delta)/std::max(delta.LengthSq(),.000001f),0.f,1.f),std::max(radius,paths[r].width/2));
        }
    }
    return result;
}
std::vector<std::vector<Vec3>> City::road_visible_parts(std::vector<Vec3> polygon,int exclude) const {
    if (polygon.size()<3) return {};
    Vec3 low=polygon.front(),high=low;
    for (auto p:polygon) {low=Vec3::sMin(low,p);high=Vec3::sMax(high,p);}
    std::vector<std::vector<Vec3>> pieces{std::move(polygon)};
    for (int r=0;r<int(paths.size());++r) if (r!=exclude) for (const auto& cover:paths[r].pavement) {
        Vec3 cover_low=cover.front(),cover_high=cover_low;
        for (auto p:cover) {cover_low=Vec3::sMin(cover_low,p);cover_high=Vec3::sMax(cover_high,p);}
        if (low.GetX()>=cover_high.GetX() || high.GetX()<=cover_low.GetX() || low.GetZ()>=cover_high.GetZ() || high.GetZ()<=cover_low.GetZ()) continue;
        std::vector<std::vector<Vec3>> outside;
        for (auto remaining:pieces) for (std::size_t i=0;i<cover.size() && remaining.size()>=3;++i) {
            const Vec3 a=cover[i],b=cover[(i+1)%cover.size()];
            auto split=split_path_polygon(remaining,a,b);auto& part=split.second;
            float area=0;
            for (std::size_t j=1;j+1<part.size();++j) area+=(part[j]-part[0]).Cross(part[j+1]-part[0]).GetY();
            if (std::abs(area)>.000001f) outside.push_back(std::move(part));
            remaining=std::move(split.first);
        }
        pieces=std::move(outside);
        if (pieces.empty()) return {};
    }
    return pieces;
}
int City::building_at(Vec3 p) const {
    if (!finite(p)) return -1;
    if (!freeform) return building_at(cell(p.GetX(),p.GetZ()));
    for (std::size_t i=0;i<buildings.size();++i) {
        const auto& b=buildings[i]; const Vec3 d=p-b.base(level), half=b.shape().size/2;
        const float angle=(b.yaw+b.rotation*90)*JPH::JPH_PI/180, c=std::cos(angle), s=std::sin(angle);
        if (std::abs(d.GetX()*c-d.GetZ()*s)<=half.GetX() && std::abs(d.GetX()*s+d.GetZ()*c)<=half.GetZ()) return int(i);
    }
    return -1;
}
void City::rebuild_paths() {
    for (auto& a : areas) {
        a.boundary=a.path.sample();
        if (a.boundary.empty()) continue;
        for (std::size_t i=1;i+1<a.boundary.size();) {
            if (path_distance(a.boundary[i],a.boundary[i-1],a.boundary[i+1])<.001f) a.boundary.erase(a.boundary.begin()+i);
            else ++i;
        }
        a.low=a.high=a.boundary.front();
        for (auto p : a.boundary) {a.low=Vec3::sMin(a.low,p); a.high=Vec3::sMax(a.high,p);}
    }
    for (auto& road : paths) {
        road.centerline=road.path.sample();road.pavement.clear();
        const auto left=offset_path(road.centerline,road.width/2),right=offset_path(road.centerline,-road.width/2);
        for (std::size_t i=1;i<left.size();++i) road.pavement.push_back({right[i-1],left[i-1],left[i],right[i]});
        if (!road.path.closed && road.centerline.size()>1) for (auto center:{road.centerline.front(),road.centerline.back()}) {
            std::vector<Vec3> cap;
            for (int i=0;i<32;++i) {const float a=-i*2*JPH::JPH_PI/32;cap.push_back(center+Vec3(std::cos(a),0,std::sin(a))*road.width/2);}
            road.pavement.push_back(std::move(cap));
        }
        for (auto& polygon:road.pavement) {
            float area=0;
            for (std::size_t i=1;i+1<polygon.size();++i) area+=(polygon[i]-polygon[0]).Cross(polygon[i+1]-polygon[0]).GetY();
            if (area<0) std::reverse(polygon.begin(),polygon.end());
        }
    }
    std::vector<std::array<Vec3,2>> edges;
    for (const auto& a : areas) if (a.kind!=AreaKind::Texture) {
        // Straight spans need only their endpoints when finding visible shoreline edges.
        auto points=a.boundary;
        for (std::size_t i=1;i+1<points.size();) {
            if (path_distance(points[i],points[i-1],points[i+1])<.001f) points.erase(points.begin()+i);
            else ++i;
        }
        for (std::size_t i=1;i<points.size();++i) edges.push_back({points[i-1],points[i]});
    }
    shore_edges.clear(); shore_bins.clear();
    for (const auto& edge : edges) {
        const Vec3 a=edge[0], b=edge[1], normal=(b-a).NormalizedOr(Vec3::sAxisX()).Cross(Vec3::sAxisY())*.01f;
        const auto split=cuts(a,b,edges);
        for (std::size_t i=1;i<split.size();++i) {
            const Vec3 from=a+(b-a)*split[i-1], to=a+(b-a)*split[i], middle=(from+to)/2;
            if ((to-from).LengthSq()<.000001f || land_at(middle+normal)==land_at(middle-normal)) continue;
            const int edge_index=int(shore_edges.size()); shore_edges.push_back({from,to});
            const auto low=cell(std::min(from.GetX(),to.GetX())-32,std::min(from.GetZ(),to.GetZ())-32);
            const auto high=cell(std::max(from.GetX(),to.GetX())+32,std::max(from.GetZ(),to.GetZ())+32);
            for (int z=std::max(-4,low.z);z<=std::min(width+3,high.z);++z)
                for (int x=std::max(-4,low.x);x<=std::min(width+3,high.x);++x) shore_bins[z*(width+8)+x].push_back(edge_index);
        }
    }
    std::vector<PathRoadSegment> raw;
    edges.clear(); road_graph.clear();
    for (const auto& r : paths) for (std::size_t i=1;i<r.centerline.size();++i) {raw.push_back({r.centerline[i-1],r.centerline[i],r.width});edges.push_back({r.centerline[i-1],r.centerline[i]});}
    std::map<std::array<int,4>,std::size_t> seen;
    for (const auto& segment : raw) {
        const auto split=cuts(segment.a,segment.b,edges);
        for (std::size_t i=1;i<split.size();++i) {
            const Vec3 a=segment.a+(segment.b-segment.a)*split[i-1],b=segment.a+(segment.b-segment.a)*split[i];
            if ((b-a).LengthSq()<.000001f) continue;
            std::array<int,2> ka{int(std::round(a.GetX()*1000)),int(std::round(a.GetZ()*1000))},kb{int(std::round(b.GetX()*1000)),int(std::round(b.GetZ()*1000))};
            if (ka>kb) std::swap(ka,kb);
            const auto inserted=seen.emplace(std::array<int,4>{ka[0],ka[1],kb[0],kb[1]},road_graph.size());
            if (inserted.second) road_graph.push_back({a,b,segment.width});
            else road_graph[inserted.first->second].width=std::max(road_graph[inserted.first->second].width,segment.width);
        }
    }
    for(auto& b:buildings)if(b.position && finite(*b.position))b.position->SetY(terrain_height(b.position->GetX(),b.position->GetZ()));
    for(auto& v:vehicles)if(finite(v.position))v.position.SetY(height(v.position.GetX(),v.position.GetZ()));
    if(spawn && finite(*spawn))spawn->SetY(height(spawn->GetX(),spawn->GetZ()));
    for(auto& t:trees)if(finite(t.base))t.base.SetY(height(t.base.GetX(),t.base.GetZ()));
}
float City::shore_distance(Vec3 p) const {
    if (!finite(p) || std::abs(p.GetX())>extent+32 || std::abs(p.GetZ())>extent+32) return 32;
    float distance=32;
    const auto c=cell(p.GetX(),p.GetZ()); const auto bin=shore_bins.find(c.z*(width+8)+c.x);
    if (bin!=shore_bins.end()) for (int i : bin->second) distance=std::min(distance,path_distance(p,shore_edges[i][0],shore_edges[i][1]));
    return distance;
}
float City::terrain_height(float x,float z) const {
    const Vec3 p(x,level,z);
    const float distance=shore_distance(p);
    const auto smooth=[](float t){t=std::clamp(t,0.f,1.f); return t*t*(3-2*t);};
    return land_at(p) ? Environment::water_level : -8*smooth(distance/32);
}
bool City::add_area(CityArea area,std::string& error,int replace) {
    if (int(area.kind)<0 || int(area.kind)>2 || !valid_texture_filename(area.texture)) return fail(error,"Invalid area type or texture filename.");
    if (!area.path.validate(error,true)) return false;
    if (replace<-1 || replace>=int(areas.size()) || (replace<0 && areas.size()>=512)) return fail(error,"Area limit reached (512).");
    City next=*this; next.freeform=true;
    if (replace>=0) next.areas[replace]=std::move(area); else next.areas.push_back(std::move(area));
    next.rebuild_paths(); next.clear_trees();
    if (!next.validate(error)) return false;
    *this=std::move(next); return true;
}
bool City::add_path_road(PathRoad road,std::string& error,int replace) {
    if (!std::isfinite(road.width) || road.width<4 || road.width>40) return fail(error,"Road width must be 4-40 metres.");
    if (!road.path.validate(error,false)) return false;
    if (replace<-1 || replace>=int(paths.size()) || (replace<0 && paths.size()>=512)) return fail(error,"Road limit reached (512).");
    City next=*this; next.freeform=true;
    if (replace>=0) next.paths[replace]=std::move(road); else next.paths.push_back(std::move(road));
    next.rebuild_paths();
    next.clear_trees();
    if (!next.validate(error)) return false;
    *this=std::move(next); return true;
}
bool City::place_building(CityBuilding b,std::string& error,int replace) {
    if (!b.position || !finite(*b.position) || std::abs(b.position->GetX())>=extent || std::abs(b.position->GetZ())>=extent || !std::isfinite(b.yaw) || b.yaw<0 || b.yaw>=360 || b.rotation<0 || b.rotation>3)
        return fail(error,"Place a building inside the map using a valid rotation.");
    if (b.mesh && !b.mesh->validate(error)) return false;
    if (!b.mesh && (b.size<1 || b.size>8 || b.height<4 || b.height>120)) return fail(error,"Invalid building dimensions.");
    if (replace<-1 || replace>=int(buildings.size()) || (replace<0 && buildings.size()>=4096)) return fail(error,"Building limit reached.");
    b.position->SetY(terrain_height(b.position->GetX(),b.position->GetZ())); b.cell=cell(b.position->GetX(),b.position->GetZ());
    // Free placement deliberately permits overlap, like vehicles; physics uses the exact rotated mesh.
    for (Vec3 p : b.points(level)) if (std::abs(p.GetX())>=extent || std::abs(p.GetZ())>=extent) return fail(error,"Keep the whole building inside the map.");
    if (!land_at(*b.position)) return fail(error,"Place the building on land.");
    if (replace>=0) buildings[replace]=std::move(b); else buildings.push_back(std::move(b));
    clear_trees(); error.clear(); return true;
}
bool City::validate_paths(std::string& error) const {
    if (areas.size()>512 || paths.size()>512) return fail(error,"Too many paths or areas.");
    std::size_t total_nodes=0,total_samples=0;
    for(const auto& a:areas){total_nodes+=a.path.nodes.size();total_samples+=a.path.sample().size();}
    for(const auto& r:paths){total_nodes+=r.path.nodes.size();total_samples+=r.centerline.size();}
    if(total_nodes>16384 || total_samples>65536)return fail(error,"City paths exceed the geometry limit (16384 nodes / 65536 samples).");
    for (const auto& a : areas) if (int(a.kind)<0 || int(a.kind)>2 || !valid_texture_filename(a.texture) || !a.path.validate(error,true)) return fail(error,"Invalid terrain area or texture.");
    for (const auto& r : paths) if (!std::isfinite(r.width) || r.width<4 || r.width>40 || !r.path.validate(error,false)) return fail(error,"Invalid road path or width.");
    for(const auto& r:paths) {
        const float half=r.width/2+(road_style.sidewalks ? 1.3f : 0);
        for(auto p:r.centerline)if(std::abs(p.GetX())+half>=extent || std::abs(p.GetZ())+half>=extent)return fail(error,"Keep the whole road footprint inside the map.");
        for(const auto& edge:{offset_path(r.centerline,half),offset_path(r.centerline,-half)})for(auto p:edge)
            if(std::abs(p.GetX())>=extent || std::abs(p.GetZ())>=extent)return fail(error,"Keep curved road corners inside the map.");
    }
    return true;
}
std::vector<PathRoadSegment> City::path_road_segments() const {
    return road_graph;
}
std::vector<std::vector<Vec3>> City::path_traffic_routes(float curb_inset) const {
    const auto segments=path_road_segments();
    std::vector<Vec3> points; std::vector<std::vector<int>> links;
    std::map<std::pair<int,int>,int> indices;
    const auto add=[&](Vec3 p) {auto inserted=indices.emplace(std::make_pair(int(std::round(p.GetX()*1000)),int(std::round(p.GetZ()*1000))),int(points.size())); if(inserted.second){points.push_back(p);links.emplace_back();} return inserted.first->second;};
    std::vector<std::array<int,2>> ends;
    for (const auto& s : segments) {const int a=add(s.a),b=add(s.b),i=int(ends.size()); ends.push_back({a,b}); links[a].push_back(i); links[b].push_back(i);}
    std::set<std::pair<int,int>> visited;
    std::vector<std::vector<Vec3>> result;
    for (int edge=0;edge<int(segments.size());++edge) for (int start : ends[edge]) {
        if (visited.count({edge,start})) continue;
        std::vector<Vec3> route; int current=start, next_edge=edge;
        for (std::size_t step=0;step<segments.size()*2+1;++step) {
            if (!visited.insert({next_edge,current}).second) break;
            const int next=ends[next_edge][0]==current ? ends[next_edge][1] : ends[next_edge][0];
            const Vec3 direction=(points[next]-points[current]).Normalized(), right=direction.Cross(Vec3::sAxisY());
            const float offset=curb_inset<2 && road_style.sidewalks ? segments[next_edge].width/2+.65f : std::max(.3f,segments[next_edge].width/2-curb_inset);
            route.push_back(points[current]+right*offset); route.push_back(points[next]+right*offset);
            auto outgoing=links[next];
            std::sort(outgoing.begin(),outgoing.end(),[&](int a,int b){
                const auto delta=[&](int e){return points[ends[e][0]==next ? ends[e][1] : ends[e][0]]-points[next];};
                const Vec3 da=delta(a),db=delta(b);return std::atan2(da.GetZ(),da.GetX())<std::atan2(db.GetZ(),db.GetX());
            });
            const auto incoming=std::find(outgoing.begin(),outgoing.end(),next_edge);
            const int choice=outgoing[(std::size_t(incoming-outgoing.begin())+1)%outgoing.size()];
            if(choice==next_edge)for(int i=1;i<=12;++i) {
                const float angle=i*JPH::JPH_PI/12;
                route.push_back(points[next]+(right*std::cos(angle)+direction*std::sin(angle))*offset);
            }
            current=next; next_edge=choice;
            if (current==start && next_edge==edge) break;
        }
        route.erase(std::unique(route.begin(),route.end(),[](Vec3 a,Vec3 b){return (a-b).LengthSq()<.000001f;}),route.end());
        if(route.size()>1 && (route.front()-route.back()).LengthSq()<.000001f)route.pop_back();
        if (route.size()>=4) result.push_back(std::move(route));
    }
    return result;
}
void City::make_freeform(bool smooth_coast) {
    if (freeform) return;
    const auto network=road_network();
    for (auto& b : buildings) {b.position=b.base(level); b.yaw=float(b.rotation*90); b.rotation=0;}
    // Trace tile boundaries once when opening an older map; editing and saves then use paths.
    using Key=std::pair<int,int>;
    std::multimap<Key,Key> edges;
    for (int z=0;z<width;++z) for (int x=0;x<width;++x) if (land({x,z})) {
        if (!land({x,z-1})) edges.emplace(Key{x,z},Key{x+1,z});
        if (!land({x+1,z})) edges.emplace(Key{x+1,z},Key{x+1,z+1});
        if (!land({x,z+1})) edges.emplace(Key{x+1,z+1},Key{x,z+1});
        if (!land({x-1,z})) edges.emplace(Key{x,z+1},Key{x,z});
    }
    std::vector<std::pair<float,CityArea>> contours;
    while (!edges.empty()) {
        CityPath path; path.closed=true;
        const Key start=edges.begin()->first; Key p=start;
        do {
            const auto found=edges.find(p); if(found==edges.end()) break;
            const Key next=found->second; edges.erase(found);
            path.nodes.push_back({Vec3((p.first-width/2)*block,level,(p.second-width/2)*block),Vec3::sZero()});
            p=next;
        } while (p!=start);
        std::vector<CityPath> pending{std::move(path)};
        while(!pending.empty()) {
            auto outline=std::move(pending.back());pending.pop_back();
            if(outline.nodes.size()<3)continue;
            std::map<Key,std::size_t> visited;bool split=false;
            for(std::size_t i=0;i<outline.nodes.size();++i) {
                const auto point=outline.nodes[i].position;
                const Key key{int(std::lround(point.GetX()/block)),int(std::lround(point.GetZ()/block))};
                const auto entry=visited.emplace(key,i);
                if(entry.second)continue;
                // Raster coastlines can touch at one vertex; retain each closed contour independently.
                CityPath loop;loop.closed=true;
                loop.nodes.assign(outline.nodes.begin()+entry.first->second,outline.nodes.begin()+i);
                outline.nodes.erase(outline.nodes.begin()+entry.first->second,outline.nodes.begin()+i);
                pending.push_back(std::move(loop));pending.push_back(std::move(outline));split=true;break;
            }
            if(split)continue;
            for (std::size_t i=0;i<outline.nodes.size() && outline.nodes.size()>3;) {
                const auto& n=outline.nodes; const auto count=n.size();
                if (std::abs(cross(n[i].position-n[(i+count-1)%count].position,n[(i+1)%count].position-n[i].position))<.00001f) outline.nodes.erase(outline.nodes.begin()+i);
                else ++i;
            }
            float signed_area=0;
            for(std::size_t i=0;i<outline.nodes.size();++i)signed_area+=cross(outline.nodes[i].position,outline.nodes[(i+1)%outline.nodes.size()].position);
            if (smooth_coast) {
                CityPath rounded; rounded.closed=true;
                const auto& nodes=outline.nodes;
                // Leave room for both Bezier handles between corners on one-tile edges.
                for (std::size_t i=0;i<nodes.size();++i) {
                    const Vec3 p=nodes[i].position, before=p-nodes[(i+nodes.size()-1)%nodes.size()].position, after=nodes[(i+1)%nodes.size()].position-p;
                    const float radius=std::min({block*.35f,before.Length()*.35f,after.Length()*.35f});
                    rounded.nodes.push_back({p-before.Normalized()*radius,before.Normalized()*(radius*.55228475f)});
                    rounded.nodes.push_back({p+after.Normalized()*radius,after.Normalized()*(radius*.55228475f)});
                }
                outline=std::move(rounded);
            }
            CityArea area;area.path=std::move(outline);area.kind=signed_area>0 ? AreaKind::Land : AreaKind::Water;
            contours.push_back({std::abs(signed_area),std::move(area)});
        }
    }
    std::sort(contours.begin(),contours.end(),[](const auto& a,const auto& b){return a.first>b.first;});
    for (auto& a : contours) areas.push_back(std::move(a.second));
    for (const auto& node : network) {
        auto points=node.path();
        if (!points.empty()) {PathRoad r; for(auto p : points) r.path.nodes.push_back({p,Vec3::sZero()}); paths.push_back(std::move(r));}
        else for(int d=0;d<4;++d) if(node.mask&(1u<<d)) {PathRoad r; r.path.nodes={{node.center(),Vec3::sZero()},{node.port(d).center,Vec3::sZero()}}; paths.push_back(std::move(r));}
        for (int adjacent : node.edges) if (adjacent>int(&node-network.data())) {
            const auto& other=network[adjacent]; float best=std::numeric_limits<float>::max(); Vec3 a,b;
            for (int d=0;d<4;++d) if(node.mask&(1u<<d)) for(int e=0;e<4;++e) if(other.mask&(1u<<e)) {
                const auto p=node.port(d).center,q=other.port(e).center;
                if((p-q).LengthSq()<best) {best=(p-q).LengthSq();a=p;b=q;}
            }
            if(best>.0001f && best<block*block*4) {PathRoad r; r.path.nodes={{a,Vec3::sZero()},{b,Vec3::sZero()}};paths.push_back(std::move(r));}
        }
    }
    // Preserve painted rectangles as independent filled areas during conversion.
    for(int z=0;z<width;++z) for(int x=0;x<width;++x) {
        if(!land({x,z}) || ground_texture({x,z})=="soil.png") continue;
        const std::string texture=ground_texture({x,z}); const int first=x;
        while(x+1<width && land({x+1,z}) && ground_texture({x+1,z})==texture) ++x;
        CityArea a; a.kind=AreaKind::Texture;a.texture=texture;a.path=rectangle((first-width/2)*block,(z-width/2)*block,(x+1-width/2)*block,(z+1-width/2)*block);areas.push_back(std::move(a));
    }
    freeform=true; elevation.fill(0); rebuild_paths();
    for(auto& v : vehicles) v.position.SetY(height(v.position.GetX(),v.position.GetZ()));
    if(spawn) spawn->SetY(height(spawn->GetX(),spawn->GetZ()));
    for(auto& b : buildings) b.position->SetY(terrain_height(b.position->GetX(),b.position->GetZ()));
    for(auto& t : trees) t.base.SetY(height(t.base.GetX(),t.base.GetZ()));
    clear_trees();
}
bool City::write_paths(std::ostream& file,int mesh_version) const {
    file<<areas.size()<<'\n';
    for(const auto& a : areas) {file<<int(a.kind)<<' '<<std::quoted(a.texture)<<'\n';write_path(file,a.path);}
    file<<paths.size()<<'\n';
    for(const auto& r : paths) {file<<r.width<<'\n';write_path(file,r.path);}
    file<<buildings.size()<<'\n';
    for(const auto& b : buildings) {file<<b.base(level).GetX()<<' '<<b.base(level).GetZ()<<' '<<b.yaw<<' '<<b.rotation<<' '<<b.size<<' '<<b.height<<' '<<bool(b.mesh)<<'\n';if(b.mesh)b.mesh->write(file,mesh_version);}
    file<<vehicles.size()<<'\n';
    for(const auto& v : vehicles) {file<<int(v.kind)<<' '<<v.position.GetX()<<' '<<v.position.GetZ()<<' '<<v.rotation<<' '<<bool(v.car)<<'\n';if(v.car)v.car->write(file);}
    file<<bool(spawn)<<' '<<(spawn ? spawn->GetX() : 0)<<' '<<(spawn ? spawn->GetZ() : 0)<<'\n';
    file<<start_minutes<<' '<<pedestrian_density<<' '<<bool(player_character)<<'\n'; if(player_character)player_character->write(file);
    file<<std::quoted(road_style.road_texture)<<' '<<road_style.sidewalks<<' '<<std::quoted(road_style.sidewalk_texture)<<'\n';
    file<<trees.size()<<'\n';for(const auto& t : trees)file<<t.base.GetX()<<' '<<t.base.GetZ()<<' '<<t.height<<' '<<t.yaw<<'\n';
    file<<races.size()<<'\n';for(const auto& r : races){file<<std::quoted(r.name)<<' '<<r.laps<<' '<<r.checkpoints.size()<<'\n';for(auto p:r.checkpoints)file<<p.GetX()<<' '<<p.GetZ()<<'\n';}
    return bool(file);
}
bool City::read_paths(std::istream& file,City& city,std::string& error,int mesh_version) {
    city.freeform=true;
    int count;
    std::size_t total_nodes=0,total_samples=0;
    const auto count_to=[&](int maximum){return bool(file>>count) && count>=0 && count<=maximum;};
    const auto position_valid=[](float x,float z){return std::isfinite(x) && std::isfinite(z) && std::abs(x)<extent && std::abs(z)<extent;};
    const auto budget=[&](const CityPath& path){
        total_nodes+=path.nodes.size(); total_samples+=path.sample().size();
        return total_nodes<=16384 && total_samples<=65536;
    };
    if(!count_to(512))return fail(error,"Invalid area count.");
    for(int i=0,n=count;i<n;++i) {
        CityArea area; int kind;
        if(!(file>>kind>>std::quoted(area.texture)) || kind<0 || kind>2 || !valid_texture_filename(area.texture))return fail(error,"Invalid area data.");
        if(!read_path(file,area.path,error,true))return false;
        if(!budget(area.path))return fail(error,"City path geometry limit exceeded.");
        area.kind=AreaKind(kind); city.areas.push_back(std::move(area));
    }
    if(!count_to(512))return fail(error,"Invalid road count.");
    for(int i=0,n=count;i<n;++i) {
        PathRoad road;
        if(!(file>>road.width) || !std::isfinite(road.width) || road.width<4 || road.width>40)return fail(error,"Invalid road width.");
        if(!read_path(file,road.path,error,false))return false;
        if(!budget(road.path))return fail(error,"City path geometry limit exceeded.");
        city.paths.push_back(std::move(road));
    }
    city.rebuild_paths();
    if(!count_to(4096))return fail(error,"Invalid building count.");
    for(int i=0,n=count;i<n;++i) {
        CityBuilding building; float x,z; int mesh;
        if(!(file>>x>>z>>building.yaw>>building.rotation>>building.size>>building.height>>mesh) || mesh<0 || mesh>1 || !position_valid(x,z))return fail(error,"Invalid building data.");
        building.position=Vec3(x,city.terrain_height(x,z),z); building.cell=cell(x,z);
        if(mesh) {building.mesh.emplace(); if(!BuildingMesh::read(file,*building.mesh,error,mesh_version))return false;}
        city.buildings.push_back(std::move(building));
    }
    if(!count_to(512))return fail(error,"Invalid vehicle count.");
    for(int i=0,n=count;i<n;++i) {
        CityVehicle vehicle; float x,z; int kind,car;
        if(!(file>>kind>>x>>z>>vehicle.rotation>>car) || kind<0 || kind>3 || car<0 || car>1 || !position_valid(x,z))return fail(error,"Invalid vehicle data.");
        vehicle.kind=CityVehicleKind(kind); vehicle.position=Vec3(x,city.height(x,z),z);
        if(car) {vehicle.car.emplace(); if(!CarDesign::read(file,*vehicle.car,error))return false;}
        city.vehicles.push_back(std::move(vehicle));
    }
    int present; float x,z;
    if(!(file>>present>>x>>z) || present<0 || present>1 || !position_valid(x,z))return fail(error,"Invalid spawn data.");
    if(present)city.spawn=Vec3(x,city.height(x,z),z);
    if(!(file>>city.start_minutes>>city.pedestrian_density>>present) || present<0 || present>1)return fail(error,"Invalid city settings.");
    if(present) {city.player_character.emplace(); if(!CharacterDesign::read(file,*city.player_character,error))return false;}
    if(!(file>>std::quoted(city.road_style.road_texture)>>present>>std::quoted(city.road_style.sidewalk_texture)) || present<0 || present>1)return fail(error,"Invalid road style.");
    city.road_style.sidewalks=present!=0;
    if(!count_to(8192))return fail(error,"Invalid tree count.");
    for(int i=0,n=count;i<n;++i) {
        float height,yaw;
        if(!(file>>x>>z>>height>>yaw) || !position_valid(x,z))return fail(error,"Invalid tree data.");
        city.trees.push_back({Vec3(x,city.height(x,z),z),height,yaw});
    }
    if(!count_to(128))return fail(error,"Invalid race count.");
    for(int i=0,n=count;i<n;++i) {
        RaceMap race; int points;
        if(!(file>>std::quoted(race.name)>>race.laps>>points) || points<2 || points>256)return fail(error,"Invalid race data.");
        for(int j=0;j<points;++j) {
            if(!(file>>x>>z) || !position_valid(x,z))return fail(error,"Invalid checkpoint.");
            race.checkpoints.emplace_back(x,city.height(x,z),z);
        }
        city.races.push_back(std::move(race));
    }
    return true;
}
void Environment::build_freeform_city(const City& city) {
    const auto triangle=[&](Vec3 a,Vec3 b,Vec3 c,Surface surface,bool deck=false) {
        if ((b-a).Cross(c-a).LengthSq()<.000001f) return;
        if ((b-a).Cross(c-a).GetY()<0) std::swap(b,c);
        const auto first=std::uint32_t(vertices_.size());
        vertices_.insert(vertices_.end(),{a,b,c}); triangles_.push_back({first,first+1,first+2,surface,deck});
    };
    const auto quad=[&](Vec3 a,Vec3 b,Vec3 c,Vec3 d,Surface surface,bool deck=false) {triangle(a,b,c,surface,deck);triangle(a,c,d,surface,deck);};
    quad({-extent,-8,-extent},{-extent,-8,extent},{extent,-8,extent},{extent,-8,-extent},Surface::Seabed);
    city_heights_.resize(city_mesh_width*city_mesh_width);
    for (int z=0;z<city_mesh_width;++z) for (int x=0;x<city_mesh_width;++x)
        city_heights_[z*city_mesh_width+x]=city.terrain_height(x*city_mesh_spacing-city_mesh_extent,z*city_mesh_spacing-city_mesh_extent);
    const auto point=[&](int x,int z){return Vec3(x*city_mesh_spacing-city_mesh_extent,city_heights_[z*city_mesh_width+x],z*city_mesh_spacing-city_mesh_extent);};
    for (int z=0;z+1<city_mesh_width;++z) for (int x=0;x+1<city_mesh_width;++x) {
        const auto a=point(x,z), b=point(x,z+1), c=point(x+1,z+1), d=point(x+1,z);
        if(std::max({a.GetY(),b.GetY(),c.GetY(),d.GetY()})<=-8)continue;
        const int first=int(triangles_.size());
        std::vector<std::vector<Vec3>> pieces{{a,b,c},{a,c,d}};
        const auto middle=City::cell((a.GetX()+c.GetX())/2,(a.GetZ()+c.GetZ())/2);
        const auto nearby=city.shore_bins.find(middle.z*(City::width+8)+middle.x);
        bool shore=false;
        if(nearby!=city.shore_bins.end()) for(int edge_index:nearby->second) {
            const auto edge=city.shore_edges[edge_index];const Vec3 from=edge[0],to=edge[1];
            if(std::min(from.GetX(),to.GetX())>c.GetX() || std::max(from.GetX(),to.GetX())<a.GetX()
                || std::min(from.GetZ(),to.GetZ())>c.GetZ() || std::max(from.GetZ(),to.GetZ())<a.GetZ())continue;
            shore=true;std::vector<std::vector<Vec3>> split;
            for(const auto& polygon:pieces) {
                std::vector<Vec3> inside,outside;Vec3 previous=polygon.back();float before=cross(to-from,previous-from);
                for(auto current:polygon) {
                    const float after=cross(to-from,current-from);
                    if((before>=0)!=(after>=0)){const Vec3 p=previous+(current-previous)*(before/(before-after));inside.push_back(p);outside.push_back(p);}
                    if(after>=0)inside.push_back(current);
                    if(after<=0)outside.push_back(current);
                    previous=current;before=after;
                }
                if(inside.size()>=3)split.push_back(std::move(inside));
                if(outside.size()>=3)split.push_back(std::move(outside));
            }
            pieces=std::move(split);
        }
        for(auto& polygon:pieces) {
            if(!shore){triangle(polygon[0],polygon[1],polygon[2],Surface::Soil);continue;}
            Vec3 center=Vec3::sZero();
            for(auto& p:polygon){p.SetY(city.terrain_height(p.GetX(),p.GetZ()));center+=p;}
            center/=float(polygon.size());center.SetY(city.terrain_height(center.GetX(),center.GetZ()));
            for(std::size_t i=0;i<polygon.size();++i)triangle(center,polygon[i],polygon[(i+1)%polygon.size()],Surface::Soil);
        }
        auto& indices=city_surface_cells_[z*city_mesh_width+x];
        for(int i=first;i<int(triangles_.size());++i)indices.push_back(i);
    }
    for (const auto& area : city.areas) if (area.kind==AreaKind::Land)
        city_islands_.push_back({(area.low+area.high)/2,(area.high.GetX()-area.low.GetX())/2,(area.high.GetZ()-area.low.GetZ())/2,city_->name.c_str()});
    for (int r=0;r<int(city.paths.size());++r) {
        const auto& road=city.paths[r];
        const float deck_width=road.width+(city.road_style.sidewalks ? 2.6f : 0);
        const auto left=offset_path(road.centerline,deck_width/2),right=offset_path(road.centerline,-deck_width/2);
        for(std::size_t i=1;i<road.centerline.size();++i) {
            const Vec3 a=road.centerline[i-1],b=road.centerline[i];
            const bool water=!city.land_at((a+b)/2);
            city_roads_.push_back({a,b,road.width,"CITY STREET",water ? int(city_bridges_.size()) : -1});
            quad(left[i-1],left[i],right[i],right[i-1],Surface::Road,true);
            if (!water) continue;
            city_bridges_.push_back({a,b,deck_width,City::level,"CITY BRIDGE"});
            for(const auto& edge : {left,right}) {
                const Vec3 delta=edge[i]-edge[i-1];
                const Vec3 direction=delta.Normalized(),side=direction.Cross(Vec3::sAxisY())*.08f;
                for (const auto& part:city.road_visible_parts({edge[i-1]-side,edge[i-1]+side,edge[i]+side,edge[i]-side},r)) {
                    float low=delta.Length(),high=0;
                    for (auto p:part) {const float t=(p-edge[i-1]).Dot(direction);low=std::min(low,t);high=std::max(high,t);}
                    if (high-low>.001f) barriers_.push_back({edge[i-1]+direction*((low+high)/2)+Vec3(0,.45f,0),Vec3(.16f,.85f,high-low),std::atan2(delta.GetX(),delta.GetZ())});
                }
            }
        }
        if(!road.path.closed) for(auto center:{road.centerline.front(),road.centerline.back()}) for(int i=0;i<32;++i) {
            const float a=i*2*JPH::JPH_PI/32,b=(i+1)*2*JPH::JPH_PI/32;
            triangle(center,center+Vec3(std::cos(a),0,std::sin(a))*deck_width/2,center+Vec3(std::cos(b),0,std::sin(b))*deck_width/2,Surface::Road,true);
        }
    }
    for (auto route : city.path_traffic_routes(2.38f)) city_streets_.push_back({std::move(route),9.8f,"CITY STREET",true,8,3,2.2f});
    for (const auto& b : city.buildings) {
        const auto points=b.points(City::level); Vec3 low=points.front(),high=low;
        for(auto p : points){low=Vec3::sMin(low,p);high=Vec3::sMax(high,p);}
        buildings_.push_back({(low+high)/2,high-low,0,BuildingKind::Office});
    }
    trees_=city.trees;
}
} // namespace ambaretto
