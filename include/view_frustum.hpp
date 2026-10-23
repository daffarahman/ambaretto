#pragma once
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <array>
#include <cmath>

namespace ambaretto {
// Capture inside the active 3D pass so FOV, render-target aspect and clip planes match drawing.
class ViewFrustum {
public:
    ViewFrustum() {
        const auto m = MatrixMultiply(rlGetMatrixModelview(),rlGetMatrixProjection());
        planes_ = {{{m.m3+m.m0,m.m7+m.m4,m.m11+m.m8,m.m15+m.m12},
                    {m.m3-m.m0,m.m7-m.m4,m.m11-m.m8,m.m15-m.m12},
                    {m.m3+m.m1,m.m7+m.m5,m.m11+m.m9,m.m15+m.m13},
                    {m.m3-m.m1,m.m7-m.m5,m.m11-m.m9,m.m15-m.m13},
                    {m.m3+m.m2,m.m7+m.m6,m.m11+m.m10,m.m15+m.m14},
                    {m.m3-m.m2,m.m7-m.m6,m.m11-m.m10,m.m15-m.m14}}};
        for (auto& p : planes_) {
            const float length = std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z);
            if (length > 0) { p.x/=length; p.y/=length; p.z/=length; p.w/=length; }
        }
    }
    bool visible(BoundingBox bounds,const Matrix* transform = nullptr) const {
        auto center = Vector3Scale(Vector3Add(bounds.min,bounds.max),.5f);
        auto half = Vector3Scale(Vector3Subtract(bounds.max,bounds.min),.5f);
        if (transform) {
            center = Vector3Transform(center,*transform);
            const auto& m = *transform;
            half = {std::abs(m.m0)*half.x+std::abs(m.m4)*half.y+std::abs(m.m8)*half.z,
                    std::abs(m.m1)*half.x+std::abs(m.m5)*half.y+std::abs(m.m9)*half.z,
                    std::abs(m.m2)*half.x+std::abs(m.m6)*half.y+std::abs(m.m10)*half.z};
        }
        for (const auto& p : planes_)
            if (p.x*center.x+p.y*center.y+p.z*center.z+p.w
                +std::abs(p.x)*half.x+std::abs(p.y)*half.y+std::abs(p.z)*half.z < -.001f) return false;
        return true;
    }
private:
    std::array<Vector4,6> planes_{};
};
} // namespace ambaretto
