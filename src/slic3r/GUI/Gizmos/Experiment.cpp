#include "libslic3r/Model.hpp"

#include <algorithm>

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoPainterBase.hpp"
#include "libslic3r/TriangleSelector.hpp"

#include <boost/nowide/cstdio.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r::GUI {

//TODO close in seprate namespace

static constexpr size_t empty { size_t(-1) };

struct Triangle {
    stl_triangle_vertex_indices indices;
    Vec3f normal;
    size_t index_in_its;
    Vec3i neighbours;
    Vec3f z_coords_sorted_asc;

    //members updated during algorithm
    float strength { 0.0 };
    bool supports = false;
    bool visited = false;
    bool gathering_supports = false;
};

inline float face_area(const stl_vertex vertex[3]) {
    return (vertex[1] - vertex[0]).cross(vertex[2] - vertex[1]).norm() / 2;
}

inline float its_face_area(const indexed_triangle_set &its, const stl_triangle_vertex_indices face)
        {
    const stl_vertex vertices[3] { its.vertices[face[0]], its.vertices[face[1]], its.vertices[face[2]] };
    return face_area(vertices);
}
inline float its_face_area(const indexed_triangle_set &its, const int face_idx)
        {
    return its_face_area(its, its.indices[face_idx]);
}

struct SupportPlacerMesh {
    const float gather_phase_target = 3.0;

    indexed_triangle_set mesh;
    std::vector<Triangle> triangles;
    std::vector<size_t> triangle_indexes_by_z;

    explicit SupportPlacerMesh(indexed_triangle_set &&t_mesh) :
            mesh(t_mesh) {
        auto neighbours = its_face_neighbors_par(mesh);

        auto z_coords_sorted = [&](const size_t &face_index) {
            const auto &face = mesh.indices[face_index];
            Vec3f z_coords = Vec3f { mesh.vertices[face.x()].z(), mesh.vertices[face.y()].z(),
                    mesh.vertices[face.z()].z() };
            ;
            std::sort(z_coords.data(), z_coords.data() + 3);
            return z_coords;
        };

        for (size_t face_index = 0; face_index < mesh.indices.size(); ++face_index) {
            Vec3f normal = its_face_normal(mesh, face_index);
            triangles.push_back(Triangle { mesh.indices[face_index], normal, face_index, neighbours[face_index],
                    z_coords_sorted(face_index) });
            triangle_indexes_by_z.push_back(face_index);
        }

        std::sort(triangle_indexes_by_z.begin(), triangle_indexes_by_z.end(),
                [&](const size_t &left, const size_t &right) {
                    for (int i = 0; i < triangles[left].z_coords_sorted_asc.size(); ++i) {
                        if (triangles[left].z_coords_sorted_asc.data()[i]
                                < triangles[right].z_coords_sorted_asc.data()[i]) {
                            return true;
                        }
                        if (triangles[left].z_coords_sorted_asc.data()[i]
                                > triangles[right].z_coords_sorted_asc.data()[i]) {
                            return false;
                        }
                    }
                    return false;
                });
    }

    const float calculate_support_cost(float dot_product, size_t index_in_its) const {
        float dot_pow = dot_product * dot_product;
        float weight = (1.0 / (-1.2 * dot_pow + 2)) - 0.5;
        return weight * its_face_area(mesh, index_in_its);
    }

    void find_support_areas() {
        for (const size_t &current_index : triangle_indexes_by_z) {
            Triangle &current = triangles[current_index];
            float dot_product = current.normal.dot(-Vec3f::UnitZ());

            if (dot_product < 0.2) {
                current.strength = gather_phase_target * 100.0;
                current.visited = true;
                current.supports = false;
            } else {
                float neighbours_strength_sum = 0;
                for (const auto &neighbour_index : current.neighbours) {
                    const Triangle &neighbour = triangles[neighbour_index];
                    if (!neighbour.visited) {
                        //not visited yet, ignore
                        continue;
                    } else {
                        neighbours_strength_sum += neighbour.strength;
                        float support_cost = calculate_support_cost(dot_product, current.index_in_its);
                        if (neighbour.gathering_supports || neighbour.strength < support_cost) {
                            continue;
                        } else {
                            current.strength = neighbour.strength - support_cost;
                            current.visited = true;
                            current.supports = false;
                            current.gathering_supports = false;
                            break;
                        }
                    }
                }

                if (!current.visited) {
                    current.supports = true;
                    current.visited = true;
                    current.strength = neighbours_strength_sum + its_face_area(mesh, current.index_in_its);
                    current.gathering_supports = current.strength < gather_phase_target;
                }
            }
        }
    }

}
;

inline void do_experimental_support_placement(indexed_triangle_set mesh,
        TriangleSelectorGUI *selector) {
    SupportPlacerMesh support_placer { std::move(mesh) };

    support_placer.find_support_areas();

    for (const Triangle &t : support_placer.triangles) {
        if (t.supports) {
            selector->set_facet(t.index_in_its, EnforcerBlockerType::ENFORCER);
            selector->request_update_render_data();
        }
    }
}

}
