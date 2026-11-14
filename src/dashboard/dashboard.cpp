// Native dashboard. Engine access is serialized on one worker; HWNDs stay on the UI thread.
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <algorithm>
#include <commctrl.h>
#include <commdlg.h>
#include <cstdio>
#include <functional>
#include <thread>
#include <sstream>
#include <fstream>
#include <cmath>
#include "dashboard_model.h"
#include "frame_source.h"
#include "image_io.h"
#include "fs_compat.h"
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

namespace {
const UINT completed = WM_APP + 1;
const int design_width = 1015, design_height = 745;
// Resolve newer APIs at runtime so the same source builds with VS2015's 8.1 SDK.
void enable_dpi_awareness() {
    HMODULE user = GetModuleHandleW(L"user32.dll");
    typedef BOOL (WINAPI* SetContext)(HANDLE);
    auto context = reinterpret_cast<SetContext>(GetProcAddress(user,"SetProcessDpiAwarenessContext"));
    if(context && context(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)))) return;
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if(shcore) {
        typedef HRESULT (WINAPI* SetAwareness)(int);
        auto awareness = reinterpret_cast<SetAwareness>(GetProcAddress(shcore,"SetProcessDpiAwareness"));
        HRESULT result = awareness ? awareness(2) : E_NOTIMPL;
        FreeLibrary(shcore);
        if(SUCCEEDED(result) || result == E_ACCESSDENIED) return;
    }
    SetProcessDPIAware();
}
UINT monitor_dpi(HMONITOR monitor) {
    UINT x=96,y=96;
    HMODULE shcore=LoadLibraryW(L"shcore.dll");
    if(shcore) {
        typedef HRESULT (WINAPI* GetMonitorDpi)(HMONITOR,int,UINT*,UINT*);
        auto get=reinterpret_cast<GetMonitorDpi>(GetProcAddress(shcore,"GetDpiForMonitor"));
        if(!get || FAILED(get(monitor,0,&x,&y))) x=96;
        FreeLibrary(shcore);
    }
    return x;
}
enum { Profile=100, Database, BrowseDb, Open, Close, Load, Capture, Camera,
       Enroll, Authenticate, Id, Name, Card, Rename, SetCard, Refresh, Remove, Backup, People, Status, Preview };
