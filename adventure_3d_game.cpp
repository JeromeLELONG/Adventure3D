/**
 * MIT License
 *
 * Copyright (c) 2018-2019 Younguk Kim (bluekyu)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */



#include <cstring>

#include <imgui.h>

#include <throw_event.h>
#include <nodePathCollection.h>
#include <mouseButton.h>
#include <colorAttrib.h>
#include <colorBlendAttrib.h>
#include <depthTestAttrib.h>
#include <cullFaceAttrib.h>
#include <scissorAttrib.h>
#include <geomNode.h>
#include <geomTriangles.h>
#include <graphicsWindow.h>


#include "cOnscreenText.h"
#include "genericFunctionInterval.h"
#include "texturePool.h"
#include "ambientLight.h"
#include "directionalLight.h"
#include "genericAsyncTask.h"
#include "waitInterval.h"
#include "cIntervalManager.h"
#include "adventure_3d_game.hpp"

const double PI = 3.14159265;

#if defined(__WIN32__) || defined(_WIN32)
#include <WinUser.h>
#include <shellapi.h>
#endif

class Adventure3D::WindowProc : public GraphicsWindowProc
{
public:
    WindowProc(Adventure3D& p3d_imgui) : p3d_imgui_(p3d_imgui)
    {
    }

#if defined(__WIN32__) || defined(_WIN32)
    LONG wnd_proc(GraphicsWindow* graphicsWindow, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) override
    {
        switch (msg)
        {
            case WM_DROPFILES:
            {
                HDROP hdrop = (HDROP)wparam;
                POINT pt;
                DragQueryPoint(hdrop, &pt);
                p3d_imgui_.dropped_point_ = LVecBase2(static_cast<PN_stdfloat>(pt.x), static_cast<PN_stdfloat>(pt.y));

                const UINT file_count = DragQueryFileW(hdrop, 0xFFFFFFFF, NULL, 0);

                std::vector<wchar_t> buffer;
                p3d_imgui_.dropped_files_.clear();
                for (UINT k = 0; k < file_count; ++k)
                {
                    UINT buffer_size = DragQueryFileW(hdrop, k, NULL, 0) + 1;       // #char + \0
                    buffer.resize(buffer_size);
                    UINT ret = DragQueryFileW(hdrop, k, buffer.data(), buffer_size);
                    if (ret)
                    {
                        p3d_imgui_.dropped_files_.push_back(Filename::from_os_specific_w(std::wstring(buffer.begin(), buffer.end() - 1)));
                    }
                }

                throw_event(DROPFILES_EVENT_NAME);

                break;
            }

            default:
            {
                break;
            }
        }

        return 0;
    }
#endif

private:
    Adventure3D& p3d_imgui_;
};

// ************************************************************************************************

Adventure3D::Adventure3D(GraphicsWindow* window, NodePath parent) 
    : window_(window),
    m_pandasNp(P_pandas),
    m_modelsNp(P_pandas)
{
    root_ = parent.attach_new_node("imgui-root", 1000);

    context_ = ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();

    // Setup back-end capabilities flags
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;
}

Adventure3D::~Adventure3D()
{
#if defined(__WIN32__) || defined(_WIN32)
    if (enable_file_drop_)
    {
        if (window_.is_valid_pointer())
        {
            window_->remove_window_proc(window_proc_.get());
            window_proc_.reset();
            if (auto handle = window_->get_window_handle())
                DragAcceptFiles((HWND)handle->get_int_handle(), FALSE);
        }
    }
#endif

    ImGui::DestroyContext();
    context_ = nullptr;
}


void Adventure3D::init_scene(WindowFramework* windowFrameworkPtr)
{
    m_windowFrameworkPtr = windowFrameworkPtr;
    scene_loaded = true;
    // preconditions
    if (m_windowFrameworkPtr == NULL)
    {
        nout << "ERROR: parameter windowFrameworkPtr cannot be NULL." << endl;
        return;
    }


    // Set the background color
    m_windowFrameworkPtr->get_display_region_3d()->set_clear_color(Colorf(0.6, 0.6, 1, 1));
    // Allow manual positioning of the camera
    // Note: in that state by default in C++
    NodePath cameraNp = m_windowFrameworkPtr->get_camera_group();
    // Set the cameras' position and orientation
    cameraNp.set_pos_hpr(0, -8, 2.5, 0, -9, 0);

    // Load and position our models
    load_models();
    // Add some basic lighting
    setup_lights();
    // Create the needed intervals and put the carousel into motion
    start_carousel();

    m_windowFrameworkPtr->get_panda_framework()->define_key("escape", "sysExit", sys_exit, NULL);
    m_windowFrameworkPtr->get_panda_framework()->define_key("o", "removeNode", removeNode, this);
    m_windowFrameworkPtr->get_panda_framework()->define_key("p", "addNodes", addNodes, this);
}

