// Adventure3D.cpp : Ce fichier contient la fonction 'main'. L'exécution du programme commence et se termine à cet endroit.
//

#include <iostream>

#include <pandaFramework.h>
#include <pandaSystem.h>
#include <buttonThrower.h>
#include <mouseWatcher.h>
#include <pgTop.h>
#include "cOnscreenText.h"

#include <imgui.h>

#include "adventure_3d_game.hpp"

//#include "world.h"


void setup_render(Adventure3D* panda3d_imgui_helper)
{
    auto task_mgr = AsyncTaskManager::get_global_ptr();

    // NOTE: ig_loop has process_events and 50 sort.
    PT(GenericAsyncTask) new_frame_imgui_task = new GenericAsyncTask("new_frame_imgui", [](GenericAsyncTask*, void* user_data) {
        static_cast<Adventure3D*>(user_data)->new_frame_imgui();
        return AsyncTask::DS_cont;
        }, panda3d_imgui_helper);
    new_frame_imgui_task->set_sort(0);
    task_mgr->add(new_frame_imgui_task);

    PT(GenericAsyncTask) render_imgui_task = new GenericAsyncTask("render_imgui", [](GenericAsyncTask*, void* user_data) {
        static_cast<Adventure3D*>(user_data)->render_imgui();
        return AsyncTask::DS_cont;
        }, panda3d_imgui_helper);
    render_imgui_task->set_sort(40);
    task_mgr->add(render_imgui_task);
}

void setup_button(WindowFramework* window_framework, Adventure3D* panda3d_imgui_helper)
{
    if (auto bt = window_framework->get_mouse().find("kb-events"))
    {
        auto ev_handler = EventHandler::get_global_event_handler();

        ButtonThrower* bt_node = DCAST(ButtonThrower, bt.node());
        std::string ev_name;

        ev_name = bt_node->get_button_down_event();
        if (ev_name.empty())
        {
            ev_name = "imgui-button-down";
            bt_node->set_button_down_event(ev_name);
        }
        ev_handler->add_hook(ev_name, [](const Event* ev, void* user_data) {
            const auto& key_name = ev->get_parameter(0).get_string_value();
            const auto& button = ButtonRegistry::ptr()->get_button(key_name);
            static_cast<Adventure3D*>(user_data)->on_button_down_or_up(button, true);
            }, panda3d_imgui_helper);

        ev_name = bt_node->get_button_up_event();
        if (ev_name.empty())
        {
            ev_name = "imgui-button-up";
            bt_node->set_button_up_event(ev_name);
        }
        ev_handler->add_hook(ev_name, [](const Event* ev, void* user_data) {
            const auto& key_name = ev->get_parameter(0).get_string_value();
            const auto& button = ButtonRegistry::ptr()->get_button(key_name);
            static_cast<Adventure3D*>(user_data)->on_button_down_or_up(button, false);
            }, panda3d_imgui_helper);

        ev_name = bt_node->get_keystroke_event();
        if (ev_name.empty())
        {
            ev_name = "imgui-keystroke";
            bt_node->set_keystroke_event(ev_name);
        }
        ev_handler->add_hook(ev_name, [](const Event* ev, void* user_data) {
            wchar_t keycode = ev->get_parameter(0).get_wstring_value()[0];
            static_cast<Adventure3D*>(user_data)->on_keystroke(keycode);
            }, panda3d_imgui_helper);
    }
}

void setup_mouse(WindowFramework* window_framework)
{
    window_framework->enable_keyboard();
    auto mouse_watcher = window_framework->get_mouse();
    auto mouse_watcher_node = DCAST(MouseWatcher, mouse_watcher.node());
    DCAST(PGTop, window_framework->get_pixel_2d().node())->set_mouse_watcher(mouse_watcher_node);
}

void on_imgui_new_frame()
{
    static bool show_demo_window = true;
    static bool show_another_window = false;
    static LVecBase3f clear_color = LVecBase3f(0);

    // 1. Show a simple window.
    // Tip: if we don't call ImGui::Begin()/ImGui::End() the widgets automatically appears in a window called "Debug".
    {
        static float f = 0.0f;
        static int counter = 0;
        ImGui::Text("Hello, world!");                           // Display some text (you can use a format string too)
        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f    
        ImGui::ColorEdit3("clear color", (float*)&clear_color[0]); // Edit 3 floats representing a color

        ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our windows open/close state
        ImGui::Checkbox("Another Window", &show_another_window);

        if (ImGui::Button("Button"))                            // Buttons return true when clicked (NB: most widgets return true when edited/activated)
            counter++;
        ImGui::SameLine();
        ImGui::Text("counter = %d", counter);

        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    }

    // 2. Show another simple window. In most cases you will use an explicit Begin/End pair to name your windows.
    if (show_another_window)
    {
        ImGui::Begin("Another Window", &show_another_window);
        ImGui::Text("Hello from another window!");
        if (ImGui::Button("Close Me"))
            show_another_window = false;
        ImGui::End();
    }

    // 3. Show the ImGui demo window. Most of the sample code is in ImGui::ShowDemoWindow(). Read its code to learn more about Dear ImGui!
    if (show_demo_window)
    {
        ImGui::SetNextWindowPos(ImVec2(650, 20), ImGuiCond_FirstUseEver); // Normally user code doesn't need/want to call this because positions are saved in .ini file anyway. Here we just want to make the demo initial state a bit more friendly!
        ImGui::ShowDemoWindow(&show_demo_window);
    }
}

