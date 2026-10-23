#pragma once
#include <raylib.h>
#include <rlgl.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace ambaretto {
struct MeshUploadBudget {
    static constexpr std::size_t byte_limit = 2*1024*1024;
    std::size_t bytes = 0;
    unsigned meshes = 0;
};
struct MeshStreamingStats {
    std::size_t chunks = 0, resident_chunks = 0, pending_chunks = 0;
    std::size_t cpu_geometry_bytes = 0, resident_geometry_bytes = 0;
};
inline bool mesh_resident(const Model& model) {
    return model.meshCount > 0 && model.meshes && model.meshes[0].vaoId != 0;
}
// Borrows static meshes; their CPU arrays and owning models must outlive this registry.
class MeshStreaming {
public:
    static constexpr float preload_distance = 256, retain_distance = 512;
    void include(const MeshStreaming& other) { entries_.insert(entries_.end(),other.entries_.begin(),other.entries_.end()); }
    void add(Model& model) {
        for (int i = 0; i < model.meshCount; ++i) {
            auto& mesh = model.meshes[i];
            const auto bytes = std::size_t(mesh.vertexCount) *
                ((mesh.vertices ? 12 : 0) + (mesh.normals ? 12 : 0) + (mesh.texcoords ? 8 : 0)
                + (mesh.texcoords2 ? 8 : 0) + (mesh.colors ? 4 : 0))
                + (mesh.indices ? std::size_t(mesh.triangleCount)*6 : 0);
            entries_.push_back({&mesh, GetMeshBoundingBox(mesh), bytes});
        }
    }
    bool update(Vector3 focus, float distance, MeshUploadBudget& budget) {
        pending_.clear();
        for (auto& entry : entries_) {
            const auto& box = entry.bounds;
            const float dx = std::max({box.min.x-focus.x, 0.f, focus.x-box.max.x});
            const float dz = std::max({box.min.z-focus.z, 0.f, focus.z-box.max.z});
            entry.distance = std::hypot(dx,dz);
            if (entry.mesh->vaoId && entry.distance > distance+retain_distance) release(*entry.mesh);
            if (!entry.mesh->vaoId && entry.distance <= distance+preload_distance) pending_.push_back(&entry);
        }
        std::stable_sort(pending_.begin(),pending_.end(),[](const auto* a,const auto* b) { return a->distance < b->distance; });
        std::size_t uploaded = 0;
        for (auto* entry : pending_) {
            // shortcut: one dense chunk can exceed 2 MiB, split dense cells if uploads stall frames.
            if (budget.meshes >= 4 || (budget.meshes && budget.bytes+entry->bytes > MeshUploadBudget::byte_limit)) break;
            static const auto error = reinterpret_cast<unsigned int (*)()>(rlGetProcAddress("glGetError"));
            if (error) for (int i = 0; i < 16 && error(); ++i) {}
            UploadMesh(entry->mesh,false);
            if (!entry->mesh->vaoId || !entry->mesh->vboId || !entry->mesh->vboId[0] || (error && error())) {
                release(*entry->mesh);
                throw std::runtime_error("GPU could not upload a streamed scenery mesh.");
            }
            budget.bytes += entry->bytes; ++budget.meshes; ++uploaded;
        }
        pending_count_ = pending_.size()-uploaded;
        return pending_count_ == 0;
    }
    MeshStreamingStats stats() const {
        MeshStreamingStats result;
        result.chunks = entries_.size(); result.pending_chunks = pending_count_;
        for (const auto& entry : entries_) {
            result.cpu_geometry_bytes += entry.bytes;
            if (entry.mesh->vaoId) { ++result.resident_chunks; result.resident_geometry_bytes += entry.bytes; }
        }
        return result;
    }
private:
    struct Entry { Mesh* mesh; BoundingBox bounds; std::size_t bytes; float distance = 0; };
    static void release(Mesh& mesh) {
        // Unload only the GPU handles; retain the original CPU arrays for the next visit.
        Mesh gpu{}; gpu.vaoId = mesh.vaoId; gpu.vboId = mesh.vboId;
        UnloadMesh(gpu); mesh.vaoId = 0; mesh.vboId = nullptr;
    }
    std::vector<Entry> entries_;
    std::vector<Entry*> pending_;
    std::size_t pending_count_ = 0;
};
} // namespace ambaretto
