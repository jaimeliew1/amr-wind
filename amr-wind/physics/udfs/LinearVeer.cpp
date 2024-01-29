#include "amr-wind/physics/udfs/LinearVeer.H"
#include "amr-wind/core/Field.H"
#include "amr-wind/core/FieldRepo.H"
#include "amr-wind/core/vs/vector.H"
#include "amr-wind/equation_systems/icns/icns.H"

#include "AMReX_ParmParse.H"

namespace amr_wind::udf {

LinearVeer::LinearVeer(const Field& fld)
{
    // This is a where the user can set some user defined variables
    // This capability can be activated with the following in the input file:
    // xlo.type = "mass_inflow"
    // xlo.velocity.inflow_type = LinearVeer
    // LinearVeer.foo = 1.0

    // clang-format off
    //{
       const int ncomp = fld.num_comp();
       amrex::ParmParse pp("LinearVeer");
       pp.query("foo", m_op.foo);
    //    amrex::Vector<amrex::Real> vel(0.0, ncomp);
    //    pp.getarr("velocity", vel);
    //    AMREX_ALWAYS_ASSERT(vel.size() == ncomp);
    //    for (int i = 0; i < ncomp; ++i) {
    //        m_op.bar[i] = vel[i];
    //    }
    //}
    // clang-format on

}

} // namespace amr_wind::udf