void Adventure3D::setup_style(Style style)
{
    switch (style)
    {
    case Style::dark:
        ImGui::StyleColorsDark();
        break;
    case Style::classic:
        ImGui::StyleColorsClassic();
        break;
    case Style::light:
        ImGui::StyleColorsLight();
        break;
    }
}

void Adventure3D::setup_geom()
{
    PT(GeomVertexArrayFormat) array_format = new GeomVertexArrayFormat(
        InternalName::get_vertex(), 4, Geom::NT_stdfloat, Geom::C_point,
        InternalName::get_color(), 1, Geom::NT_packed_dabc, Geom::C_color
    );

    vformat_ = GeomVertexFormat::register_format(new GeomVertexFormat(array_format));

    root_.set_state(RenderState::make(
        ColorAttrib::make_vertex(),
        ColorBlendAttrib::make(ColorBlendAttrib::M_add, ColorBlendAttrib::O_incoming_alpha, ColorBlendAttrib::O_one_minus_incoming_alpha),
        DepthTestAttrib::make(DepthTestAttrib::M_none),
        CullFaceAttrib::make(CullFaceAttrib::M_cull_none)
    ));
}

void Adventure3D::setup_shader(const Filename& shader_dir_path)
{
    root_.set_shader(Shader::load(
        Shader::SL_GLSL,
        shader_dir_path / "panda3d_imgui.vert.glsl",
        shader_dir_path / "panda3d_imgui.frag.glsl",
        "",
        "",
        ""));
}

void Adventure3D::setup_shader(Shader* shader)
{
    root_.set_shader(shader);
}

void Adventure3D::setup_font()
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault();
    setup_font_texture();
}

void Adventure3D::setup_font(const char* font_filename, float font_size)
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF(font_filename, font_size);
    setup_font_texture();
}

void Adventure3D::setup_event()
{
    ImGuiIO& io = ImGui::GetIO();

    // for button holder although the variable is not used.
    button_map_ = window_->get_keyboard_map();

    io.KeyMap[ImGuiKey_Tab] = KeyboardButton::tab().get_index();
    io.KeyMap[ImGuiKey_LeftArrow] = KeyboardButton::left().get_index();
    io.KeyMap[ImGuiKey_RightArrow] = KeyboardButton::right().get_index();
    io.KeyMap[ImGuiKey_UpArrow] = KeyboardButton::up().get_index();
    io.KeyMap[ImGuiKey_DownArrow] = KeyboardButton::down().get_index();
    io.KeyMap[ImGuiKey_PageUp] = KeyboardButton::page_up().get_index();
    io.KeyMap[ImGuiKey_PageDown] = KeyboardButton::page_down().get_index();
    io.KeyMap[ImGuiKey_Home] = KeyboardButton::home().get_index();
    io.KeyMap[ImGuiKey_End] = KeyboardButton::end().get_index();
    io.KeyMap[ImGuiKey_Insert] = KeyboardButton::insert().get_index();
    io.KeyMap[ImGuiKey_Delete] = KeyboardButton::del().get_index();
    io.KeyMap[ImGuiKey_Backspace] = KeyboardButton::backspace().get_index();
    io.KeyMap[ImGuiKey_Space] = KeyboardButton::space().get_index();
    io.KeyMap[ImGuiKey_Enter] = KeyboardButton::enter().get_index();
    io.KeyMap[ImGuiKey_Escape] = KeyboardButton::escape().get_index();
    io.KeyMap[ImGuiKey_A] = KeyboardButton::ascii_key('a').get_index();
    io.KeyMap[ImGuiKey_C] = KeyboardButton::ascii_key('c').get_index();
    io.KeyMap[ImGuiKey_V] = KeyboardButton::ascii_key('v').get_index();
    io.KeyMap[ImGuiKey_X] = KeyboardButton::ascii_key('x').get_index();
    io.KeyMap[ImGuiKey_Y] = KeyboardButton::ascii_key('y').get_index();
    io.KeyMap[ImGuiKey_Z] = KeyboardButton::ascii_key('z').get_index();
}