COnscreenText title("title", COnscreenText::TS_plain);

PandaFramework framework;

void displayConsoleLog(const Event* eventPtr, void* dataPtr)
{
    title.set_text("aaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    std::cout << "Pressed M!" << std::endl;
    return;
}

void changeScene(const Event* eventPtr, void* dataPtr)
{
    std::cout << "Change Scene" << std::endl;
    framework.close_all_windows();
    //framework.open_framework();
    framework.set_window_title("Super Amandine3D - Scene 2");
    // open the window
    WindowFramework* window_framework = framework.open_window();
    GraphicsWindow* window = window_framework->get_graphics_window();
    // setup Panda3D mouse for pixel2d
    setup_mouse(window_framework);


    // use if the context is in different DLL.
    //ImGui::SetCurrentContext(panda3d_imgui_helper.get_context());



    //World world = World(window_framework);
    //world.~World();
    // do the main loop, equal to run() in python
    framework.main_loop();
    return;
}


int main(int argc, char* argv[])
{
    std::cout << argc << std::endl;
    // open a new window framework

    framework.open_framework();

    // set the window title to My Panda3D Window
    framework.set_window_title("Super Amandine3D");

    // open the window
    WindowFramework* window_framework = framework.open_window();
    GraphicsWindow* window = window_framework->get_graphics_window();


    // setup Panda3D mouse for pixel2d
    setup_mouse(window_framework);

    // This creates the on screen title that is in every tutorial
    title.set_text("Mon premier jeu Panda3D");
    title.set_fg(Colorf(1, 1, 1, 1));
    title.set_pos(LVecBase2f(0.0, -0.95));
    title.set_scale(0.07);
    title.reparent_to(window_framework->get_aspect_2d());


    // setup ImGUI for Panda3D
    Adventure3D panda3d_imgui_helper(window, window_framework->get_pixel_2d());
    panda3d_imgui_helper.setup_style();
    panda3d_imgui_helper.setup_geom();
    panda3d_imgui_helper.setup_shader(Filename("shader"));
    panda3d_imgui_helper.setup_font();
    panda3d_imgui_helper.setup_event();
    panda3d_imgui_helper.on_window_resized();
    panda3d_imgui_helper.enable_file_drop();

    // setup Panda3D task and key event
    setup_render(&panda3d_imgui_helper);
    setup_button(window_framework, &panda3d_imgui_helper);

    EventHandler::get_global_event_handler()->add_hook("window-event", [](const Event*, void* user_data) {
        static_cast<Adventure3D*>(user_data)->on_window_resized();
        }, &panda3d_imgui_helper);

    // use if the context is in different DLL.
    //ImGui::SetCurrentContext(panda3d_imgui_helper.get_context());

    EventHandler::get_global_event_handler()->add_hook(Adventure3D::NEW_FRAME_EVENT_NAME, [](const Event*) {
        // draw my GUI
        on_imgui_new_frame();
        });

    window_framework->get_panda_framework()->define_key("m", "sysExit", displayConsoleLog, NULL);
    window_framework->get_panda_framework()->define_key("n", "sysExit", changeScene, NULL);

    std::cout << "Before main_loop()" << std::endl;

    if (argc == 2 && strcmp(argv[1], "robots") == 0)
    {
        std::cout << argv[1] << std::endl;
        //Robots robots = Robots(window_framework);
        //robots.~Robots();
        // do the main loop, equal to run() in python
        framework.main_loop();
    }
    else
    {
        // Create an instance of our class
        
        // REMPLACER INSTANCIATION
        
        //World world = World(window_framework);
        panda3d_imgui_helper.init_scene(window_framework);
        
        // FIN REMPLACEMENT INSTANCIATION
        
        //world.~World();
        // do the main loop, equal to run() in python
        framework.main_loop();

    }






    std::cout << "After main_loop()" << std::endl;

    // close the window framework
    framework.close_framework();



    return 0;
}