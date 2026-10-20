// The prime tower only exists when a print changes filament, and the pricer
// always prints one. Print.cpp still references the tower code for sizing
// and planning, which pulls in the tower generators plus the G-code writer
// and parser they use. The pricing build links these instead: the small
// sizing helpers, copied unchanged from OrcaSlicer 2.4.2 (AGPL-3.0), and
// stand-ins for the rest that fail if a tower is ever asked for.
#include "libslic3r/libslic3r.h"
#include "libslic3r/Exception.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/GCode/WipeTower2.hpp"
#include <cmath>

namespace Slic3r {

[[noreturn]] static void no_tower() { throw Slic3r::RuntimeError("The pricing build has no prime tower"); }

const std::map<float, float> WipeTower::min_depth_per_height = {
    {5.f,5.f}, {100.f, 20.f}, {250.f, 40.f}, {350.f, 60.f}
};

float WipeTower::get_limit_depth_by_height(float max_height)
{
    float min_wipe_tower_depth = 0.f;
    auto  iter                 = WipeTower::min_depth_per_height.begin();
    while (iter != WipeTower::min_depth_per_height.end()) {
        auto curr_height_to_depth = *iter;

        // This is the case that wipe tower height is lower than the first min_depth_to_height member.
        if (curr_height_to_depth.first >= max_height) {
            min_wipe_tower_depth = curr_height_to_depth.second;
            break;
        }

        iter++;

        // If curr_height_to_depth is the last member, use its min_depth.
        if (iter == WipeTower::min_depth_per_height.end()) {
            min_wipe_tower_depth = curr_height_to_depth.second;
            break;
        }

        // If wipe tower height is between the current and next member, set the min_depth as linear interpolation between them
        auto next_height_to_depth = *iter;
        if (next_height_to_depth.first > max_height) {
            float height_base    = curr_height_to_depth.first;
            float height_diff    = next_height_to_depth.first - curr_height_to_depth.first;
            float min_depth_base = curr_height_to_depth.second;
            float depth_diff     = next_height_to_depth.second - curr_height_to_depth.second;

            min_wipe_tower_depth = min_depth_base + (max_height - curr_height_to_depth.first) / height_diff * depth_diff;
            break;
        }
    }
    return min_wipe_tower_depth;
}

float WipeTower::get_auto_brim_by_height(float max_height) {
    if (max_height < 100) return max_height/100.f * 8.f;
    return 8.f;
}

std::pair<double, double> WipeTower2::get_wipe_tower_cone_base(double width, double height, double depth, double angle_deg)
{
    double R = std::tan(Geometry::deg2rad(angle_deg/2.)) * height;
    double fake_width = 0.66 * width;
    double diag = std::hypot(fake_width / 2., depth / 2.);
    double support_scale = 1.;
    if (R > diag) {
        double w = fake_width;
        double sin = 0.5 * depth / diag;
        double tan = depth / w;
        double t = (R - diag) * sin;
        support_scale = (w / 2. + t / tan + t * tan) / (w / 2.);
    }
    return std::make_pair(R, support_scale);
}

std::vector<std::vector<float>> WipeTower2::extract_wipe_volumes(const PrintConfig& config)
{
    // Get wiping matrix to get number of extruders and convert vector<double> to vector<float>:
    std::vector<float> wiping_matrix(cast<float>(config.flush_volumes_matrix.values));
    auto scale = config.flush_multiplier.get_at(0);

    // The values shall only be used when SEMM is enabled. The purging for other printers
    // is determined by filament_minimal_purge_on_wipe_tower.
    if (! config.purge_in_prime_tower.value || ! config.single_extruder_multi_material.value)
        std::fill(wiping_matrix.begin(), wiping_matrix.end(), 0.f);

    // Extract purging volumes for each extruder pair:
    std::vector<std::vector<float>> wipe_volumes;
    const unsigned int number_of_extruders = (unsigned int)(sqrt(wiping_matrix.size())+EPSILON);
    for (size_t i = 0; i<number_of_extruders; ++i)
        wipe_volumes.push_back(std::vector<float>(wiping_matrix.begin()+i*number_of_extruders, wiping_matrix.begin()+(i+1)*number_of_extruders));

    // Also include filament_minimal_purge_on_wipe_tower. This is needed for the preview.
    for (unsigned int i = 0; i<number_of_extruders; ++i)
        for (unsigned int j = 0; j<number_of_extruders; ++j)
            wipe_volumes[i][j] = std::max<float>(wipe_volumes[i][j] * scale, config.filament_minimal_purge_on_wipe_tower.get_at(j));

    return wipe_volumes;
}

WipeTower::WipeTower(const PrintConfig &, int, Vec3d, size_t, const float, const std::vector<unsigned int> &) { no_tower(); }
void WipeTower::set_extruder(size_t, const PrintConfig &) { no_tower(); }
void WipeTower::plan_toolchange(float, float, unsigned int, unsigned int, float, float) { no_tower(); }
WipeTower::ToolChangeResult WipeTower::tool_change(size_t, bool, bool) { no_tower(); }
void WipeTower::generate_new(std::vector<std::vector<WipeTower::ToolChangeResult>> &) { no_tower(); }
TriangleMesh WipeTower::its_make_rib_tower(float, float, float, float, float, bool) { no_tower(); }
TriangleMesh WipeTower::its_make_rib_brim(const Polygon &, float) { no_tower(); }
Polygon WipeTower::rib_section(float, float, float, float, bool) { no_tower(); }

WipeTower2::WipeTower2(const PrintConfig &, const PrintRegionConfig &, int, Vec3d, const std::vector<std::vector<float>> &, size_t) { no_tower(); }
void WipeTower2::set_extruder(size_t, const PrintConfig &) { no_tower(); }
void WipeTower2::plan_toolchange(float, float, unsigned int, unsigned int, float) { no_tower(); }
void WipeTower2::generate(std::vector<std::vector<WipeTower::ToolChangeResult>> &) { no_tower(); }
std::vector<std::pair<float, float>> WipeTower2::get_z_and_depth_pairs() const { no_tower(); }
std::vector<WipeTower::ToolChangeResult> WipeTower2::prime(float, const std::vector<unsigned int> &, bool) { no_tower(); }
WipeTower::ToolChangeResult WipeTower2::tool_change(size_t) { no_tower(); }

} // namespace Slic3r