void Adventure3D::enable_file_drop()
{
    // register file drop
#if defined(__WIN32__) || defined(_WIN32)
    enable_file_drop_ = true;
    if (window_.is_valid_pointer())
    {
        DragAcceptFiles((HWND)window_->get_window_handle()->get_int_handle(), TRUE);
        window_proc_ = std::make_unique<WindowProc>(*this);
        window_->add_window_proc(window_proc_.get());
    }
#endif
}

void Adventure3D::on_window_resized()
{
    if (window_.is_valid_pointer())
        on_window_resized(LVecBase2(static_cast<float>(window_->get_x_size()), static_cast<float>(window_->get_y_size())));
}

void Adventure3D::on_window_resized(const LVecBase2& size)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(size[0], size[1]);
    //io.DisplayFramebufferScale;
}

void Adventure3D::on_button_down_or_up(const ButtonHandle& button, bool down)
{
    if (button == ButtonHandle::none())
        return;

    ImGuiIO& io = ImGui::GetIO();
    if (MouseButton::is_mouse_button(button))
    {
        if (button == MouseButton::one())
        {
            io.MouseDown[0] = down;
        }
        else if (button == MouseButton::three())
        {
            io.MouseDown[1] = down;
        }
        else if (button == MouseButton::two())
        {
            io.MouseDown[2] = down;
        }
        else if (button == MouseButton::four())
        {
            io.MouseDown[3] = down;
        }
        else if (button == MouseButton::five())
        {
            io.MouseDown[4] = down;
        }
        else if (down)
        {
            if (button == MouseButton::wheel_up())
                io.MouseWheel += 1;
            else if (button == MouseButton::wheel_down())
                io.MouseWheel -= 1;
            else if (button == MouseButton::wheel_right())
                io.MouseWheelH += 1;
            else if (button == MouseButton::wheel_left())
                io.MouseWheelH -= 1;
        }
    }
    else
    {
        io.KeysDown[button.get_index()] = down;

        if (button == KeyboardButton::control())
            io.KeyCtrl = down;
        else if (button == KeyboardButton::shift())
            io.KeyShift = down;
        else if (button == KeyboardButton::alt())
            io.KeyAlt = down;
        else if (button == KeyboardButton::meta())
            io.KeySuper = down;
    }
}

void Adventure3D::on_keystroke(wchar_t keycode)
{
    if (keycode < 0 || keycode >= (std::numeric_limits<ImWchar>::max)())
        return;

    ImGuiIO& io = ImGui::GetIO();
    io.AddInputCharacter(keycode);
}

bool Adventure3D::new_frame_imgui()
{
    if (root_.is_hidden())
        return false;

    static const int MOUSE_DEVICE_INDEX = 0;

    ImGuiIO& io = ImGui::GetIO();

    io.DeltaTime = static_cast<float>(ClockObject::get_global_clock()->get_dt());

    if (window_.is_valid_pointer() && window_->is_of_type(GraphicsWindow::get_class_type()))
    {
        const auto& mouse = window_->get_pointer(MOUSE_DEVICE_INDEX);
        if (mouse.get_in_window())
        {
            if (io.WantSetMousePos)
            {
                window_->move_pointer(MOUSE_DEVICE_INDEX, io.MousePos.x, io.MousePos.y);
            }
            else
            {
                io.MousePos.x = static_cast<float>(mouse.get_x());
                io.MousePos.y = static_cast<float>(mouse.get_y());
            }
        }
        else
        {
            io.MousePos.x = -FLT_MAX;
            io.MousePos.y = -FLT_MAX;
        }
    }

    ImGui::NewFrame();

    throw_event_directly(*EventHandler::get_global_event_handler(), NEW_FRAME_EVENT_NAME);

    return true;
}

