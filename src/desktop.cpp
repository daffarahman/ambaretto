#include "desktop.hpp"
#include "city.hpp"
#include "ui_font.hpp"
#include <raylib.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define NOGDI
#define NOUSER
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace ambaretto {
namespace {
const char* names[] = {"Cities", "Buildings", "Cars", "Characters", "Settings"};
const char* flags[] = {"--cities", "--building-builder", "--car-editor", "--character-creator", "--settings"};
std::filesystem::path own_session;
struct App {
    std::filesystem::path session, draft;
#ifdef _WIN32
    HANDLE process;
#else
    pid_t process;
#endif
};
std::vector<App> apps;
bool button(Rectangle r, const char* label, bool enabled = true) {
    const bool hover = enabled && IsWindowFocused() && CheckCollisionPointRec(GetMousePosition(),r);
    DrawRectangleRec(r,hover ? ui::dos_yellow : ui::dos_blue);
    DrawRectangleLinesEx(r,1,ui::dos_white);
    ui::draw_text(label,int(r.x+12),int(r.y+(r.height-18)/2),18,enabled ? (hover ? ui::dos_blue : ui::dos_white) : ui::dos_light_blue);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
void remove_file(const std::filesystem::path& path) {
    std::error_code ignored; if (!path.empty()) std::filesystem::remove(path,ignored);
}
void reap(bool preserve_play = true) {
    apps.erase(std::remove_if(apps.begin(),apps.end(),[&](const App& app) {
        std::error_code ec;
        if (preserve_play && std::filesystem::exists(app.session.string()+".play",ec)) return false;
#ifdef _WIN32
        if (WaitForSingleObject(app.process,0)!=WAIT_OBJECT_0) return false;
        CloseHandle(app.process);
#else
        if (waitpid(app.process,nullptr,WNOHANG)==0) return false;
#endif
        remove_file(app.session.string()+".close"); remove_file(app.session.string()+".play"); remove_file(app.draft);
        return true;
    }),apps.end());
}
bool signal_close(std::string& error) {
    for (const auto& app : apps) {
        std::ofstream file(app.session.string()+".close");
        if (!file) { error = "Cannot ask an editor to close. Close its window manually."; return false; }
        file << "close\n";
    }
    return true;
}
bool pending_play(City& city, const std::filesystem::path& directory, std::string& error) {
    for (const auto& app : apps) {
        const auto path = app.session.string()+".play";
        std::ifstream file(path); if (!file) continue;
        std::string id; std::getline(file,id); file.close(); remove_file(path);
        if (id.size()!=16 || id.find_first_not_of("0123456789abcdef")!=std::string::npos) { error="Invalid city play request"; continue; }
        if (City::load(directory/(id+".city"),city,error) && city.playable()) return true;
        if (error.empty()) error="Choose a player character and spawn before playing.";
    }
    return false;
}
#ifdef _WIN32
std::wstring quote(const std::wstring& value) {
    std::wstring out=L"\""; unsigned slashes=0;
    for (wchar_t c : value) {
        if (c==L'\\') { ++slashes; continue; }
        out.append(c==L'"' ? slashes*2+1 : slashes,L'\\'); slashes=0; out+=c;
    }
    out.append(slashes*2,L'\\'); return out+L"\"";
}
#endif
}
void set_app_session(const std::filesystem::path& session) { own_session=session; }
bool app_should_close() {
    if (WindowShouldClose()) return true;
    if (own_session.empty()) return false;
    const auto path=own_session.string()+".close";
    std::error_code error; if (!std::filesystem::exists(path,error)) return false;
    return true;
}
void cancel_app_close() { if (!own_session.empty()) remove_file(own_session.string()+".close"); }
bool launch_app(DesktopApp app, std::string& error, const std::filesystem::path& draft) {
    reap();
    const auto root=std::filesystem::path(GetApplicationDirectory());
    const auto directory=root/".desktop";
    std::error_code ec; std::filesystem::create_directories(directory,ec);
    if (ec) { error="Cannot create desktop session: "+ec.message(); return false; }
    static unsigned serial=0;
#ifdef _WIN32
    const auto pid=GetCurrentProcessId();
#else
    const auto pid=getpid();
#endif
    const auto session=directory/(std::to_string(pid)+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial));
    std::filesystem::path copy;
    if (!draft.empty()) {
        copy=session; copy+=draft.extension();
        std::filesystem::copy_file(draft,copy,ec);
        if (ec) { error="Cannot open draft: "+ec.message(); return false; }
    }
#ifdef _WIN32
    const auto executable=root/"Ambaretto.exe";
    std::wstring command=quote(executable.wstring())+L" "+std::filesystem::path(flags[int(app)]).wstring()+L" --session "+quote(session.wstring());
    if (!copy.empty()) command+=L" --design "+quote(copy.wstring());
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&startup,&process)) {
        error="Cannot open app (Windows error "+std::to_string(GetLastError())+")"; remove_file(copy); return false;
    }
    CloseHandle(process.hThread); apps.push_back({session,copy,process.hProcess});
