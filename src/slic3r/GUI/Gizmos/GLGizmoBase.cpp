#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectManipulation.hpp"
#if ENABLE_GL_SHADERS_ATTRIBUTES
#include "slic3r/GUI/Plater.hpp"
#endif // ENABLE_GL_SHADERS_ATTRIBUTES

// TODO: Display tooltips quicker on Linux

namespace Slic3r {
namespace GUI {

const float GLGizmoBase::Grabber::SizeFactor = 0.05f;
const float GLGizmoBase::Grabber::MinHalfSize = 1.5f;
const float GLGizmoBase::Grabber::DraggingScaleFactor = 1.25f;

void GLGizmoBase::Grabber::render(bool hover, float size)
{
    render(size, hover ? complementary(color) : color, false);
}

float GLGizmoBase::Grabber::get_half_size(float size) const
{
    return std::max(size * SizeFactor, MinHalfSize);
}

float GLGizmoBase::Grabber::get_dragging_half_size(float size) const
{
    return get_half_size(size) * DraggingScaleFactor;
}

void GLGizmoBase::Grabber::render(float size, const ColorRGBA& render_color, bool picking)
{
#if ENABLE_GL_SHADERS_ATTRIBUTES
    GLShaderProgram* shader = wxGetApp().get_current_shader();
    if (shader == nullptr)
        return;
#endif // ENABLE_GL_SHADERS_ATTRIBUTES

    if (!m_cube.is_initialized()) {
        // This cannot be done in constructor, OpenGL is not yet
        // initialized at that point (on Linux at least).
        indexed_triangle_set its = its_make_cube(1., 1., 1.);
        its_translate(its, -0.5f * Vec3f::Ones());
#if ENABLE_LEGACY_OPENGL_REMOVAL
        m_cube.init_from(its);
#else
        m_cube.init_from(its, BoundingBoxf3{ { -0.5, -0.5, -0.5 }, { 0.5, 0.5, 0.5 } });
#endif // ENABLE_LEGACY_OPENGL_REMOVAL
    }

    const float fullsize = 2.0f * (dragging ? get_dragging_half_size(size) : get_half_size(size));

#if ENABLE_LEGACY_OPENGL_REMOVAL
    m_cube.set_color(render_color);
#else
    m_cube.set_color(-1, render_color);
#endif // ENABLE_LEGACY_OPENGL_REMOVAL

#if ENABLE_GL_SHADERS_ATTRIBUTES
    const Camera& camera = wxGetApp().plater()->get_camera();
    const Transform3d view_model_matrix = camera.get_view_matrix() * matrix * Geometry::assemble_transform(center, angles, fullsize * Vec3d::Ones());
    const Transform3d& projection_matrix = camera.get_projection_matrix();
 
    shader->set_uniform("view_model_matrix", view_model_matrix);
    shader->set_uniform("projection_matrix", projection_matrix);
    shader->set_uniform("normal_matrix", (Matrix3d)view_model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose());
#else
    glsafe(::glPushMatrix());
    glsafe(::glTranslated(center.x(), center.y(), center.z()));
    glsafe(::glRotated(Geometry::rad2deg(angles.z()), 0.0, 0.0, 1.0));
    glsafe(::glRotated(Geometry::rad2deg(angles.y()), 0.0, 1.0, 0.0));
    glsafe(::glRotated(Geometry::rad2deg(angles.x()), 1.0, 0.0, 0.0));
    glsafe(::glScaled(fullsize, fullsize, fullsize));
#endif // ENABLE_GL_SHADERS_ATTRIBUTES
    m_cube.render();
#if !ENABLE_GL_SHADERS_ATTRIBUTES
    glsafe(::glPopMatrix());
#endif // !ENABLE_GL_SHADERS_ATTRIBUTES
}

GLGizmoBase::GLGizmoBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : m_parent(parent)
    , m_group_id(-1)
    , m_state(Off)
    , m_shortcut_key(0)
    , m_icon_filename(icon_filename)
    , m_sprite_id(sprite_id)
    , m_hover_id(-1)
    , m_dragging(false)
    , m_imgui(wxGetApp().imgui())
    , m_first_input_window_render(true)
    , m_dirty(false)
{
}

void GLGizmoBase::set_hover_id(int id)
{
    // do not change hover id during dragging
    assert(!m_dragging);

    // allow empty grabbers when not using grabbers but use hover_id - flatten, rotate
    if (!m_grabbers.empty() && id >= (int) m_grabbers.size())
        return;
    
    m_hover_id = id;
    on_set_hover_id();    
}

bool GLGizmoBase::update_items_state()
{
    bool res = m_dirty;
    m_dirty  = false;
    return res;
}

ColorRGBA GLGizmoBase::picking_color_component(unsigned int id) const
{
    id = BASE_ID - id;
    if (m_group_id > -1)
        id -= m_group_id;

    return picking_decode(id);
}

void GLGizmoBase::render_grabbers(const BoundingBoxf3& box) const
{
    render_grabbers((float)((box.size().x() + box.size().y() + box.size().z()) / 3.0));
}

void GLGizmoBase::render_grabbers(float size) const
{
#if ENABLE_GL_SHADERS_ATTRIBUTES
    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light_attr");
#else
    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
#endif // ENABLE_GL_SHADERS_ATTRIBUTES
    if (shader == nullptr)
        return;
    shader->start_using();
    shader->set_uniform("emission_factor", 0.1f);
    for (int i = 0; i < (int)m_grabbers.size(); ++i) {
        if (m_grabbers[i].enabled)
            m_grabbers[i].render(m_hover_id == i, size);
    }
    shader->stop_using();
}

void GLGizmoBase::render_grabbers_for_picking(const BoundingBoxf3& box) const
{
#if ENABLE_LEGACY_OPENGL_REMOVAL
#if ENABLE_GL_SHADERS_ATTRIBUTES
    GLShaderProgram* shader = wxGetApp().get_shader("flat_attr");
#else
    GLShaderProgram* shader = wxGetApp().get_shader("flat");
#endif // ENABLE_GL_SHADERS_ATTRIBUTES
    if (shader != nullptr) {
        shader->start_using();
#endif // ENABLE_LEGACY_OPENGL_REMOVAL
        const float mean_size = float((box.size().x() + box.size().y() + box.size().z()) / 3.0);

        for (unsigned int i = 0; i < (unsigned int)m_grabbers.size(); ++i) {
            if (m_grabbers[i].enabled) {
                m_grabbers[i].color = picking_color_component(i);
                m_grabbers[i].render_for_picking(mean_size);
            }
        }
#if ENABLE_LEGACY_OPENGL_REMOVAL
        shader->stop_using();
    }
#endif // ENABLE_LEGACY_OPENGL_REMOVAL
}

// help function to process grabbers
// call start_dragging, stop_dragging, on_dragging
bool GLGizmoBase::use_grabbers(const wxMouseEvent &mouse_event) {
    bool is_dragging_finished = false;
    if (mouse_event.Moving()) { 
        // it should not happen but for sure
        assert(!m_dragging);
        if (m_dragging) is_dragging_finished = true;
        else return false; 
    } 

    if (mouse_event.LeftDown()) {
        Selection &selection = m_parent.get_selection();        
        if (!selection.is_empty() && m_hover_id != -1 && 
            (m_grabbers.empty() || m_hover_id < static_cast<int>(m_grabbers.size()))) {
            selection.setup_cache();

            m_dragging = true;
            for (auto &grabber : m_grabbers) grabber.dragging = false;
            if (!m_grabbers.empty() && m_hover_id < int(m_grabbers.size()))
                m_grabbers[m_hover_id].dragging = true;            
            
            // prevent change of hover_id during dragging
            m_parent.set_mouse_as_dragging();
            on_start_dragging();

            // Let the plater know that the dragging started
            m_parent.post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_STARTED));
            m_parent.set_as_dirty();
            return true;
        }
    } else if (m_dragging) {
        // when mouse cursor leave window than finish actual dragging operation
        bool is_leaving = mouse_event.Leaving();
        if (mouse_event.Dragging()) {
            m_parent.set_mouse_as_dragging();
            Point      mouse_coord(mouse_event.GetX(), mouse_event.GetY());
            auto       ray = m_parent.mouse_ray(mouse_coord);
            UpdateData data(ray, mouse_coord);

            on_dragging(data);

            wxGetApp().obj_manipul()->set_dirty();
            m_parent.set_as_dirty();
            return true;
        } else if (mouse_event.LeftUp() || is_leaving || is_dragging_finished) {
            for (auto &grabber : m_grabbers) grabber.dragging = false;
            m_dragging = false;

            // NOTE: This should be part of GLCanvas3D
            // Reset hover_id when leave window
            if (is_leaving) m_parent.mouse_up_cleanup();

            on_stop_dragging();

            // There is prediction that after draggign, data are changed
            // Data are updated twice also by canvas3D::reload_scene.
            // Should be fixed.
            m_parent.get_gizmos_manager().update_data(); 

            wxGetApp().obj_manipul()->set_dirty();

            // Let the plater know that the dragging finished, so a delayed
            // refresh of the scene with the background processing data should
            // be performed.
            m_parent.post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED));
            // updates camera target constraints
            m_parent.refresh_camera_scene_box();
            return true;
        }
    }
    return false;
}

std::string GLGizmoBase::format(float value, unsigned int decimals) const
{
    return Slic3r::string_printf("%.*f", decimals, value);
}

void GLGizmoBase::set_dirty() {
    m_dirty = true;
}

void GLGizmoBase::render_input_window(float x, float y, float bottom_limit)
{
    on_render_input_window(x, y, bottom_limit);
    if (m_first_input_window_render) {
        // for some reason, the imgui dialogs are not shown on screen in the 1st frame where they are rendered, but show up only with the 2nd rendered frame
        // so, we forces another frame rendering the first time the imgui window is shown
        m_parent.set_as_dirty();
        m_first_input_window_render = false;
    }
}



std::string GLGizmoBase::get_name(bool include_shortcut) const
{
    int key = get_shortcut_key();
    std::string out = on_get_name();
    if (include_shortcut && key >= WXK_CONTROL_A && key <= WXK_CONTROL_Z)
        out += std::string(" [") + char(int('A') + key - int(WXK_CONTROL_A)) + "]";
    return out;
}


} // namespace GUI
} // namespace Slic3r