bool Adventure3D::render_imgui()
{
    if (root_.is_hidden())
        return false;

    ImGui::Render();

    ImGuiIO& io = ImGui::GetIO();
    const float fb_width = io.DisplaySize.x * io.DisplayFramebufferScale.x;
    const float fb_height = io.DisplaySize.y * io.DisplayFramebufferScale.y;

    auto draw_data = ImGui::GetDrawData();
    //draw_data->ScaleClipRects(io.DisplayFramebufferScale);

    auto npc = root_.get_children();
    for (int k = 0, k_end = npc.get_num_paths(); k < k_end; ++k)
        npc.get_path(k).detach_node();

    for (int k = 0; k < draw_data->CmdListsCount; ++k)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[k];

        if (!(k < static_cast<int>(geom_data_.size())))
        {
            geom_data_.push_back({
                new GeomVertexData("imgui-vertex-" + std::to_string(k), vformat_, GeomEnums::UsageHint::UH_stream),
                {}
            });
        }

        auto& geom_list = geom_data_[k];

        auto vertex_handle = geom_list.vdata->modify_array_handle(0);
        if (vertex_handle->get_num_rows() < cmd_list->VtxBuffer.Size)
            vertex_handle->unclean_set_num_rows(cmd_list->VtxBuffer.Size);

        std::memcpy(
            vertex_handle->get_write_pointer(),
            reinterpret_cast<const unsigned char*>(cmd_list->VtxBuffer.Data),
            cmd_list->VtxBuffer.Size * sizeof(decltype(cmd_list->VtxBuffer)::value_type));

        auto idx_buffer_data = cmd_list->IdxBuffer.Data;
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; ++cmd_i)
        {
            const ImDrawCmd* draw_cmd = &cmd_list->CmdBuffer[cmd_i];
            auto elem_count = static_cast<int>(draw_cmd->ElemCount);

            if (!(cmd_i < static_cast<int>(geom_list.nodepaths.size())))
                geom_list.nodepaths.push_back(create_geomnode(geom_list.vdata));

            NodePath np = geom_list.nodepaths[cmd_i];
            np.reparent_to(root_);

            auto gn = DCAST(GeomNode, np.node());

            auto index_handle = gn->modify_geom(0)->modify_primitive(0)->modify_vertices(elem_count)->modify_handle();
            if (index_handle->get_num_rows() < elem_count)
                index_handle->unclean_set_num_rows(elem_count);

            std::memcpy(
                index_handle->get_write_pointer(),
                reinterpret_cast<const unsigned char*>(idx_buffer_data),
                elem_count * sizeof(decltype(cmd_list->IdxBuffer)::value_type));
            idx_buffer_data += elem_count;

            CPT(RenderState) state = RenderState::make(ScissorAttrib::make(
                draw_cmd->ClipRect.x / fb_width,
                draw_cmd->ClipRect.z / fb_width,
                1 - draw_cmd->ClipRect.w / fb_height,
                1 - draw_cmd->ClipRect.y / fb_height));

            if (draw_cmd->TextureId)
                state = state->add_attrib(TextureAttrib::make(static_cast<Texture*>(draw_cmd->TextureId)));

            gn->set_geom_state(0, state);
        }
    }

    return true;
}

void Adventure3D::setup_font_texture()
{
    ImGuiIO& io = ImGui::GetIO();

    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);

    font_texture_ = Texture::make_texture();
    font_texture_->set_name("imgui-font-texture");
    font_texture_->setup_2d_texture(width, height, Texture::ComponentType::T_unsigned_byte, Texture::Format::F_red);
    font_texture_->set_minfilter(SamplerState::FilterType::FT_linear);
    font_texture_->set_magfilter(SamplerState::FilterType::FT_linear);

    PTA_uchar ram_image = font_texture_->make_ram_image();
    std::memcpy(ram_image.p(), pixels, width * height * sizeof(decltype(*pixels)));

    io.Fonts->TexID = font_texture_.p();
}

