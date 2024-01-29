#include "amr-wind/physics/udfs/LinearVeer.H"
#include "amr-wind/core/Field.H"
#include "amr-wind/core/FieldRepo.H"
#include "amr-wind/core/vs/vector.H"
#include "amr-wind/equation_systems/icns/icns.H"

#include "AMReX_ParmParse.H"

namespace amr_wind::udf {

LinearVeer::LinearVeer(const Field& fld)
{

       const int ncomp = fld.num_comp();
       amrex::ParmParse pp("LinearVeer");
       pp.query("amplitude", m_op.amplitude);
       pp.query("Lz", m_op.Lz);
       pp.query("D", m_op.D);
       pp.query("Uinf", m_op.Uinf);

}

} // namespace amr_wind::udf
