#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <initguid.h>
#include <xasyncprovider.h>
#include <xuser.h>
#include <string.h>
#include "winegdk-source/dlls/xgameruntime/GDKComponent/System/XStore.h"

typedef HRESULT (WINAPI *InitializeFn)(ULONG, ULONG);
typedef HRESULT (WINAPI *QueryFn)(const GUID *, REFIID, void **);
static HANDLE finished;
static void WINAPI queue_callback(void *context, BOOLEAN canceled) { if (!canceled) SetEvent((HANDLE)context); }
static XAsyncBlock block;
static void out(const char *s) {
    DWORD n = 0, written;
    while (s[n]) n++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &written, NULL);
}
static void result(const char *label, HRESULT hr) {
    char s[] = "0x00000000\r\n";
    unsigned int n = (unsigned int)hr;
    for (int i = 0; i < 8; i++) s[9-i] = "0123456789ABCDEF"[(n >> (i*4)) & 15];
    out(label); out("="); out(s);
}
static void flag(const char *label, BOOL value) { out(label); out(value ? "=true\r\n" : "=false\r\n"); }
static DWORD WINAPI watchdog(void *unused) {
    if (WaitForSingleObject(finished, 60000) == WAIT_TIMEOUT) {
        result("watchdog", HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        TerminateProcess(GetCurrentProcess(), 124);
    }
    return 0;
}

static IXStoreImpl *store;
static LONG callback_count;
static void WINAPI store_completed(XAsyncBlock *async) { InterlockedIncrement(&callback_count); }
void mainCRTStartup(void) {
 HMODULE runtime; InitializeFn init; QueryFn query; IXThreadingImpl *threading=NULL;
 XStoreContextHandle context=NULL, invalid=(void*)1; HRESULT hr; SIZE_T size=42; char *token=NULL;
 char product[]="9N6JF50HZFW9", challenge[128]="Cricket26-headless-runtime-copy-check-";
 const char *products[]={product}; char suffix[17]; BOOL valid=TRUE; ULONGLONG stamp=GetTickCount64(),deadline;
 for(int i=0;i<16;i++)suffix[15-i]="0123456789abcdef"[(stamp>>(i*4))&15];suffix[16]=0;strcat(challenge,suffix);
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 finished=CreateEventW(NULL,TRUE,FALSE,NULL);if(!finished||!CreateThread(NULL,0,watchdog,NULL,0,NULL))ExitProcess(1);
 if(!LoadLibraryW(L"xgameruntime.dll.threading"))ExitProcess(1);
 runtime=LoadLibraryW(L"xgameruntime.dll");if(!runtime)ExitProcess(1);
 init=(InitializeFn)GetProcAddress(runtime,"InitializeApiImpl");query=(QueryFn)GetProcAddress(runtime,"QueryApiImpl");if(!init||!query)ExitProcess(1);
 hr=init(10006,4429);result("Initialize",hr);if(FAILED(hr))ExitProcess(2);
 hr=query(&CLSID_XThreadingImpl,&IID_IXThreadingImpl,(void**)&threading);result("ThreadingFactory",hr);if(FAILED(hr))ExitProcess(2);
 hr=query(&CLSID_XStoreImpl,&IID_IXStoreImpl,(void**)&store);result("StoreFactory",hr);if(FAILED(hr))ExitProcess(2);
 hr=store->lpVtbl->XStoreCreateContext(store,NULL,&context);result("RealStoreContext",hr);if(FAILED(hr)||!context)ExitProcess(2);
 hr=IXThreadingImpl_XTaskQueueCreate(threading,XTaskQueueDispatchMode_Manual,XTaskQueueDispatchMode_Manual,&block.queue);if(FAILED(hr))ExitProcess(2);
 block.callback=store_completed;
 hr=store->lpVtbl->XStoreQueryLicenseTokenAsync(store,context,products,1,challenge,&block);result("LicenseAsyncAccepted",hr);if(FAILED(hr))ExitProcess(2);
 flag("no_inline_callback",callback_count==0);valid=valid&&callback_count==0;
 hr=store->lpVtbl->XStoreQueryLicenseTokenResultSize(store,&block,&size);result("PendingSize",hr);flag("pending_size_zero",hr==E_PENDING&&size==0);valid=valid&&hr==E_PENDING&&size==0;
 memset(product,'?',sizeof(product)-1);memset(challenge,'?',strlen(challenge));
 store->lpVtbl->XStoreCloseContextHandle(store,context);context=NULL;
 deadline=GetTickCount64()+40000;
 while((hr=IXThreadingImpl_XAsyncGetStatus(threading,&block,FALSE))==E_PENDING&&GetTickCount64()<deadline)
  IXThreadingImpl_XTaskQueueDispatch(threading,block.queue,XTaskQueuePort_Work,25);
 result("LicenseCompletionStatus",hr);flag("callback_waits_for_completion_port",callback_count==0);valid=valid&&callback_count==0;
 while(!callback_count&&GetTickCount64()<deadline)IXThreadingImpl_XTaskQueueDispatch(threading,block.queue,XTaskQueuePort_Completion,25);
 for(int i=0;i<4;i++)IXThreadingImpl_XTaskQueueDispatch(threading,block.queue,XTaskQueuePort_Completion,0);
 flag("callback_exactly_once",callback_count==1);valid=valid&&callback_count==1;
 size=42;hr=store->lpVtbl->XStoreQueryLicenseTokenResultSize(store,&block,&size);result("LicenseResultSize",hr);
 if(SUCCEEDED(hr)&&size>1&&size<=49153) {
  token=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,size);if(!token)ExitProcess(1);
  hr=store->lpVtbl->XStoreQueryLicenseTokenResult(store,&block,size,token);result("LicenseResult",hr);
  flag("real_token_returned",SUCCEEDED(hr)&&token[0]&&token[size-1]==0);valid=valid&&SUCCEEDED(hr)&&token[0]&&token[size-1]==0;
  SecureZeroMemory(token,size);HeapFree(GetProcessHeap(),0,token);
 } else {flag("failure_has_no_token_size",FAILED(hr)&&size==0);valid=FALSE;}
 IXThreadingImpl_XTaskQueueCloseHandle(threading,block.queue);
 store->lpVtbl->Release(store);IXThreadingImpl_Release(threading);SetEvent(finished);ExitProcess(valid?0:2);
}