NodePath Adventure3D::create_geomnode(const GeomVertexData* vdata)
{
    PT(GeomTriangles) prim = new GeomTriangles(GeomEnums::UsageHint::UH_stream);

    static_assert(
        sizeof(ImDrawIdx) == sizeof(uint16_t) ||
        sizeof(ImDrawIdx) == sizeof(uint32_t),
        "Type of ImDrawIdx is not uint16_t or uint32_t. Update below code!"
        );
    if (sizeof(ImDrawIdx) == sizeof(uint16_t))
        prim->set_index_type(GeomEnums::NumericType::NT_uint16);
    else if (sizeof(ImDrawIdx) == sizeof(uint32_t))
        prim->set_index_type(GeomEnums::NumericType::NT_uint32);

    prim->close_primitive();

    PT(Geom) geom = new Geom(vdata);
    geom->add_primitive(prim);

    PT(GeomNode) geom_node = new GeomNode("imgui-geom");
    geom_node->add_geom(geom, RenderState::make_empty());

    return NodePath(geom_node);
}


void Adventure3D::sys_exit(const Event* eventPtr, void* dataPtr)
{
    exit(0);
}

void Adventure3D::removeNode(const Event* eventPtr, void* dataPtr)
{
    if (static_cast<Adventure3D*>(dataPtr)->scene_loaded)
    {
        static_cast<Adventure3D*>(dataPtr)->m_pandasNp[0].remove_node();
        static_cast<Adventure3D*>(dataPtr)->m_carouselNp.remove_node();
        static_cast<Adventure3D*>(dataPtr)->m_envNp.remove_node();
        static_cast<Adventure3D*>(dataPtr)->scene_loaded = false;
    }
    
}

void Adventure3D::addNodes(const Event* eventPtr, void* dataPtr)
{
    if (!static_cast<Adventure3D*>(dataPtr)->scene_loaded)
    {
        // Load and position our models
        static_cast<Adventure3D*>(dataPtr)->load_models();
        // Add some basic lighting
        static_cast<Adventure3D*>(dataPtr)->setup_lights();
        // Create the needed intervals and put the carousel into motion
        static_cast<Adventure3D*>(dataPtr)->start_carousel();
        static_cast<Adventure3D*>(dataPtr)->scene_loaded = true;
    }
}


void Adventure3D::load_models()
{
    // Load the carousel base
    NodePath modelsNp = m_windowFrameworkPtr->get_panda_framework()->get_models();
    m_carouselNp = m_windowFrameworkPtr->load_model(modelsNp, "./models/carousel_base");
    // Attach it to render
    NodePath renderNp = m_windowFrameworkPtr->get_render();
    m_carouselNp.reparent_to(renderNp);

    // Load the modeled lights that are on the outer rim of the carousel
    // (not Panda lights)
    // There are 2 groups of lights. At any given time, one group will have the
    // "on" texture and the other will have the "off" texture.
    m_lights1Np = m_windowFrameworkPtr->load_model(modelsNp, "./models/carousel_lights");
    m_lights1Np.reparent_to(m_carouselNp);

    // Load the 2nd set of lights
    m_lights2Np = m_windowFrameworkPtr->load_model(modelsNp, "./models/carousel_lights");
    // We need to rotate the 2nd so it doesn't overlap with the 1st set.
    m_lights2Np.set_h(36);
    m_lights2Np.reparent_to(m_carouselNp);

    // Load the textures for the lights. One texture is for the "on" state,
    // the other is for the "off" state.
    m_lightOffTexPtr = TexturePool::load_texture("./models/carousel_lights_off.jpg");
    m_lightOnTexPtr = TexturePool::load_texture("./models/carousel_lights_on.jpg");

    // Create an list (m_pandasNp) with filled with 4 dummy nodes attached to
    // the carousel.
    // This uses a python concept called "Array Comprehensions." Check the Python
    // manual for more information on how they work
    for (int i = 0; i < P_pandas; ++i)
    {
        string nodeName("panda");
        nodeName += i;
        m_pandasNp[i] = m_carouselNp.attach_new_node(nodeName);
        m_modelsNp[i] = m_windowFrameworkPtr->load_model(modelsNp, "./models/carousel_panda");
        // Note: we'll be using a task, we won't need these
        // self.moves = [0 for i in range(4)]

        // set the position and orientation of the ith panda node we just created
        // The Z value of the position will be the base height of the pandas.
        // The headings are multiplied by i to put each panda in its own position
        // around the carousel
        m_pandasNp[i].set_pos_hpr(0, 0, 1.3, i * 90, 0, 0);


        // Load the actual panda model, and parent it to its dummy node
        m_modelsNp[i].reparent_to(m_pandasNp[i]);
        // Set the distance from the center. This distance is based on the way the
        // carousel was modeled in Maya
        m_modelsNp[i].set_y(.85);
    }

    // Load the environment (Sky sphere and ground plane)
    m_envNp = m_windowFrameworkPtr->load_model(modelsNp, "./models/env");
    m_envNp.reparent_to(renderNp);
    m_envNp.set_scale(7);
}

