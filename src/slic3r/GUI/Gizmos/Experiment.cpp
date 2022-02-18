#include "libslic3r/Model.hpp"

#include <algorithm>

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoPainterBase.hpp"
#include "libslic3r/TriangleSelector.hpp"

namespace Slic3r::GUI {

//TODO close in seprate namespace

struct Triangle {
    stl_triangle_vertex_indices indices;
    Vec3f normal;
    size_t index_in_its;
    Vec3i neighbours;
    float min_z;

    //members updated during algorithm
    size_t area { 0 };
};

struct SupportPlacerMesh {

    indexed_triangle_set mesh;
    std::vector<Triangle> triangles;
    std::vector<size_t> triangle_indexes_by_z;

    explicit SupportPlacerMesh(indexed_triangle_set &&mesh) :
            mesh(mesh) {

        auto neighbours = its_face_neighbors_par(mesh);

        auto min_z_point = [](const size_t &t_index) {
            const auto &t = triangles[t_index];
            return std::min(mesh.vertices[t.indices.z()].z(),
                    std::min(mesh.vertices[t.indices.x()].z(), mesh.vertices[t.indices.y()].z()));
        };

        for (size_t face_index = 0; face_index < mesh.indices; ++face_index) {
            Vec3f normal = its_face_normal(mesh, face_index);
            triangles.push_back(Triangle { mesh.indices[face_index], normal, face_index, neighbours[face_index],
                    min_z_point(face_index) });
            triangle_indexes_by_z.push_back(face_index);
        }

        std::sort(triangle_indexes_by_z.begin(), triangle_indexes_by_z.end(),
                [&](const size_t &left, const size_t &right) {
                    return triangles[left].min_z < triangles[right].min_z;
                });
    }

    void assign_areas() {
        size_t next_to_process = 0;
        while (next_to_process < triangles.size()) {
        if (triangles[next_to_process].area > 0){
            //already done, skip
            continue;
        }



    }
}

};

void do_experimental_support_placement(indexed_triangle_set mesh,
        const TriangleSelectorGUI *selector) {
    SupportPlacerMesh support_placer { std::move(mesh) };

    support_placer.assign_areas();

}

}
