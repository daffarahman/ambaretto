#include "mesh_streaming.hpp"
#include "view_frustum.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace ambaretto;
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
struct Models {
    std::vector<Model> items;
    ~Models() { for (auto& model : items) UnloadModel(model); }
    void add(float x,int triangles = 1) {
        Mesh mesh{}; mesh.vertexCount = triangles*3; mesh.triangleCount = triangles;
        mesh.vertices = static_cast<float*>(MemAlloc(unsigned(mesh.vertexCount*3*sizeof(float))));
        require(mesh.vertices != nullptr,"Fixture allocation failed");
        for (int i = 0; i < mesh.vertexCount; ++i) {
            mesh.vertices[i*3] = x+(i%3 == 1); mesh.vertices[i*3+1] = 0; mesh.vertices[i*3+2] = i%3 == 2;
        }
        items.push_back(LoadModelFromMesh(mesh));
    }
};
bool update(MeshStreaming& stream,Vector3 focus,float distance = 100) {
    MeshUploadBudget budget;
    const bool ready = stream.update(focus,distance,budget);
    require(budget.meshes <= 4,"Streaming exceeded its per-frame mesh limit");
    return ready;
}
void residency() {
    Models models;
    for (int i = 7; i >= 0; --i) models.add(float(i*10));
    models.add(5000); models.add(-5000);
    MeshStreaming stream;
    for (auto& model : models.items) stream.add(model);
    const auto initial = stream.stats();
    require(initial.chunks == 10 && initial.resident_chunks == 0,"Distant geometry was uploaded eagerly");
    require(!update(stream,{0,3000,0}) && stream.stats().resident_chunks == 4 && stream.stats().pending_chunks == 4,
        "Initial upload ignored its budget or treated flight altitude as distance");
    for (int i = 0; i < 8; ++i)
        require(mesh_resident(models.items[i]) == (i >= 4),"Streaming did not upload nearest chunks first");
    require(update(stream,{0,0,0}) && stream.stats().resident_chunks == 8,"Pending geometry never completed");
    auto& near = models.items.back();
    auto* vertices = near.meshes[0].vertices;
    require(update(stream,{-5000,0,0}) && mesh_resident(near),"Negative world coordinates did not stream");
    const auto buffer = near.meshes[0].vboId[0];
    const auto vao = near.meshes[0].vaoId;
    require(update(stream,{-5500,0,0}) && near.meshes[0].vaoId == vao,"Chunk-boundary movement reloaded retained geometry");
    require(update(stream,{-5700,0,0}) && stream.stats().resident_chunks == 0,"Far meshes stayed resident");
    const auto is_buffer = reinterpret_cast<unsigned char (*)(unsigned int)>(rlGetProcAddress("glIsBuffer"));
    require(is_buffer && !is_buffer(buffer),"Eviction retained the GPU vertex buffer");
    require(near.meshes[0].vertices == vertices && stream.stats().cpu_geometry_bytes == initial.cpu_geometry_bytes,
        "Eviction destroyed the CPU geometry needed for reloading");
    for (int i = 0; i < 12; ++i) {
        require(update(stream,{i%2 ? -5000.f : 5000.f,0,0}),"Teleport left pending local geometry");
        require(stream.stats().resident_chunks == 1 && stream.stats().resident_geometry_bytes == 36,
            "Repeated travel accumulated resident meshes");
    }
    std::cout << "Travel fixture: " << initial.cpu_geometry_bytes << " CPU bytes, "
        << stream.stats().resident_geometry_bytes << " resident GPU bytes\n";
}
void upload_bytes_and_bounds() {
    Models models; models.add(0,40000); models.add(10,40000); models.add(20,70000);
    MeshStreaming stream;
    for (auto& model : models.items) stream.add(model);
    MeshUploadBudget budget;
    require(!stream.update({0,0,0},100,budget) && budget.meshes == 1 && budget.bytes <= MeshUploadBudget::byte_limit,
        "Two ordinary chunks exceeded the byte budget");
    require(!update(stream,{0,0,0}) && update(stream,{0,0,0}),"An oversized mesh could not make forward progress");
    Models wide; wide.add(-1000);
    wide.items[0].meshes[0].vertices[3] = 1000;
    MeshStreaming bounds; bounds.add(wide.items[0]);
    require(update(bounds,{1000,0,0},0) && mesh_resident(wide.items[0]),"Streaming ignored a chunk extending past its cell centre");
    require(update(bounds,{10000,0,0},0) && bounds.stats().resident_chunks == 0,"View-distance reduction retained distant geometry");
}
BoundingBox box(Vector3 center,Vector3 half = {.1f,.1f,.1f}) {
    return {Vector3Subtract(center,half),Vector3Add(center,half)};
}
void camera_visibility() {
    const auto near = rlGetCullDistanceNear(), far = rlGetCullDistanceFar();
    rlSetClipPlanes(.1,100);
    Camera3D camera{{0,0,0},{0,0,-1},{0,1,0},90,CAMERA_PERSPECTIVE};
    BeginMode3D(camera);
    const ViewFrustum perspective;
    require(perspective.visible(box({0,0,-5})),"Visible geometry was culled");
    for (Vector3 p : {Vector3{0,0,5},Vector3{10,0,-5},Vector3{-10,0,-5},Vector3{0,10,-5},Vector3{0,-10,-5},Vector3{0,0,-110}})
        require(!perspective.visible(box(p)),"Off-screen geometry remained visible");
    require(perspective.visible(box({5,0,-5})) && perspective.visible(box({0,0,-.1f})),
        "Geometry crossing a side or near plane disappeared");
    require(!perspective.visible(box({0,0,-.025f},{.01f,.01f,.01f})),"Geometry before the near plane remained visible");
    require(perspective.visible(box({0,0,0},{1000,1000,1000})),"A chunk containing the camera disappeared");
    auto transform = MatrixTranslate(0,0,-5);
    require(perspective.visible(box({0,0,0}),&transform),"Translated model bounds were culled in local space");
    transform = MatrixTranslate(0,0,5);
    require(!perspective.visible(box({0,0,0}),&transform),"A model behind the camera was drawn");
    transform = MatrixMultiply(MatrixScale(-.1f,1,100),MatrixTranslate(0,0,1));
    require(perspective.visible(box({0,0,0}),&transform),"Mirrored or scaled geometry crossing the near plane disappeared");
    transform = MatrixMultiply(MatrixRotateY(PI/2),MatrixTranslate(0,0,1));
    require(perspective.visible(box({0,0,0},{5,.1f,.1f}),&transform),"Rotated geometry was tested with unrotated bounds");
    EndMode3D();
    camera.target = {0,0,1}; BeginMode3D(camera);
    const ViewFrustum turned;
    require(turned.visible(box({0,0,5})) && !turned.visible(box({0,0,-5})),"Turning the camera did not update visibility");
    EndMode3D();
    const auto wide = LoadRenderTexture(160,80);
    require(IsRenderTextureValid(wide),"Camera visibility fixture needs a render target");
    camera.target = {0,0,-1}; camera.projection = CAMERA_ORTHOGRAPHIC; camera.fovy = 10;
    BeginTextureMode(wide); BeginMode3D(camera);
    const ViewFrustum orthographic;
    require(orthographic.visible(box({9,0,-5})) && !orthographic.visible(box({11,0,-5}))
        && !orthographic.visible(box({0,6,-5})),"Orthographic visibility ignored render-target aspect or FOV");
    EndMode3D(); EndTextureMode(); UnloadRenderTexture(wide);
    rlSetClipPlanes(near,far);
}
}
int main() {
    SetTraceLogLevel(LOG_WARNING); SetConfigFlags(FLAG_WINDOW_HIDDEN); InitWindow(64,64,"Mesh streaming checks");
    int result = 0;
    try {
        require(IsWindowReady(),"Streaming checks require an OpenGL context");
        residency(); upload_bytes_and_bounds(); camera_visibility();
        std::cout << "Mesh residency, upload budgets, eviction/reload and perspective/orthographic camera visibility passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    CloseWindow(); return result;
}