// Panda Lighting
void Adventure3D::setup_lights()
{
    NodePath renderNp = m_windowFrameworkPtr->get_render();

    // Create some lights and add them to the scene. By setting the lights on
    // render they affect the entire scene
    // Check out the lighting tutorial for more information on lights
    PT(AmbientLight) ambientLightPtr = new AmbientLight("ambientLight");
    if (ambientLightPtr != NULL)
    {
        ambientLightPtr->set_color(Colorf(0.4, 0.4, 0.35, 1));
        renderNp.set_light(renderNp.attach_new_node(ambientLightPtr));
    }
    PT(DirectionalLight) directionalLightPtr = new DirectionalLight("directionalLight");
    if (directionalLightPtr != NULL)
    {
        directionalLightPtr->set_direction(LVecBase3f(0, 8, -2.5));
        directionalLightPtr->set_color(Colorf(0.9, 0.8, 0.9, 1));
        renderNp.set_light(renderNp.attach_new_node(directionalLightPtr));
    }

    // Explicitly set the environment to not be lit
    m_envNp.set_light_off();
}

void Adventure3D::start_carousel()
{
    // Here's where we actually create the intervals to move the carousel
    // The first type of interval we use is one created directly from a NodePath
    // This interval tells the NodePath to vary its orientation (hpr) from its
    // current value (0,0,0) to (360,0,0) over 20 seconds. Intervals created from
    // NodePaths also exist for position, scale, color, and shear
    m_carouselSpinIntervalPtr = new CLerpNodePathInterval("carouselSpinInterval",
        20,
        CLerpNodePathInterval::BT_no_blend,
        true,
        false,
        m_carouselNp,
        NodePath());
    if (m_carouselSpinIntervalPtr != NULL)
    {
        m_carouselSpinIntervalPtr->set_start_hpr(LVecBase3f(0, 0, 0));
        m_carouselSpinIntervalPtr->set_end_hpr(LVecBase3f(360, 0, 0));

        // Once an interval is created, we need to tell it to actually move.
        // start() will cause an interval to play once. loop() will tell an interval
        // to repeat once it finished. To keep the carousel turning, we use loop()
        m_carouselSpinIntervalPtr->loop();
    }

    // The next type of interval we use is called a LerpFunc interval. It is
    // called that because it linearly interpolates (aka Lerp) values passed to
    // a function over a given amount of time.

    // In this specific case, horses on a carousel don't move constantly up,
    // suddenly stop, and then constantly move down again. Instead, they start
    // slowly, get fast in the middle, and slow down at the top. This motion is
    // close to a sine wave. This LerpFunc calls the function oscilatePanda
    // (which we will create below), which changes the height of the panda based
    // on the sin of the value passed in. In this way we achieve non-linear
    // motion by linearly changing the input to a function
    m_lerpFuncPtrVec.resize(P_pandas);
    m_lerpFuncPtrVec[P_panda1] = oscillate_panda<P_panda1>;
    m_lerpFuncPtrVec[P_panda2] = oscillate_panda<P_panda2>;
    m_lerpFuncPtrVec[P_panda3] = oscillate_panda<P_panda3>;
    m_lerpFuncPtrVec[P_panda4] = oscillate_panda<P_panda4>;


    m_moveIntervalPtrVec.resize(P_pandas);
    for (int i = 0; i < P_pandas; ++i)
    {
        string intervalName("moveInterval");
        intervalName += i;
        m_moveIntervalPtrVec[i] = new DoubleLerpFunctionInterval(intervalName,
            m_lerpFuncPtrVec[i],
            this,
            3,
            0,
            2 * PI,
            DoubleLerpFunctionInterval::BT_no_blend);
        if (m_moveIntervalPtrVec[i] != NULL)
        {
            m_moveIntervalPtrVec[i]->loop();
        }
    }

    // Finally, we combine Sequence, Parallel, Func, and Wait intervals,
    // to schedule texture swapping on the lights to simulate the lights turning
    // on and off.
    // Sequence intervals play other intervals in a sequence. In other words,
    // it waits for the current interval to finish before playing the next
    // one.
    // Parallel intervals play a group of intervals at the same time
    // Wait intervals simply do nothing for a given amount of time
    // Func intervals simply make a single function call. This is helpful because
    // it allows us to schedule functions to be called in a larger sequence. They
    // take virtually no time so they don't cause a Sequence to wait.

    m_lightBlinkIntervalPtr = new CMetaInterval("lightBlinkInterval");

    if (m_lightBlinkIntervalPtr != NULL)
    {
        // For the first step in our sequence we will set the on texture on one
        // light and set the off texture on the other light at the same time
        m_lightBlinkIntervalPtr->add_c_interval(new GenericFunctionInterval("lights1OnInterval",
            call_blink_lights<L_light1, B_blink_on>,
            this,
            true));
        m_lightBlinkIntervalPtr->add_c_interval(new GenericFunctionInterval("lights2OffInterval",
            call_blink_lights<L_light2, B_blink_off>,
            this,
            true),
            0,
            CMetaInterval::RS_previous_begin);
        // Then we will wait 1 second
        m_lightBlinkIntervalPtr->add_c_interval(new WaitInterval(0.1));

        // Then we will switch the textures at the same time
        m_lightBlinkIntervalPtr->add_c_interval(new GenericFunctionInterval("lights1OffInterval",
            call_blink_lights<L_light1, B_blink_off>,
            this,
            true));
        m_lightBlinkIntervalPtr->add_c_interval(new GenericFunctionInterval("lights2OnInterval",
            call_blink_lights<L_light2, B_blink_on>,
            this,
            true),
            0,
            CMetaInterval::RS_previous_begin);
        // Then we will wait another second
        m_lightBlinkIntervalPtr->add_c_interval(new WaitInterval(0.1));

        // Loop this sequence continuously
        m_lightBlinkIntervalPtr->loop();
    }

    // Note: setup a task to step the interval manager
    AsyncTaskManager::get_global_ptr()->add(new GenericAsyncTask("intervalManagerTask",
        step_interval_manager,
        this));
}

