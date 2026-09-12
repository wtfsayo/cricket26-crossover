#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <initguid.h>
#include <xasyncprovider.h>
#include <xuser.h>

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
void mainCRTStartup(void) {
    HRESULT hr;
    HMODULE native, runtime;
    InitializeFn init;
    QueryFn query;
    IXThreadingImpl *threading = NULL;
    IXUserImpl *users = NULL;
    IXUserGamertagImpl *profile = NULL;
    XUserHandle user = NULL;
    UINT64 xuid = 0;
    static char gamertag[256];
    SIZE_T used = 0;
    HANDLE guard, queueEvent;
    DWORD exitCode = 1;
    ULONGLONG deadline;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    finished = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!finished) { result("CreateEvent", HRESULT_FROM_WIN32(GetLastError())); ExitProcess(1); }
    guard = CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    if (!guard) { result("CreateThread", HRESULT_FROM_WIN32(GetLastError())); ExitProcess(1); }
    native = LoadLibraryW(L"xgameruntime.dll.threading");
    hr = native ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    result("LoadNativeThreading", hr);
    if (FAILED(hr)) goto done;
    runtime = LoadLibraryW(L"xgameruntime.dll");
    hr = runtime ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    result("LoadRuntime", hr);
    if (FAILED(hr)) goto done;
    init = (InitializeFn)GetProcAddress(runtime, "InitializeApiImpl");
    query = (QueryFn)GetProcAddress(runtime, "QueryApiImpl");
    hr = init && query ? S_OK : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    result("ResolveRuntimeApi", hr);
    if (FAILED(hr)) goto done;
    hr = init(10001, 3181); result("InitializeApiImpl", hr);
    if (FAILED(hr)) goto done;
    hr = query(&CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&threading);
    result("QueryXThreading", hr);
    if (FAILED(hr)) goto done;
    hr = query(&CLSID_XUserImpl, &IID_IXUserImpl, (void **)&users);
    result("QueryXUser", hr);
    if (FAILED(hr)) goto done;
    hr = IXThreadingImpl_XTaskQueueCreate(threading, XTaskQueueDispatchMode_ThreadPool, XTaskQueueDispatchMode_ThreadPool, &block.queue);
    result("XTaskQueueCreate", hr);
    if (FAILED(hr)) goto done;
    queueEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!queueEvent) { result("QueueEvent", HRESULT_FROM_WIN32(GetLastError())); goto done; }
    hr = IXThreadingImpl_XTaskQueueSubmitCallback(threading, block.queue, XTaskQueuePort_Work, queueEvent, queue_callback);
    result("QueueSubmitCallback", hr);
    if (FAILED(hr)) goto done;
    {
        DWORD waited = WaitForSingleObject(queueEvent, 5000);
        hr = waited == WAIT_OBJECT_0 ? S_OK : HRESULT_FROM_WIN32(waited == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
    }
    result("QueueCallbackCompleted", hr);
    if (FAILED(hr)) goto done;
    CloseHandle(queueEvent);
    hr = IXUserImpl_XUserAddAsync(users, XUserAddOptions_AddDefaultUserSilently, &block);
    result("XUserAddAsyncSilent", hr);
    if (FAILED(hr)) goto done;
    deadline = GetTickCount64() + 45000;
    while ((hr = IXThreadingImpl_XAsyncGetStatus(threading, &block, FALSE)) == E_PENDING && GetTickCount64() < deadline) Sleep(25);
    if (hr == E_PENDING) {
        result("XAsyncGetStatus", HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        IXThreadingImpl_XAsyncCancel(threading, &block);
        /* Keep the async block and its queue alive until process teardown. */
        exitCode = 124;
        goto done;
    }
    result("XAsyncGetStatus", hr);
    hr = IXUserImpl_XUserAddResult(users, &block, &user);
    result("XUserAddResult", hr);
    if (FAILED(hr) || !user) goto done;
    hr = IXUserImpl_XUserGetId(users, user, &xuid);
    result("XUserGetId", hr);
    flag("nonzero_xuid", SUCCEEDED(hr) && xuid != 0);
    hr = IXUserImpl_QueryInterface(users, &IID_IXUserGamertagImpl, (void **)&profile);
    result("QueryGamertag", hr);
    if (SUCCEEDED(hr)) {
        hr = IXUserGamertagImpl_XUserGetGamertag(profile, user, XUserGamertagComponent_Classic, sizeof(gamertag), gamertag, &used);
        result("XUserGetGamertag", hr);
        flag("nonempty_profile", SUCCEEDED(hr) && gamertag[0] != 0);
        if (SUCCEEDED(hr) && gamertag[0] && xuid) exitCode = 0;
        SecureZeroMemory(gamertag, sizeof(gamertag));
        IXUserGamertagImpl_Release(profile);
    }
    IXUserImpl_XUserCloseHandle(users, user);
    IXThreadingImpl_XTaskQueueCloseHandle(threading, block.queue);
    IXUserImpl_Release(users);
    IXThreadingImpl_Release(threading);
 done:
    /* Keep the watchdog armed through runtime DLL process-detach callbacks. */
    ExitProcess(exitCode);
}