#else
    const auto executable=(root/"Ambaretto").string();
    const auto session_arg=session.string(), draft_arg=copy.string();
    std::vector<char*> args{const_cast<char*>(executable.c_str()),const_cast<char*>(flags[int(app)]),const_cast<char*>("--session"),const_cast<char*>(session_arg.c_str())};
    if (!copy.empty()) { args.push_back(const_cast<char*>("--design")); args.push_back(const_cast<char*>(draft_arg.c_str())); }
    args.push_back(nullptr); pid_t process;
    const auto result=posix_spawn(&process,executable.c_str(),nullptr,nullptr,args.data(),environ);
    if (result) { error="Cannot open app (error "+std::to_string(result)+")"; remove_file(copy); return false; }
    apps.push_back({session,copy,process});
#endif
    error=std::string(names[int(app)])+" opened in a separate window"; return true;
}
bool close_apps() {
    reap(false); if (apps.empty()) return true;
    std::string status; signal_close(status); bool interactive=false;
    while (true) {
        reap(false); if (apps.empty()) return true;
        BeginDrawing(); ui::draw_desktop();
        const Rectangle r{(GetScreenWidth()-720)/2.f,(GetScreenHeight()-260)/2.f,720,260};
        ui::draw_window(r,"Close editor windows");
        ui::draw_text("Save or discard drafts in the open editor windows.",int(r.x+24),int(r.y+62),18,ui::dos_white);
        ui::draw_text("All app windows must close before continuing.",int(r.x+24),int(r.y+100),18,ui::dos_white);
        ui::draw_text(status.c_str(),int(r.x+24),int(r.y+140),16,ui::dos_yellow);
        const bool retry=button({r.x+24,r.y+194,320,42},"Ask editors to close",interactive);
        const bool cancel=button({r.x+376,r.y+194,320,42},"Cancel",interactive) || (interactive && IsKeyPressed(KEY_ESCAPE)) || WindowShouldClose();
        EndDrawing(); interactive=true;
        if (retry) signal_close(status);
        if (cancel) { for (const auto& app : apps) remove_file(app.session.string()+".close"); return false; }
    }
}
bool request_play(const City& city, std::string& error) {
    if (own_session.empty()) return true;
    const auto temp=own_session.string()+".tmp", path=own_session.string()+".play";
    std::ofstream file(temp); file<<city.id<<'\n'; file.close();
    if (!file) { error="Cannot send city to the desktop"; return false; }
    std::error_code ec; std::filesystem::rename(temp,path,ec);
    if (ec) { error="Cannot send city to the desktop: "+ec.message(); return false; }
    return true;
}
bool desktop_menu(City& city, const std::filesystem::path& directory, const std::string& screenshot) {
    EnableCursor(); SetWindowTitle("Ambaretto - Desktop");
    std::string status="Open an app. Each editor has its own window and draft.";
    int selected=0, frames=0; bool interactive=false;
    while (true) {
        if (pending_play(city,directory,status)) { if (close_apps()) return true; status="Game start cancelled"; }
        reap();
        if (app_should_close()) { if (close_apps()) return false; }
        if (interactive && IsKeyPressed(KEY_RIGHT)) selected=(selected+1)%5;
        if (interactive && IsKeyPressed(KEY_LEFT)) selected=(selected+4)%5;
        int open=interactive && IsKeyPressed(KEY_ENTER) ? selected : -1;
        BeginDrawing(); ui::draw_desktop();
        DrawRectangle(0,0,GetScreenWidth(),36,ui::dos_white);
        ui::draw_text("AMBARETTO / DESKTOP",24,8,20,ui::dos_blue);
        ui::draw_text("Create something. Then take it for a drive.",48,88,24,ui::dos_yellow);
        const char* details[]={"Create or open a map", "Create or edit a building", "Create or edit a car", "Create or edit a character", "Graphics and controls"};
        for (int i=0;i<5;++i) {
            const float x=48.f+i*((GetScreenWidth()-96.f)/5);
            const Rectangle icon{x,164,152,148};
            const bool hover=CheckCollisionPointRec(GetMousePosition(),icon);
            if (interactive && hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { selected=i; open=i; }
            const Color color=selected==i || hover ? ui::dos_yellow : ui::dos_white;
            const int ix=int(x+36), iy=178;
            if (i==0) {
                for (int tower=0;tower<3;++tower) DrawRectangleLinesEx({float(ix+tower*26),float(iy+24-tower*8),22,float(40+tower*8)},3,color);
                DrawLine(ix-6,iy+70,ix+82,iy+70,color);
            } else if (i==1) {
                DrawRectangleLinesEx({float(ix+8),float(iy),64,70},3,color);
                for (int row=0;row<3;++row) for (int col=0;col<3;++col) DrawRectangle(ix+18+col*16,iy+12+row*16,7,7,color);
                DrawRectangleLines(ix+34,iy+54,14,16,color);
            } else if (i==2) {
                DrawRectangleLinesEx({float(ix),float(iy+30),80,26},3,color);
                DrawRectangleLinesEx({float(ix+18),float(iy+12),44,22},3,color);
                DrawCircle(ix+17,iy+60,9,color); DrawCircle(ix+63,iy+60,9,color);
            } else if (i==3) {
                DrawCircle(ix+40,iy+10,10,color);
                DrawRectangle(ix+29,iy+25,22,28,color);
                DrawLineEx({float(ix+29),float(iy+30)},{float(ix+12),float(iy+48)},5,color);
                DrawLineEx({float(ix+51),float(iy+30)},{float(ix+68),float(iy+48)},5,color);
                DrawLineEx({float(ix+34),float(iy+53)},{float(ix+27),float(iy+72)},6,color);
                DrawLineEx({float(ix+46),float(iy+53)},{float(ix+53),float(iy+72)},6,color);
            } else {
                DrawCircleLines(ix+40,iy+34,25,color); DrawCircleLines(ix+40,iy+34,10,color);
                for (int tooth=0;tooth<8;++tooth) {
                    const float angle=tooth*PI/4;
                    DrawLineEx({ix+40+std::cos(angle)*25,iy+34+std::sin(angle)*25},{ix+40+std::cos(angle)*36,iy+34+std::sin(angle)*36},5,color);
                }
            }
            const auto label=names[i]; ui::draw_text(label,int(x+(152-ui::measure_text(label,20))/2),268,20,color);
            if (hover || selected==i) ui::draw_text(details[i],48,354,20,ui::dos_white);
        }
        ui::draw_text("Click an app / Arrow keys + Enter",48,408,18,ui::dos_white);
        DrawRectangle(0,GetScreenHeight()-48,GetScreenWidth(),48,ui::dos_white);
        ui::draw_text(status.c_str(),24,GetScreenHeight()-33,16,ui::dos_blue);
        const bool quit=button({float(GetScreenWidth()-144),6,128,28},"Quit",interactive);
        EndDrawing(); interactive=true;
        if (quit && close_apps()) return false;
        if (open>=0) launch_app(DesktopApp(open),status);
        if (!screenshot.empty() && ++frames>=3) { auto capture=LoadImageFromScreen(); ExportImage(capture,screenshot.c_str()); UnloadImage(capture); return false; }
    }
}
}
