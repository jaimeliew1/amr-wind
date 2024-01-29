#ifndef LINEAR_VEER_H
#define LINEAR_VEER_H

#include "AMReX_Geometry.H"
#include "AMReX_Gpu.H"

namespace amr_wind {

class Field;

namespace udf {

struct LinearVeer
{
    struct DeviceOp
    {
        // clang-format off
        // Declare parameters here if needed. For example:
        amrex::Real foo{1.0};
        // amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> bar = {0.0};
        // clang-format on

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
            // Compute quantities to set the field values. For example:
            // clang-format off
            const auto* problo = geom.ProbLo();
            const auto* dx = geom.CellSize();
            const auto x = problo[0] + (iv[0] + 0.5) * dx[0];
            const auto y = problo[1] + (iv[1] + 0.5) * dx[1];
            const auto z = problo[2] + (iv[2] + 0.5) * dx[2];
            const amrex::GpuArray<amrex::Real, AMREX_SPACEDIM> vel = {1.0, 0.0, 0.0};

            // Once the above is done, fill the field as:
            field(iv[0], iv[1], iv[2], dcomp + comp) = vel[orig_comp + comp];
            std::cout << "hello, Jaime here. this is foo" << foo << std::endl;
            // clang-format on
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
