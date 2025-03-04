#include "aw_test_utils/MeshTest.H"
#include "aw_test_utils/iter_tools.H"
#include "aw_test_utils/test_utils.H"
#include "amr-wind/utilities/trig_ops.H"
#include "amr-wind/equation_systems/vof/volume_fractions.H"
#include "amr-wind/equation_systems/vof/vof_hybridsolver_ops.H"
#include "amr-wind/equation_systems/vof/vof.H"
#include "amr-wind/equation_systems/SchemeTraits.H"

namespace amr_wind_tests {

class VOFToolTest : public MeshTest
{
protected:
    void populate_parameters() override
    {
        MeshTest::populate_parameters();

        {
            amrex::ParmParse pp("amr");
            amrex::Vector<int> ncell{{4, 4, 4}};
            pp.add("max_level", 0);
            pp.add("max_grid_size", 4);
            pp.addarr("n_cell", ncell);
        }
        {
            amrex::ParmParse pp("geometry");
            amrex::Vector<amrex::Real> problo{{0.0, 0.0, 0.0}};
            amrex::Vector<amrex::Real> probhi{{1.0, 1.0, 1.0}};

            pp.addarr("prob_lo", problo);
            pp.addarr("prob_hi", probhi);
        }
    }

    amrex::Real dx = 0.25;
};

namespace {

void initialize_levelset(
    const int shape,
    const amrex::Real deltax,
    const amrex::Box& gbx,
    const amrex::Array4<amrex::Real>& lvs_arr)
{
    const int s = shape;
    const amrex::Real dx = deltax;
    amrex::ParallelFor(gbx, [=] AMREX_GPU_DEVICE(int i, int j, int k) {
        switch (s) {
        case 0:
            // Horizontal line
            lvs_arr(i, j, k) = 1.7 * dx;
            break;
        case 1:
            // Parabola
            lvs_arr(i, j, k) =
                1.9 * dx + 0.1 * dx * std::pow((amrex::Real)j - 0.3, 2);
            break;
        case 2:
            // Cosine profile
            lvs_arr(i, j, k) =
                2.0 * dx *
                (1.0 +
                 std::cos(((amrex::Real)i - 1.2) / amr_wind::utils::pi()));
            break;
        }
        // Subtract from local height
        lvs_arr(i, j, k) -= dx * ((amrex::Real)k + 0.5);
    });
}

void initialize_volume_fractions(
    const amrex::Box& bx, const amrex::Array4<amrex::Real>& vof_arr)
{
    // grow the box by 1 so that x,y,z go out of bounds and min(max()) corrects
    // it and it fills the ghosts with wall values
    amrex::ParallelFor(grow(bx, 1), [=] AMREX_GPU_DEVICE(int i, int j, int k) {
        // Default is gas phase
        vof_arr(i, j, k) = 0.0;
        // Set up some multiphase cells
        if (i + j + k > 5) {
            vof_arr(i, j, k) = 0.3;
        }
        if (i + j + k > 10) {
            vof_arr(i, j, k) = 0.7;
        }
        // Set up a liquid cell
        if (i == 0 && j == 0 && k == 0) {
            vof_arr(i, j, k) = 1.0;
        }
    });
}

void init_lvs(
    const int dir, const amrex::Real deltax, amr_wind::Field& levelset)
{
    run_algorithm(levelset, [&](const int lev, const amrex::MFIter& mfi) {
        auto levelset_arr = levelset(lev).array(mfi);
        const auto& bx = mfi.growntilebox();
        initialize_levelset(dir, deltax, bx, levelset_arr);
    });
}

void init_vof(amr_wind::Field& vof)
{
    run_algorithm(vof, [&](const int lev, const amrex::MFIter& mfi) {
        auto vof_arr = vof(lev).array(mfi);
        const auto& bx = mfi.validbox();
        initialize_volume_fractions(bx, vof_arr);
    });
}

amrex::Real
levelset_to_vof_test_impl(const amrex::Real deltax, amr_wind::Field& levelset)
{
    amrex::Real error_total = 0.0;
    const amrex::Real dx = deltax;

    for (int lev = 0; lev < levelset.repo().num_active_levels(); ++lev) {

        error_total += amrex::ReduceSum(
            levelset(lev), 0,
            [=] AMREX_GPU_HOST_DEVICE(
                amrex::Box const& bx,
                amrex::Array4<amrex::Real const> const& levelset_arr)
                -> amrex::Real {
                amrex::Real error = 0.0;

                amrex::Loop(bx, [=, &error](int i, int j, int k) noexcept {
                    amrex::Real vof = amr_wind::multiphase::levelset_to_vof(
                        i, j, k, 2.0 * dx, levelset_arr);

                    // Perform checks in multiphase cells
                    if (vof > 1e-12 && vof < 1.0 - 1e-12) {
                        // Integrate to get VOF, check error
                        amrex::Real approx_vof = amrex::min(
                            1.0,
                            amrex::max(
                                0.0, (levelset_arr(i, j, k) + 0.5 * dx) / dx));
                        error += std::abs(approx_vof - vof);
                    }

                    // Perform checks in single-phase cells
                    if (vof <= 1e-12) {
                        // Interface should be more than half cell away,
                        // negative levelset value
                        error +=
                            amrex::max(0.0, 0.5 * dx + levelset_arr(i, j, k));
                    }
                    if (vof >= 1.0 - 1e-12) {
                        // Interface should be more than half cell away,
                        // positive levelset value
                        error +=
                            amrex::max(0.0, 0.5 * dx - levelset_arr(i, j, k));
                    }
                });

                return error;
            });
    }
    return error_total;
}

amrex::Real interface_band_test_impl(amr_wind::Field& vof)
{
    amrex::Real error_total = 0;

    for (int lev = 0; lev < vof.repo().num_active_levels(); ++lev) {

        error_total += amrex::ReduceSum(
            vof(lev), 0,
            [=] AMREX_GPU_HOST_DEVICE(
                amrex::Box const& bx,
                amrex::Array4<amrex::Real const> const& vof_arr)
                -> amrex::Real {
                amrex::Real error = 0;

                amrex::Loop(bx, [=, &error](int i, int j, int k) noexcept {
                    bool intf =
                        amr_wind::multiphase::interface_band(i, j, k, vof_arr);

                    bool nocheck = true;
                    // Check within a cell of multiphase cells
                    if (i + 1 + j + 1 + k + 1 > 5) {
                        error += (intf ? 0 : 1);
                        nocheck = false;
                    }

                    // Check within a cell of liquid cell
                    if (i < 2 && j < 2 && k < 2) {
                        error += (intf ? 0 : 1);
                        nocheck = false;
                    }

                    // Confirm no flag in other locations
                    if (nocheck) {
                        error += (intf ? 1 : 0);
                    }
                });

                return error;
            });
    }
    return error_total;
}

amrex::Real sharpen_test_impl(amr_wind::Field& vof)
{
    amrex::Real error_total = 0;

    for (int lev = 0; lev < vof.repo().num_active_levels(); ++lev) {

        error_total += amrex::ReduceSum(
            vof(lev), 0,
            [=] AMREX_GPU_HOST_DEVICE(
                amrex::Box const& bx,
                amrex::Array4<amrex::Real const> const& vof_arr)
                -> amrex::Real {
                amrex::Real error = 0;

                amrex::Loop(bx, [=, &error](int i, int j, int k) noexcept {
                    // Initial VOF distribution is 0, 0.3, 0.7, or 1.0
                    // Expected answer based on implementation of
                    // amr_wind::multiphase::sharpen_kernel
                    amrex::Real vof_answer = 0.0;
                    if (i + j + k > 5) {
                        // because VOF < 0.5
                        const amrex::Real sign = -1.0;
                        const amrex::Real delta = std::abs(0.3 - 0.5);
                        vof_answer = 0.5 + sign * std::pow(delta, 1.0 / 3.0);
                    }
                    if (i + j + k > 10) {
                        // because VOF > 0.5
                        const amrex::Real sign = 1.0;
                        const amrex::Real delta = std::abs(0.7 - 0.5);
                        vof_answer = 0.5 + sign * std::pow(delta, 1.0 / 3.0);
                    }
                    // Set up a liquid cell
                    if (i == 0 && j == 0 && k == 0) {
                        vof_answer = 1.0;
                    }

                    // Limit answer to VOF bounds
                    vof_answer = std::max(0.0, std::min(1.0, vof_answer));

                    // Difference between actual and expected
                    error += std::abs(vof_arr(i, j, k) - vof_answer);
                });

                return error;
            });
    }
    return error_total;
}

amrex::Real sharpen_test_density_impl(
    amr_wind::Field& dens, const amrex::Real rho1, const amrex::Real rho2)
{
    amrex::Real error_total = 0;

    for (int lev = 0; lev < dens.repo().num_active_levels(); ++lev) {

        error_total += amrex::ReduceSum(
            dens(lev), 0,
            [=] AMREX_GPU_HOST_DEVICE(
                amrex::Box const& bx,
                amrex::Array4<amrex::Real const> const& dens_arr)
                -> amrex::Real {
                amrex::Real error = 0;

                amrex::Loop(bx, [=, &error](int i, int j, int k) noexcept {
                    // Initial VOF distribution is 0, 0.3, 0.7, or 1.0
                    // Expected answer based on implementation of
                    // amr_wind::multiphase::sharpen_kernel
                    amrex::Real vof_answer = 0.0;
                    if (i + j + k > 5) {
                        // because VOF < 0.5
                        const amrex::Real sign = -1.0;
                        const amrex::Real delta = std::abs(0.3 - 0.5);
                        vof_answer = 0.5 + sign * std::pow(delta, 1.0 / 3.0);
                    }
                    if (i + j + k > 10) {
                        // because VOF > 0.5
                        const amrex::Real sign = 1.0;
                        const amrex::Real delta = std::abs(0.7 - 0.5);
                        vof_answer = 0.5 + sign * std::pow(delta, 1.0 / 3.0);
                    }
                    // Set up a liquid cell
                    if (i == 0 && j == 0 && k == 0) {
                        vof_answer = 1.0;
                    }

                    // Limit answer to VOF bounds
                    vof_answer = std::max(0.0, std::min(1.0, vof_answer));

                    // Difference between actual and expected
                    const amrex::Real dens_answer =
                        rho1 * vof_answer + rho2 * (1.0 - vof_answer);
                    error += std::abs(dens_arr(i, j, k) - dens_answer);
                });

                return error;
            });
    }
    return error_total;
}

} // namespace

TEST_F(VOFToolTest, interface_band)
{

    populate_parameters();
    {
        amrex::ParmParse pp("geometry");
        amrex::Vector<int> periodic{{0, 0, 0}};
        pp.addarr("is_periodic", periodic);
    }

    initialize_mesh();

    auto& repo = sim().repo();
    const int ncomp = 1;
    const int nghost = 3;
    auto& vof = repo.declare_field("vof", ncomp, nghost);

    init_vof(vof);
    amrex::Real error_total = interface_band_test_impl(vof);
    amrex::ParallelDescriptor::ReduceRealSum(error_total);
    EXPECT_EQ(error_total, 0.0);
}

TEST_F(VOFToolTest, levelset_to_vof)
{

    populate_parameters();
    {
        amrex::ParmParse pp("geometry");
        amrex::Vector<int> periodic{{0, 0, 0}};
        pp.addarr("is_periodic", periodic);
    }

    initialize_mesh();

    auto& repo = sim().repo();
    const int ncomp = 1;
    const int nghost = 3;
    auto& levelset = repo.declare_field("levelset", ncomp, nghost);

    amrex::Real error_total = 0.0;
    // profile 0: horizontal
    init_lvs(0, dx, levelset);
    error_total = levelset_to_vof_test_impl(dx, levelset);
    amrex::ParallelDescriptor::ReduceRealSum(error_total);
    EXPECT_NEAR(error_total, 0.0, 1e-12);
    //  profile 1: parabola
    init_lvs(1, dx, levelset);
    error_total = levelset_to_vof_test_impl(dx, levelset);
    amrex::ParallelDescriptor::ReduceRealSum(error_total);
    EXPECT_NEAR(error_total, 0.0, 0.011);
    // profile 2: cosine
    init_lvs(2, dx, levelset);
    error_total = levelset_to_vof_test_impl(dx, levelset);
    amrex::ParallelDescriptor::ReduceRealSum(error_total);
    EXPECT_NEAR(error_total, 0.0, 0.016);
}

TEST_F(VOFToolTest, sharpen_acquired_vof)
{

    populate_parameters();
    initialize_mesh();

    auto& repo = sim().repo();
    const int ncomp = 1;
    const int nghost = 3;
    const int nghost_int = 1;
    auto& vof = repo.declare_field("vof", ncomp, nghost);
    auto& iblank = repo.declare_int_field("iblank_cell", ncomp, nghost_int);

    // Use as if entire domain is from nalu
    iblank.setVal(-1);
    // Initialize and sharpen vof
    init_vof(vof);
    amr_wind::multiphase::sharpen_acquired_vof(1, iblank, vof);

    // Check results
    amrex::Real error_total = sharpen_test_impl(vof);
    amrex::ParallelDescriptor::ReduceRealSum(error_total);
    EXPECT_NEAR(error_total, 0.0, 1e-15);
}

TEST_F(VOFToolTest, sharpen_replace_old_density)
{

    const amrex::Real rho1 = 1000.0;
    const amrex::Real rho2 = 1.0;
    populate_parameters();
    {
        amrex::ParmParse pp("geometry");
        amrex::Vector<int> periodic{{1, 1, 1}};
        pp.addarr("is_periodic", periodic);
    }
    {
        amrex::ParmParse pp("MultiPhase");
        pp.add("density_fluid1", rho1);
        pp.add("density_fluid2", rho2);
    }
    {
        amrex::ParmParse pp("incflo");
        amrex::Vector<std::string> physics{"MultiPhase"};
        pp.addarr("physics", physics);
        pp.add("use_godunov", (int)1);
    }

    initialize_mesh();

    auto& repo = sim().repo();

    // PDE manager, for access to VOF PDE later
    auto& pde_mgr = sim().pde_manager();
    // Setup of icns provides MAC velocities
    pde_mgr.register_icns();

    // Initialize physics for the sake of MultiPhase routines
    sim().init_physics();

    // Initialize volume fraction field
    auto& vof = repo.get_field("vof");

    // Set up iblank field
    const int ncomp = 1;
    const int nghost_int = 1;
    auto& iblank = repo.declare_int_field("iblank_cell", ncomp, nghost_int);
    // Use as if entire domain is from nalu
    iblank.setVal(-1);

    // Initialize vof
    init_vof(vof);

    // Initialize advective velocities with something
    auto& umac = repo.get_field("u_mac");
    auto& vmac = repo.get_field("v_mac");
    auto& wmac = repo.get_field("w_mac");
    umac.setVal(1.0);
    vmac.setVal(1.0);
    wmac.setVal(1.0);

    // Get vof equation handle and perform init
    auto& seqn = pde_mgr(
        amr_wind::pde::VOF::pde_name() + "-" +
        amr_wind::fvm::Godunov::scheme_name());
    seqn.initialize();

    // Do advection step (which results in sharpening and in copying of
    // sharpened vof to old vof)
    seqn.compute_advection_term(amr_wind::FieldState::Old);

    // Check that old vof is equal to the sharpened solution
    amrex::Real error_total =
        sharpen_test_impl(vof.state(amr_wind::FieldState::Old));
    amrex::ParallelDescriptor::ReduceRealSum(error_total);
    EXPECT_NEAR(error_total, 0.0, 1e-15);

    // Do post solve (which recalculates old density with old vof)
    seqn.post_solve_actions();

    // Check old density
    auto& dens = repo.get_field("density");
    error_total = sharpen_test_density_impl(
        dens.state(amr_wind::FieldState::Old), rho1, rho2);
    amrex::ParallelDescriptor::ReduceRealSum(error_total);
    EXPECT_NEAR(error_total, 0.0, 1e-15);
}

} // namespace amr_wind_tests
