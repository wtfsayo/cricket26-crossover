/* Wine-window diagnostics only. No desktop/screen DC or host permission changes.
 * Default: enumerate visible Cricket windows and save their client GDI surfaces.
 * Input requires an explicit observed target: --enter <hex HWND> <decimal PID>.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static HDC (WINAPI *pCreateCompatibleDC)(HDC);
static HBITMAP (WINAPI *pCreateDIBSection)(HDC,const BITMAPINFO *,UINT,void **,HANDLE,DWORD);
static HGDIOBJ (WINAPI *pSelectObject)(HDC,HGDIOBJ);
static BOOL (WINAPI *pDeleteObject)(HGDIOBJ);
static BOOL (WINAPI *pDeleteDC)(HDC);
static BOOL (WINAPI *pBitBlt)(HDC,int,int,int,int,HDC,int,int,DWORD);
static BOOL (WINAPI *pGdiFlush)(void);
static HANDLE done;
static unsigned int matched;
static BOOL list_only;
static void out(const char *s) { DWORD wrote;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,strlen(s),&wrote,NULL); }
static void wide(const WCHAR *s) { char b[4096];int n=WideCharToMultiByte(CP_UTF8,0,s,-1,b,sizeof(b),NULL,NULL);if(n)out(b); }
static BOOL contains_cricket(const WCHAR *s)
{
    static const WCHAR needle[]=L"cricket";
    for(;*s;s++) {unsigned int i;for(i=0;needle[i];i++){WCHAR c=s[i];if(c>='A'&&c<='Z')c+=32;if(c!=needle[i])break;}if(!needle[i])return TRUE;}
    return FALSE;
}
static BOOL target_matches(HWND hwnd,DWORD *pid)
{
    WCHAR title[512],image[2048];DWORD n=ARRAYSIZE(image);HANDLE process;BOOL result;
    if(!IsWindow(hwnd)||!IsWindowVisible(hwnd))return FALSE;
    GetWindowThreadProcessId(hwnd,pid);GetWindowTextW(hwnd,title,ARRAYSIZE(title));
    result=contains_cricket(title);
    process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,*pid);
    if(process){if(QueryFullProcessImageNameW(process,0,image,&n)){WCHAR *base=wcsrchr(image,'\\');result=result||contains_cricket(base?base+1:image);}CloseHandle(process);}
    return result;
}
static void identify(HWND hwnd,DWORD pid)
{
    WCHAR title[512],cls[512];RECT r;char b[200];
    GetWindowTextW(hwnd,title,ARRAYSIZE(title));GetClassNameW(hwnd,cls,ARRAYSIZE(cls));GetClientRect(hwnd,&r);
    snprintf(b,sizeof(b),"window hwnd=0x%llx pid=%lu client=%ldx%ld minimized=%s title=",(unsigned long long)(ULONG_PTR)hwnd,pid,r.right-r.left,r.bottom-r.top,IsIconic(hwnd)?"true":"false");out(b);wide(title);out(" class=");wide(cls);out("\r\n");
}
static DWORD WINAPI watchdog(void *unused)
{
    if(WaitForSingleObject(done,15000)==WAIT_TIMEOUT){out("watchdog_timeout=true\r\n");TerminateProcess(GetCurrentProcess(),124);}return 0;
}
static BOOL save_bmp(HWND hwnd,const WCHAR *method,int width,int height,BYTE *pixels)
{
    WCHAR path[2048],*slash;BITMAPFILEHEADER fh;BITMAPINFOHEADER ih;DWORD size=(DWORD)width*height*4,wrote;HANDLE file;BOOL ok;
    DWORD n=GetModuleFileNameW(NULL,path,ARRAYSIZE(path));if(!n||n>=ARRAYSIZE(path))return FALSE;
    slash=wcsrchr(path,'\\');if(!slash)return FALSE;slash[1]=0;
    n=wcslen(path);if(_snwprintf(path+n,ARRAYSIZE(path)-n,L"game-window-%llx-%s.bmp",(unsigned long long)(ULONG_PTR)hwnd,method)<0)return FALSE;
    memset(&fh,0,sizeof(fh));memset(&ih,0,sizeof(ih));fh.bfType=0x4d42;fh.bfOffBits=sizeof(fh)+sizeof(ih);fh.bfSize=fh.bfOffBits+size;
    ih.biSize=sizeof(ih);ih.biWidth=width;ih.biHeight=-height;ih.biPlanes=1;ih.biBitCount=32;ih.biCompression=BI_RGB;ih.biSizeImage=size;
    file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);if(file==INVALID_HANDLE_VALUE)return FALSE;
    ok=WriteFile(file,&fh,sizeof(fh),&wrote,NULL)&&wrote==sizeof(fh);
    if(ok)ok=WriteFile(file,&ih,sizeof(ih),&wrote,NULL)&&wrote==sizeof(ih);
    if(ok)ok=WriteFile(file,pixels,size,&wrote,NULL)&&wrote==size;
    CloseHandle(file);
    if(ok){out("capture=");wide(path);out("\r\n");}
    return ok;
}
static void capture(HWND hwnd)
{
    RECT r;int width,height;HDC dc,mem;HBITMAP bmp;HGDIOBJ old;BITMAPINFO info;BYTE *pixels=NULL;BOOL success;
    if(!GetClientRect(hwnd,&r))return;width=r.right-r.left;height=r.bottom-r.top;
    if(width<1||height<1||width>8192||height>8192){out("capture_dimensions_unsupported=true\r\n");return;}
    dc=GetDC(hwnd);if(!dc){out("client_dc_unavailable=true\r\n");return;}
    mem=pCreateCompatibleDC(dc);if(!mem){ReleaseDC(hwnd,dc);return;}
    memset(&info,0,sizeof(info));info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    bmp=pCreateDIBSection(dc,&info,DIB_RGB_COLORS,(void **)&pixels,NULL,0);
    if(!bmp){pDeleteDC(mem);ReleaseDC(hwnd,dc);return;}
    old=pSelectObject(mem,bmp);
    memset(pixels,0,(SIZE_T)width*height*4);
    success=pBitBlt(mem,0,0,width,height,dc,0,0,SRCCOPY);
    pGdiFlush();out(success?"client_bitblt_succeeded=true\r\n":"client_bitblt_succeeded=false\r\n");
    if(success)save_bmp(hwnd,L"bitblt",width,height,pixels);
    memset(pixels,0,(SIZE_T)width*height*4);
    success=PrintWindow(hwnd,mem,PW_CLIENTONLY|2);
    pGdiFlush();out(success?"client_printwindow_succeeded=true\r\n":"client_printwindow_succeeded=false\r\n");
    if(success)save_bmp(hwnd,L"printwindow",width,height,pixels);
    pSelectObject(mem,old);pDeleteObject(bmp);pDeleteDC(mem);ReleaseDC(hwnd,dc);
}
static BOOL CALLBACK enumerate(HWND hwnd,LPARAM unused)
{
    DWORD pid;if(!target_matches(hwnd,&pid))return TRUE;matched++;identify(hwnd,pid);if(!list_only)capture(hwnd);return TRUE;
}
void mainCRTStartup(void)
{
    HMODULE gdi;WCHAR *command=GetCommandLineW(), *arg;
    BOOL minimize=wcsstr(command,L"--minimize")!=NULL;
    BOOL restore=wcsstr(command,L"--restore")!=NULL;
    BOOL close_window=wcsstr(command,L"--close")!=NULL;
    list_only=wcsstr(command,L"--list")!=NULL;
    arg=wcsstr(command,minimize?L"--minimize":restore?L"--restore":close_window?L"--close":L"--enter");
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    done=CreateEventW(NULL,TRUE,FALSE,NULL);if(!done||!CreateThread(NULL,0,watchdog,NULL,0,NULL))ExitProcess(1);
    if(arg) {
        WCHAR *end;ULONG_PTR id;DWORD requested,actual;HWND hwnd;BOOL down,up;
        arg+=minimize?10:restore?9:7;id=(ULONG_PTR)_wcstoui64(arg,&end,16);if(end==arg)ExitProcess(2);arg=end;requested=wcstoul(arg,&end,10);hwnd=(HWND)id;
        if(end==arg||!requested||!target_matches(hwnd,&actual)||actual!=requested){out("input_target_rejected=true\r\n");ExitProcess(2);}
        identify(hwnd,actual);
        if(minimize||close_window||restore) {
            BOOL posted=restore?ShowWindowAsync(hwnd,SW_SHOWNOACTIVATE):PostMessageW(hwnd,close_window?WM_CLOSE:WM_SYSCOMMAND,close_window?0:SC_MINIMIZE,0);
            out(posted?"window_action_posted=true\r\n":"window_action_posted=false\r\n");ExitProcess(posted?0:1);
        }
        down=PostMessageW(hwnd,WM_KEYDOWN,VK_RETURN,1|(0x1c<<16));
        up=down&&PostMessageW(hwnd,WM_KEYUP,VK_RETURN,1|(0x1c<<16)|(1u<<30)|(1u<<31));
        out(down&&up?"enter_posted=true\r\n":"enter_posted=false\r\n");ExitProcess(down&&up?0:1);
    }
    if(list_only){EnumWindows(enumerate,0);ExitProcess(matched?0:3);}
    gdi=LoadLibraryW(L"gdi32.dll");if(!gdi)ExitProcess(1);
#define RESOLVE(name) p##name=(void *)GetProcAddress(gdi,#name);if(!p##name)ExitProcess(1)
    RESOLVE(CreateCompatibleDC);RESOLVE(CreateDIBSection);RESOLVE(SelectObject);RESOLVE(DeleteObject);RESOLVE(DeleteDC);RESOLVE(BitBlt);RESOLVE(GdiFlush);
#undef RESOLVE
    EnumWindows(enumerate,0);out(matched?"cricket_window_found=true\r\n":"cricket_window_found=false\r\n");ExitProcess(matched?0:3);
}
