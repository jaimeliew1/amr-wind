#ifndef LINEAR_VEER_H
#define LINEAR_VEER_H

#include <cmath>
#include "AMReX_Geometry.H"
#include "AMReX_Gpu.H"
#include "amr-wind/utilities/trig_ops.H"

namespace amr_wind {

class Field;

namespace udf {

struct LinearVeer
{
    struct DeviceOp
    {

        amrex::Real amplitude{1.0};
        amrex::Real Lz{1.0};
        amrex::Real D{1.0};
        amrex::Real Uinf{1.0};
        /*
        LinearVeer.amplitude = 1.0
        LinearVeer.Lz = 1.0
        LinearVeer.D = 1.0
        LinearVeer.Uinf = 1.0
        */

        AMREX_GPU_DEVICE
        inline void operator()(
            const amrex::IntVect& iv,
            amrex::Array4<amrex::Real> const& field,
            amrex::GeometryData const& geom,
            const amrex::Real time,
            amrex::Orientation ori,
            const int comp,
            const int dcomp,
            const int orig_comp) const
        {

            const auto* problo = geom.ProbLo();
            const auto* dx = geom.CellSize();
            const auto x = problo[0] + (iv[0] + 0.5) * dx[0];
            const auto y = problo[1] + (iv[1] + 0.5) * dx[1];
            const auto z = problo[2] + (iv[2] + 0.5) * dx[2];

            const amrex::Real alpha = amplitude * std::tanh((z - Lz / 2.0) / D) * pi() / 180;
            const amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> vel = {Uinf * std::cos(alpha), Uinf * std::sin(alpha), 0.0};


            field(iv[0], iv[1], iv[2], dcomp + comp) = vel[orig_comp + comp];

        }
    };
    using DeviceType = DeviceOp;

    static std::string identifier() { return "LinearVeer"; }

    explicit LinearVeer(const Field& fld);

    DeviceType device_instance() const { return m_op; }

    DeviceOp m_op;
};

} // namespace udf
} // namespace amr_wind

#endif /* LINEAR_VEER_H */