template<int pandaId>
void Adventure3D::oscillate_panda(const double& rad, void* dataPtr)
{
    // preconditions
    if (dataPtr == NULL)
    {
        nout << "ERROR: parameter dataPtr cannot be NULL." << endl;
        return;
    }

    double offset = PI * (pandaId % 2);
    static_cast<Adventure3D*>(dataPtr)->m_modelsNp[pandaId].set_z(sin(rad + offset) * 0.2);
}

template<int lightId, int blinkId>
void Adventure3D::call_blink_lights(void* dataPtr)
{
    // preconditions
    if (dataPtr == NULL)
    {
        nout << "ERROR: parameter dataPtr cannot be NULL." << endl;
        return;
    }

    static_cast<Adventure3D*>(dataPtr)->blink_lights((LightId)lightId, (BlinkId)blinkId);
}

void Adventure3D::blink_lights(LightId lightId, BlinkId blinkId)
{
    NodePath lightsNp;
    switch (lightId)
    {
    case L_light1:
        lightsNp = m_lights1Np;
        break;
    case L_light2:
        lightsNp = m_lights2Np;
        break;
    default:
        nout << "ERROR: forgot a LightId?" << endl;
        return;
    }

    switch (blinkId)
    {
    case B_blink_on:
        lightsNp.set_texture(m_lightOnTexPtr);
        break;
    case B_blink_off:
        lightsNp.set_texture(m_lightOffTexPtr);
        break;
    default:
        nout << "ERROR: forgot a BlinkId?" << endl;
        return;
    }
}

AsyncTask::DoneStatus Adventure3D::step_interval_manager(GenericAsyncTask* taskPtr, void* dataPtr)
{
    CIntervalManager::get_global_ptr()->step();
    return AsyncTask::DS_cont;
}