std::wstring wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, 0); if(n) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n); return out;
}
std::string utf8(const std::wstring& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, 0); if(n) WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, nullptr, nullptr); return out;
}
struct Window {
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    struct Placement { HWND child; int x,y,width,height; };
    std::vector<Placement> placements;
    UINT dpi=96;
    double font_scale=0;
    bool arranging=false;
    fa::DashboardModel model;
    std::thread worker;
    bool busy = false, opened = false;
    std::string message;
    fa::Image preview; // UI copy: never accessed by the worker.
    std::vector<fa::Record> rows; // UI snapshot, stable while editing a selection.
    HWND control(int id) { return GetDlgItem(hwnd, id); }
    std::wstring text(int id) {
        HWND h = control(id); int n = GetWindowTextLengthW(h);
        std::wstring s(n + 1, 0); GetWindowTextW(h, &s[0], n + 1); s.resize(n); return s;
    }
    void set(int id, const std::wstring& s) { SetWindowTextW(control(id), s.c_str()); }
    HWND add(const wchar_t* cls, const wchar_t* caption, DWORD style, int id, int x, int y, int w, int h) {
        HWND child = CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0,
            cls, caption, WS_CHILD|WS_VISIBLE|style, x,y,w,h,hwnd,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
        placements.push_back({child,x,y,w,h});
        return child;
    }
    void label(const wchar_t* s, int x,int y,int w) { add(L"STATIC",s,0,0,x,y,w,22); }
    void button(const wchar_t* s,int id,int x,int y,int w) { add(L"BUTTON",s,WS_TABSTOP,id,x,y,w,30); }
    void create() {
        dpi=monitor_dpi(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST));
        font = CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        label(L"FACE AUTHENTICATION",20,14,450);
        label(L"Register people, recognize a face, and manage your database.",20,40,700);
        label(L"Model profile",20,80,110);
        add(L"EDIT",L"models\\permissive.ini",WS_TABSTOP|ES_AUTOHSCROLL,Profile,135,76,345,27);
        label(L"Database",20,115,110);
        add(L"EDIT",L"build\\dashboard_people.db",WS_TABSTOP|ES_AUTOHSCROLL,Database,135,111,345,27);
        button(L"Choose...",BrowseDb,490,110,95); button(L"Open / create",Open,595,110,125); button(L"Close",Close,730,110,85);
        label(L"1  Face photo",20,160,450);
        add(L"STATIC",L"Load a photo or capture a camera frame",SS_OWNERDRAW,Preview,20,190,460,290);
        button(L"Load photo",Load,20,490,120);
        button(L"Capture camera",Capture,150,490,145);
        add(L"EDIT",L"0",WS_TABSTOP|ES_NUMBER,Camera,310,492,40,27);
        label(L"Camera index",360,496,120);
        label(L"2  Person details",510,160,450);
        label(L"Person ID",510,198,120); add(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,Id,635,194,360,27);
        label(L"Name",510,236,120); add(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,Name,635,232,360,27);
        label(L"RF card (optional)",510,274,125); add(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,Card,635,270,360,27);
        button(L"Register face",Enroll,510,316,150); button(L"Authenticate face",Authenticate,675,316,180);
        label(L"Authentication uses the card above when supplied.",510,358,480);
        label(L"Use exactly one face. Recognition does not test liveness.",510,387,490);
        label(L"3  Registered people",510,433,450);
        label(L"Select a row to edit its name, card, or face photo.",510,463,480);
        HWND list = add(WC_LISTVIEWW,L"",WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,People,510,492,485,190);
        ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
        const wchar_t* names[] = { L"Person ID",L"Name",L"RF card",L"Dimension" };
        int widths[] = {110,155,125,80};
        for(int i=0;i<4;++i) { LVCOLUMNW c={}; c.mask=LVCF_TEXT|LVCF_WIDTH; c.pszText=const_cast<wchar_t*>(names[i]);c.cx=widths[i];ListView_InsertColumn(list,i,&c); }
        button(L"Save name",Rename,510,694,105); button(L"Save card",SetCard,625,694,105);
        button(L"Replace face",Refresh,740,694,120); button(L"Delete person",Remove,870,694,125);
        button(L"Export backup",Backup,20,545,145);
        add(L"EDIT",L"Open or create a database to begin. Changes are saved immediately.",
            ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,Status,20,595,460,130);
        enable(); layout();
    }
    void layout() {
        if(arranging || placements.empty() || IsIconic(hwnd)) return;
        arranging=true;
        RECT client={};
        double scale=1;
        int canvas_width=0,canvas_height=0;
        // SetScrollInfo can show/hide bars, changing the client size. Recalculate once.
        for(int pass=0;pass<2;++pass) {
            GetClientRect(hwnd,&client);
            const double fit=(std::min)(double(client.right)/design_width,double(client.bottom)/design_height);
            // Keep controls readable on a small display; scroll rather than clip or distort them.
            scale=(std::max)(0.8*dpi/96.0,fit);
            canvas_width=(int)std::lround(design_width*scale);
            canvas_height=(int)std::lround(design_height*scale);
            SCROLLINFO horizontal={sizeof(SCROLLINFO),SIF_RANGE|SIF_PAGE};
            horizontal.nMax=canvas_width-1;horizontal.nPage=(UINT)(std::max)(0L,client.right);
            SCROLLINFO vertical={sizeof(SCROLLINFO),SIF_RANGE|SIF_PAGE};
            vertical.nMax=canvas_height-1;vertical.nPage=(UINT)(std::max)(0L,client.bottom);
            SetScrollInfo(hwnd,SB_HORZ,&horizontal,TRUE);SetScrollInfo(hwnd,SB_VERT,&vertical,TRUE);
        }
        GetClientRect(hwnd,&client);
        const int left=(std::max)(0,(int(client.right)-canvas_width)/2)-GetScrollPos(hwnd,SB_HORZ);
        const int top=(std::max)(0,(int(client.bottom)-canvas_height)/2)-GetScrollPos(hwnd,SB_VERT);
        HFONT previous=nullptr;
        if(std::abs(font_scale-scale)>0.001) {
            HFONT next=CreateFontW(-(int)std::lround(16*scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                                   DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
            if(next) {previous=font;font=next;font_scale=scale;}
        }
        HDWP positions=BeginDeferWindowPos((int)placements.size());
        for(const Placement& p:placements) {
            const int x=left+(int)std::lround(p.x*scale), y=top+(int)std::lround(p.y*scale);
            const int width=(int)std::lround(p.width*scale), height=(int)std::lround(p.height*scale);
            if(positions) positions=DeferWindowPos(positions,p.child,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
            else SetWindowPos(p.child,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
            SendMessageW(p.child,WM_SETFONT,(WPARAM)font,FALSE);
        }
        if(positions) EndDeferWindowPos(positions);
        if(previous) DeleteObject(previous);
        RECT list={};GetClientRect(control(People),&list);
        // Reserve a scrollbar width even with no rows, so adding rows doesn't clip the last column.
        const int usable=(std::max)(0,int(list.right)-int(std::lround(20*scale)));
        const int weights[]={110,155,125,80};int used=0;
        for(int i=0;i<4;++i) {
            const int width=i==3 ? usable-used : MulDiv(usable,weights[i],470);
            ListView_SetColumnWidth(control(People),i,width);used+=width;
        }
        RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
        arranging=false;
    }
    void scroll(int bar,UINT request) {
        SCROLLINFO info={sizeof(SCROLLINFO),SIF_ALL};GetScrollInfo(hwnd,bar,&info);
        int next=info.nPos;
        const int step=MulDiv(32,dpi,96);
        switch(request) {
        case SB_LINEUP:next-=step;break;
        case SB_LINEDOWN:next+=step;break;
        case SB_PAGEUP:next-=(int)info.nPage;break;
        case SB_PAGEDOWN:next+=(int)info.nPage;break;
        case SB_THUMBTRACK:case SB_THUMBPOSITION:next=info.nTrackPos;break;
        case SB_TOP:next=info.nMin;break;
        case SB_BOTTOM:next=info.nMax;break;
        default:return;
        }
        SetScrollPos(hwnd,bar,next,TRUE);layout();
    }
    void enable() {
        for(int id : {Profile,Database,BrowseDb,Open}) EnableWindow(control(id),!busy&&!opened);
        for(int id : {Close,Load,Capture,Camera,Enroll,Authenticate,Id,Name,Card,Rename,SetCard,Refresh,Remove,Backup,People})
            EnableWindow(control(id),!busy&&opened);
    }
    void start(std::function<std::string()> action) {
        if(busy) return;
        busy=true; enable(); set(Status,L"Working...");
        worker=std::thread([this,action] {
            HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            try { message=action(); } catch(const std::exception& e) { message=std::string("ERROR: ")+e.what(); }
            catch(...) { message="ERROR: Operation failed."; }
            if(SUCCEEDED(hr)) CoUninitialize();
            PostMessageW(hwnd,completed,0,0);
        });
    }
    void finish() {
        worker.join(); busy=false; opened=model.pipeline!=nullptr;
        rows.clear(); preview=model.photo;
        if(opened) rows=model.pipeline->store().records();
        ListView_DeleteAllItems(control(People));
        for(size_t i=0;i<rows.size();++i) {
            std::wstring values[]={wide(rows[i].id),wide(rows[i].name),wide(rows[i].card_id),std::to_wstring(rows[i].feature.dim())};
            LVITEMW item={};item.mask=LVIF_TEXT;item.iItem=(int)i;item.pszText=&values[0][0];
            ListView_InsertItem(control(People),&item);
            for(int col=1;col<4;++col) ListView_SetItemText(control(People),(int)i,col,&values[col][0]);
        }
        set(Status,wide(message+"\r\n"+(opened ? std::to_string(rows.size())+" registered people. Database: "+model.pipeline->store().path() : "Database closed.")));
        InvalidateRect(control(Preview),nullptr,TRUE); enable();
    }
    std::wstring choose(bool save,const wchar_t* filter,bool overwrite=true,const wchar_t* extension=L"db") {
        wchar_t path[32768]={}; OPENFILENAMEW dialog={};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=hwnd;
        dialog.lpstrFilter=filter;dialog.lpstrDefExt=extension;dialog.lpstrFile=path;dialog.nMaxFile=32768;
        dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?(overwrite?OFN_OVERWRITEPROMPT:0):OFN_FILEMUSTEXIST);
        return (save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog)) ? path : L"";
    }
    std::string selected() {
        int index=ListView_GetNextItem(control(People),-1,LVNI_SELECTED);
        if(index<0 || index>=(int)rows.size()) throw std::runtime_error("Select a registered person first.");
        return rows[index].id;
    }
    void command(int id) {
        if(busy) return;
        try {
            if(id==BrowseDb) {
                auto path=choose(true,L"Face database (*.db;*.csv)\0*.db;*.csv;*.sqlite;*.sqlite3\0All files\0*.*\0",false);
                if(!path.empty()) set(Database,path); return;
            }
            if(id==Open) {
                auto profile=utf8(text(Profile)),db=utf8(text(Database));
                start([this,profile,db]{model.open(profile,db);return "Database opened. Ready to register or authenticate.";});return;
            }
            if(id==Close) {start([this]{model.pipeline.reset();model.photo=fa::Image();return "Database closed.";});return;}
            if(id==Load) {
                auto path=choose(false,L"Face photo (*.jpg;*.png;*.bmp)\0*.jpg;*.jpeg;*.png;*.bmp\0All files\0*.*\0");
                if(path.empty()) return;
                start([this,path]{fa::Image image;model.require(fa::load_image(utf8(path),image),"Cannot read this photo.");model.photo=std::move(image);return "Photo loaded. Ready to register or authenticate.";});return;
            }
            if(id==Capture) {
                int index=std::stoi(text(Camera));
                start([this,index]{auto camera=fa::open_camera_source(index);model.require(camera!=nullptr,"Cannot open the camera. Check its index and Windows camera permissions.");
                    fa::Image image;double time=0;model.require(camera->next(image,time),"Cannot capture a camera frame.");model.photo=std::move(image);return "Camera frame captured. Ready to register or authenticate.";});return;
            }
            std::string person=utf8(text(Id)),name=utf8(text(Name)),card=utf8(text(Card));
            if(id==Enroll) {start([this,person,name,card]{model.enroll(person,name,card);return "Registered: "+name;});return;}
            if(id==Authenticate) {start([this,card]{return model.authenticate(card);});return;}
            if(id==Backup) {
                auto path=choose(true,L"CSV backup (*.csv)\0*.csv\0SQLite backup (*.db)\0*.db\0All files\0*.*\0",true,L"csv");
                if(path.empty()) return;
                start([this,path]{auto& store=model.ready().store();
                    wchar_t source[32768],target[32768];
                    model.require(GetFullPathNameW(wide(store.path()).c_str(),32768,source,nullptr)!=0 && GetFullPathNameW(path.c_str(),32768,target,nullptr)!=0,"Cannot resolve the backup path.");
                    model.require(_wcsicmp(source,target)!=0,"Choose a backup file different from the open database.");
                    bool ok=store.save(utf8(path));model.require(ok,"Backup failed: "+store.last_error());return "Backup exported: "+utf8(path);});return;
            }
            if(id!=Rename&&id!=SetCard&&id!=Refresh&&id!=Remove) return;
            person=selected();
            if(id==Remove && MessageBoxW(hwnd,(L"Delete person "+wide(person)+L" from the database?").c_str(),L"Delete person",MB_YESNO|MB_ICONQUESTION)!=IDYES) return;
            if(id==Refresh && MessageBoxW(hwnd,L"Replace this person's face with the currently loaded photo?",L"Replace face",MB_YESNO|MB_ICONQUESTION)!=IDYES) return;
            start([this,id,person,name,card]{
                auto& p=model.ready();bool ok=false;
                if(id==Rename) {model.require(!name.empty(),"Name is required.");ok=p.rename_or_refresh(person,name,nullptr);}
                if(id==SetCard) ok=p.store().set_card_id(person,card);
                if(id==Refresh) {model.single_face();ok=p.rename_or_refresh(person,"",&model.photo);}
                if(id==Remove) ok=p.remove_person(person);
                model.require(ok,"Database change failed: "+p.store().last_error());
                p.reset_tracking();return "Saved database change for "+person;
            });
        } catch(const std::exception& e) {set(Status,wide(e.what()));}
    }
    void select() {
        int i=ListView_GetNextItem(control(People),-1,LVNI_SELECTED);
        if(i>=0&&i<(int)rows.size()) {set(Id,wide(rows[i].id));set(Name,wide(rows[i].name));set(Card,wide(rows[i].card_id));}
    }
    void draw(DRAWITEMSTRUCT* d) {
        FillRect(d->hDC,&d->rcItem,(HBRUSH)GetStockObject(BLACK_BRUSH));
        if(preview.empty()) {SetTextColor(d->hDC,RGB(220,220,220));SetBkMode(d->hDC,TRANSPARENT);
            DrawTextW(d->hDC,L"Load a photo or capture a camera frame",-1,&d->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);return;}
        // A 32-bit DIB avoids the 4-byte row padding required by 24-bit GDI.
        std::vector<unsigned char> pixels((size_t)preview.width*preview.height*4);
        for(int y=0;y<preview.height;++y) for(int x=0;x<preview.width;++x) {
            auto src=preview.row(y)+x*3;auto dst=&pixels[((size_t)y*preview.width+x)*4];
            dst[0]=src[0];dst[1]=src[1];dst[2]=src[2];dst[3]=255;
        }
        BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=preview.width;
        info.bmiHeader.biHeight=-preview.height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        double scale=(std::min)((double)(d->rcItem.right-d->rcItem.left)/preview.width,(double)(d->rcItem.bottom-d->rcItem.top)/preview.height);
        int w=(int)(preview.width*scale),h=(int)(preview.height*scale);
        SetStretchBltMode(d->hDC,HALFTONE);
        StretchDIBits(d->hDC,(d->rcItem.right-w)/2,(d->rcItem.bottom-h)/2,w,h,0,0,preview.width,preview.height,pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
    }
};
LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    Window* app=(Window*)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if(msg==WM_NCCREATE) {app=(Window*)((CREATESTRUCTW*)l)->lpCreateParams;app->hwnd=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)app);}
    if(!app) return DefWindowProcW(hwnd,msg,w,l);
    switch(msg) {
    case WM_CREATE:app->create();return 0;
    case WM_SIZE:app->layout();return 0;
    case WM_DPICHANGED: {
        app->dpi=HIWORD(w);
        RECT* suggested=reinterpret_cast<RECT*>(l);
        SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,
                     suggested->right-suggested->left,suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);
        app->layout();return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* limits=reinterpret_cast<MINMAXINFO*>(l);
        limits->ptMinTrackSize.x=MulDiv(320,app->dpi,96);
        limits->ptMinTrackSize.y=MulDiv(240,app->dpi,96);return 0;
    }
    case WM_VSCROLL:app->scroll(SB_VERT,LOWORD(w));return 0;
    case WM_HSCROLL:app->scroll(SB_HORZ,LOWORD(w));return 0;
    case WM_MOUSEWHEEL: {
        int turns=GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA;
        int bar=(GET_KEYSTATE_WPARAM(w)&MK_SHIFT)?SB_HORZ:SB_VERT;
        for(int i=0;i<std::abs(turns)*3;++i)app->scroll(bar,turns>0?SB_LINEUP:SB_LINEDOWN);
        return 0;
    }
    case WM_MOUSEHWHEEL: {
        int turns=GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA;
        for(int i=0;i<std::abs(turns)*3;++i)app->scroll(SB_HORZ,turns>0?SB_LINEDOWN:SB_LINEUP);
        return 0;
    }
    case WM_COMMAND:if(HIWORD(w)==BN_CLICKED)app->command(LOWORD(w));return 0;
    case WM_NOTIFY:if(((NMHDR*)l)->idFrom==People&&((NMHDR*)l)->code==LVN_ITEMCHANGED)app->select();return 0;
    case WM_DRAWITEM:if(((DRAWITEMSTRUCT*)l)->CtlID==Preview)app->draw((DRAWITEMSTRUCT*)l);return TRUE;
    case completed:app->finish();return 0;
    case WM_CLOSE:if(app->busy) {MessageBoxW(hwnd,L"Wait for the current operation to finish before closing.",L"Operation running",MB_OK);return 0;}DestroyWindow(hwnd);return 0;
    case WM_DESTROY:DeleteObject(app->font);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,w,l);
}

int self_test() {
    std::ofstream report("build/dashboard_selftest.log");
    try {
        const std::string base="build/dashboard_selftest_"+std::to_string(GetCurrentProcessId());
        for(const auto& ext : {".csv",".db"}) {
            const std::string path=base+ext, backup=base+"_backup"+ext;
            fa::DashboardModel m;
            m.open("models/permissive.ini",path);
            m.require(fa::load_image("data/portraits/adam_kinzinger/117th.jpg",m.photo),"Test portrait unavailable.");
            m.enroll("DASH-1","Dashboard Test","CARD-1");
            m.require(m.authenticate("").find("RECOGNIZED:")==0,"Face authentication failed.");
            m.require(m.authenticate("CARD-1").find("RECOGNIZED:")==0,"Card authentication failed.");
            bool rejected=false;try {m.enroll("DASH-1","Duplicate","");} catch(...) {rejected=true;}
            m.require(rejected,"Duplicate registration accepted.");
            rejected=false;try {m.authenticate("UNKNOWN-CARD");} catch(...) {rejected=true;}
            m.require(rejected,"Unknown card accepted.");
            m.require(m.ready().rename_or_refresh("DASH-1","Renamed",&m.photo),"Refresh failed.");
            m.require(m.ready().store().set_card_id("DASH-1","CARD-2"),"Card update failed.");
            m.require(m.ready().store().save(backup),"Backup failed.");
            m.pipeline.reset();m.open("models/permissive.ini",path);
            m.require(m.ready().store().find_by_card("CARD-2")!=nullptr,"Reopened database lost its card.");
            m.require(m.authenticate("").find("Renamed")!=std::string::npos,"Rename not persisted.");
            m.require(m.ready().remove_person("DASH-1"),"Delete failed.");
            m.require(m.authenticate("").find("NOT RECOGNIZED")==0,"Empty gallery accepted a face.");
            m.pipeline.reset();m.open("models/permissive.ini",path);
            m.require(m.ready().store().count()==0,"Deletion did not persist.");
            m.pipeline.reset();DeleteFileW(wide(path).c_str());DeleteFileW(wide(backup).c_str());
            report<<"PASS "<<ext<<": enroll, face/card authentication, duplicate/unknown-card rejection, rename/refresh/card update, backup, reopen, delete\n";
        }
        report<<"ALL DASHBOARD CHECKS PASSED\n";return 0;
    } catch(const std::exception& e) {report<<"FAIL: "<<e.what()<<"\n";return 1;}
}
int layout_self_test(HINSTANCE instance) {
    std::ofstream report("build/dashboard_layout_selftest.log");
    Window app;
    HWND window=CreateWindowW(L"FaceAuthenticationDashboard",L"Dashboard layout check",
        WS_OVERLAPPEDWINDOW|WS_VSCROLL|WS_HSCROLL,0,0,1040,800,nullptr,nullptr,instance,&app);
    if(!window) {report<<"FAIL: cannot create dashboard window\n";return 1;}
    int failures=0;
    const int screens[][2]={{1024,768},{1280,720},{1366,768},{1920,1080},{2560,1440},{3840,2160}};
    const UINT densities[]={96,120,144,192};
    for(UINT density:densities) for(const auto& screen:screens) {
        RECT size={0,0,(std::min)(MulDiv(design_width,density,96),screen[0]-30),
                         (std::min)(MulDiv(design_height,density,96),screen[1]-80)};
        AdjustWindowRectEx(&size,WS_OVERLAPPEDWINDOW,FALSE,0);
        // Exercise the real DPI-change handler without changing the user's display settings.
        RECT suggested={0,0,size.right-size.left,size.bottom-size.top};
        SendMessageW(window,WM_DPICHANGED,MAKELONG(density,density),(LPARAM)&suggested);
        RECT client={};GetClientRect(window,&client);
        auto rectangle=[window](HWND child) {
            RECT rect={};GetWindowRect(child,&rect);
            MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&rect),2);return rect;
        };
        RECT photo=rectangle(app.control(Preview));
        const double horizontal=double(photo.right-photo.left)/460;
        const double vertical=double(photo.bottom-photo.top)/290;
        bool ok=std::abs(horizontal-vertical)<0.008;
        for(const Window::Placement& p:app.placements) {
            RECT rect=rectangle(p.child);
            ok=ok && std::abs((rect.right-rect.left)-p.width*horizontal)<=2;
            ok=ok && std::abs((rect.bottom-rect.top)-p.height*horizontal)<=2;
        }
        LOGFONTW font={};GetObjectW(app.font,sizeof(font),&font);
        ok=ok && std::abs(-font.lfHeight-16*horizontal)<=1;
        ok=ok && -font.lfHeight>=MulDiv(12,density,96);
        int columns=0;for(int i=0;i<4;++i)columns+=ListView_GetColumnWidth(app.control(People),i);
        RECT list={};GetClientRect(app.control(People),&list);
        ok=ok && columns<=list.right && columns>list.right-30*horizontal;
        app.scroll(SB_VERT,SB_BOTTOM);app.scroll(SB_HORZ,SB_BOTTOM);
        RECT last=rectangle(app.control(Remove));
        ok=ok && last.left>=0 && last.top>=0 && last.right<=client.right && last.bottom<=client.bottom;
        if(!ok)++failures;
        report<<(ok?"PASS ":"FAIL ")<<screen[0]<<"x"<<screen[1]<<" at "<<density
              <<" DPI: proportional controls/font/columns; final action reachable\n";
        app.scroll(SB_VERT,SB_TOP);app.scroll(SB_HORZ,SB_TOP);
    }
    DestroyWindow(window);
    report<<(failures?"LAYOUT CHECKS FAILED":"ALL LAYOUT CHECKS PASSED")<<" ("<<failures<<" failures)\n";
    return failures?1:0;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR args,int show) {
    enable_dpi_awareness();
    // Locate repository resources even when launched by Explorer from another folder.
    wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,32768);std::wstring dir=exe;dir=dir.substr(0,dir.find_last_of(L"\\/"));
    for(int i=0;i<6;++i) {
        if(GetFileAttributesW((dir+L"\\models\\permissive.ini").c_str())!=INVALID_FILE_ATTRIBUTES) {SetCurrentDirectoryW(dir.c_str());break;}
        auto pos=dir.find_last_of(L"\\/");if(pos==std::wstring::npos)break;dir.resize(pos);
    }
    HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int count=0;LPWSTR* arguments=CommandLineToArgvW(GetCommandLineW(),&count);
    bool test=count==2 && arguments && std::wstring(arguments[1])==L"--self-test";
    bool layout_test=count==2 && arguments && std::wstring(arguments[1])==L"--layout-self-test";
    if(arguments)LocalFree(arguments);
    if(test) {int result=self_test();if(SUCCEEDED(hr))CoUninitialize();return result;}
    INITCOMMONCONTROLSEX common={sizeof(common),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&common);
    WNDCLASSW wc={};wc.lpfnWndProc=procedure;wc.hInstance=instance;wc.lpszClassName=L"FaceAuthenticationDashboard";
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&wc);
    if(layout_test) {int result=layout_self_test(instance);if(SUCCEEDED(hr))CoUninitialize();return result;}
    Window app;
    POINT cursor={};GetCursorPos(&cursor);
    HMONITOR monitor=MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info={sizeof(MONITORINFO)};GetMonitorInfoW(monitor,&monitor_info);
    app.dpi=monitor_dpi(monitor);
    const DWORD style=WS_OVERLAPPEDWINDOW|WS_VSCROLL|WS_HSCROLL;
    RECT bounds={0,0,MulDiv(design_width,app.dpi,96),MulDiv(design_height,app.dpi,96)};
    typedef BOOL (WINAPI* AdjustForDpi)(LPRECT,DWORD,BOOL,DWORD,UINT);
    auto adjust=reinterpret_cast<AdjustForDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"AdjustWindowRectExForDpi"));
    if(adjust)adjust(&bounds,style,FALSE,0,app.dpi);else AdjustWindowRectEx(&bounds,style,FALSE,0);
    const RECT work=monitor_info.rcWork;
    const int width=(std::min)(bounds.right-bounds.left,(work.right-work.left)*9/10);
    const int height=(std::min)(bounds.bottom-bounds.top,(work.bottom-work.top)*9/10);
    HWND hwnd=CreateWindowW(wc.lpszClassName,L"Face authentication - Registration and database",style,
        work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,
        width,height,nullptr,nullptr,instance,&app);
    if(!hwnd)return 1;
    ShowWindow(hwnd,show);MSG msg;
    while(GetMessageW(&msg,nullptr,0,0)>0) {if(!IsDialogMessageW(hwnd,&msg)) {TranslateMessage(&msg);DispatchMessageW(&msg);}}
    if(SUCCEEDED(hr))CoUninitialize();return (int)msg.wParam;
}
