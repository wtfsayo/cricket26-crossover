/* Real runtime integration probe. Uses only a disposable, non-game SCID.
 * Output deliberately excludes account identifiers, credentials, paths and save data.
 */
#define COBJMACROS
#define INITGUID
#include <initguid.h>
#include <string.h>
#include "winegdk-source/dlls/xgameruntime/GDKComponent/System/XGameSave.h"

typedef HRESULT (WINAPI *InitializeFn)(ULONG, ULONG);
typedef HRESULT (WINAPI *QueryFn)(const GUID *, REFIID, void **);
static const char probe_scid[] = "d714980e-27c4-4a71-a978-18a0d8f93ce1";
static const WCHAR probe_scid_w[] = L"d714980e-27c4-4a71-a978-18a0d8f93ce1";
static const BYTE original[] = {0x00,0x51,0xff,0x09,0x72,0x00,0x31,0x80};
static const BYTE replacement[] = {0xa3,0x00,0x18,0xef,0x62};
static IXThreadingImpl *threading;
static IXGameSaveImpl *saves;
static XAsyncBlock auth_block, save_block;
static LONG callbacks;
static HRESULT completion_hr;
static XGameSaveProviderHandle completion_provider;
static BOOL valid = TRUE;
static HANDLE finished;

static void out(const char *s)
{
    DWORD n = 0, written;
    while (s[n]) ++n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &written, NULL);
}
static void result(const char *label, HRESULT hr)
{
    char s[] = "0x00000000\r\n";
    unsigned int n = (unsigned int)hr;
    for (int i = 0; i < 8; ++i) s[9-i] = "0123456789ABCDEF"[(n >> (i*4)) & 15];
    out(label); out("="); out(s);
}
static void check(const char *label, BOOL value)
{
    out(label); out(value ? "=true\r\n" : "=false\r\n");
    if (!value) valid = FALSE;
}
static DWORD WINAPI watchdog(void *unused)
{
    if (WaitForSingleObject(finished, 150000) == WAIT_TIMEOUT)
    {
        result("watchdog", HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        TerminateProcess(GetCurrentProcess(), 124);
    }
    return 0;
}
static void WINAPI completed(XAsyncBlock *async)
{
    completion_hr = saves->lpVtbl->XGameSaveInitializeProviderResult(saves, async, &completion_provider);
    InterlockedIncrement(&callbacks);
}
static HRESULT initialize_save(XUserHandle user, BOOLEAN sync, XGameSaveProviderHandle *provider)
{
    XTaskQueueHandle queue = save_block.queue;
    XGameSaveProviderHandle pending = (XGameSaveProviderHandle)1;
    HRESULT hr;
    ULONGLONG deadline;
    memset(&save_block, 0, sizeof(save_block));
    save_block.queue = queue;
    save_block.callback = completed;
    callbacks = 0;
    completion_hr = E_PENDING;
    completion_provider = (XGameSaveProviderHandle)1;
    *provider = NULL;
    hr = saves->lpVtbl->XGameSaveInitializeProviderAsync(saves, user, probe_scid, sync, &save_block);
    result("SaveInitializeAccepted", hr);
    if (FAILED(hr)) return hr;
    check("pending_before_work", IXThreadingImpl_XAsyncGetStatus(threading, &save_block, FALSE) == E_PENDING);
    hr = saves->lpVtbl->XGameSaveInitializeProviderResult(saves, &save_block, &pending);
    check("pending_result_has_no_provider", hr == E_PENDING && pending == NULL);
    check("no_inline_callback", callbacks == 0);
    deadline = GetTickCount64() + 10000;
    do {
        IXThreadingImpl_XTaskQueueDispatch(threading, queue, XTaskQueuePort_Work, 10);
        hr = IXThreadingImpl_XAsyncGetStatus(threading, &save_block, FALSE);
    } while (hr == E_PENDING && GetTickCount64() < deadline);
    check("completion_requires_own_port", callbacks == 0);
    while (!callbacks && GetTickCount64() < deadline)
        IXThreadingImpl_XTaskQueueDispatch(threading, queue, XTaskQueuePort_Completion, 10);
    if (!callbacks)
    {
        result("SaveCompletionTimeout", HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        /* A still-active block and its context must survive until process teardown. */
        ExitProcess(124);
    }
    for (int i = 0; i < 4; ++i)
    {
        IXThreadingImpl_XTaskQueueDispatch(threading, queue, XTaskQueuePort_Work, 0);
        IXThreadingImpl_XTaskQueueDispatch(threading, queue, XTaskQueuePort_Completion, 0);
    }
    check("callback_exactly_once", callbacks == 1);
    result("SaveCompletion", completion_hr);
    *provider = completion_provider;
    return completion_hr;
}

static BOOL read_equals(XGameSaveContainerHandle container, const char *name, const BYTE *bytes, SIZE_T size)
{
    union { UINT64 alignment; BYTE bytes[2048]; } buffer;
    XGameSaveBlob *blob = (XGameSaveBlob *)buffer.bytes;
    const char *names[] = {name};
    UINT32 count = 1;
    HRESULT hr;
    memset(&buffer, 0, sizeof(buffer));
    hr = saves->lpVtbl->XGameSaveReadBlobData(saves, container, names, &count, sizeof(buffer), blob);
    result("ReadBlob", hr);
    if (FAILED(hr) || count != 1 || blob->info.size != size || !blob->info.name || !blob->data) return FALSE;
    /* Check returned pointers before touching data; the public API stores results in this buffer. */
    if ((ULONG_PTR)blob->data < (ULONG_PTR)buffer.bytes ||
        (ULONG_PTR)blob->data > (ULONG_PTR)buffer.bytes + sizeof(buffer) - size) return FALSE;
    if ((ULONG_PTR)blob->info.name < (ULONG_PTR)buffer.bytes ||
        (ULONG_PTR)blob->info.name > (ULONG_PTR)buffer.bytes + sizeof(buffer) - strlen(name) - 1) return FALSE;
    return !memcmp(blob->data, bytes, size) && !memcmp(blob->info.name, name, strlen(name) + 1);
}
static BOOL missing_blob(XGameSaveContainerHandle container, const char *name)
{
    union { UINT64 alignment; BYTE bytes[2048]; } buffer;
    const char *names[] = {name};
    UINT32 count = 1;
    HRESULT hr = saves->lpVtbl->XGameSaveReadBlobData(saves, container, names, &count, sizeof(buffer), (XGameSaveBlob *)buffer.bytes);
    result("MissingBlob", hr);
    return FAILED(hr);
}
static void hex16(char *out_hex, UINT64 value)
{
    for (int i = 0; i < 16; ++i) out_hex[15-i] = "0123456789abcdef"[(value >> (4*i)) & 15];
    out_hex[16] = 0;
}

static BOOL verify_local_user_lookup(IXUserImpl *users, XUserHandle *user, UINT64 expected_id)
{
    XUserLocalId local_id = {0}, unknown_id = {~(UINT64)0};
    XUserHandle found = NULL, unknown = (XUserHandle)1, retained = NULL;
    UINT64 found_id = 0;
    HRESULT hr = IXUserImpl_XUserGetLocalId(users, *user, &local_id);
    check("local_user_id_available", SUCCEEDED(hr) && local_id.value != 0);
    if (FAILED(hr) || !local_id.value) return FALSE;
    hr = IXUserImpl_XUserFindUserByLocalId(users, local_id, &found);
    result("FindUserByLocalId", hr);
    check("local_user_lookup_found_handle", SUCCEEDED(hr) && found != NULL);
    if (FAILED(hr) || !found) return FALSE;
    hr = IXUserImpl_XUserGetId(users, found, &found_id);
    check("local_user_lookup_identity_matches", SUCCEEDED(hr) && found_id == expected_id);
    /* The lookup must own a reference independently of the original auth handle. */
    IXUserImpl_XUserCloseHandle(users, *user);
    *user = found;
    found_id = 0;
    hr = IXUserImpl_XUserGetId(users, *user, &found_id);
    check("lookup_handle_survives_original_close", SUCCEEDED(hr) && found_id == expected_id);
    hr = IXUserImpl_XUserFindUserByLocalId(users, local_id, &retained);
    check("retained_local_user_still_findable", SUCCEEDED(hr) && retained != NULL);
    if (SUCCEEDED(hr) && retained) IXUserImpl_XUserCloseHandle(users, retained);
    found_id = 0;
    hr = IXUserImpl_XUserGetId(users, *user, &found_id);
    check("closing_extra_lookup_preserves_user", SUCCEEDED(hr) && found_id == expected_id);
    hr = IXUserImpl_XUserFindUserByLocalId(users, unknown_id, &unknown);
    result("FindUnknownLocalUser", hr);
    check("unknown_local_user_rejected_and_null", hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && unknown == NULL);
    if (SUCCEEDED(hr) && unknown && unknown != (XUserHandle)1) IXUserImpl_XUserCloseHandle(users, unknown);
    SecureZeroMemory(&found_id, sizeof(found_id));
    SecureZeroMemory(&local_id, sizeof(local_id));
    return valid;
}

/* Accept only our fixed disposable container format, never a path or identity. */
static BOOL read_mode(char container[64], BOOL *read_only)
{
    const WCHAR *arg = GetCommandLineW();
    const WCHAR prefix[] = L"--read-persisted local-probe-";
    *read_only = FALSE;
    if (*arg == L'"') { ++arg; while (*arg && *arg != L'"') ++arg; if (*arg) ++arg; }
    else while (*arg && *arg != L' ' && *arg != L'\t') ++arg;
    while (*arg == L' ' || *arg == L'\t') ++arg;
    if (!*arg) return TRUE;
    for (unsigned int i = 0; i < ARRAY_SIZE(prefix)-1; ++i)
        if (*arg++ != prefix[i]) return FALSE;
    for (unsigned int i = 0; i < 16; ++i)
    {
        WCHAR c = *arg++;
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return FALSE;
        container[12+i] = c;
    }
    container[28] = 0;
    if (*arg) return FALSE;
    *read_only = TRUE;
    return TRUE;
}

static BOOL verify_fresh_process(const char *container)
{
    WCHAR executable[2048], command[2200];
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process = {0};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    HANDLE job;
    DWORD length, wait, exit_code = 1;
    BOOL success = FALSE;
    length = GetModuleFileNameW(NULL, executable, ARRAY_SIZE(executable));
    if (!length || length >= ARRAY_SIZE(executable)) return FALSE;
    lstrcpyW(command, L"\""); lstrcatW(command, executable);
    lstrcatW(command, L"\" --read-persisted ");
    length = lstrlenW(command);
    for (unsigned int i = 0; i <= 28; ++i) command[length+i] = container[i];
    job = CreateJobObjectW(NULL, NULL);
    if (!job) return FALSE;
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) goto done;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (!CreateProcessW(executable, command, NULL, NULL, TRUE, CREATE_SUSPENDED,
                        NULL, NULL, &startup, &process)) goto done;
    if (!AssignProcessToJobObject(job, process.hProcess) || ResumeThread(process.hThread) == (DWORD)-1)
    {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
        goto done;
    }
    wait = WaitForSingleObject(process.hProcess, 65000);
    if (wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exit_code))
        success = exit_code == 0;
    else
    {
        TerminateProcess(process.hProcess, 124);
        WaitForSingleObject(process.hProcess, 5000);
    }
    result("FreshProcessExit", (HRESULT)exit_code);
done:
    if (!success) result("FreshProcessVerification", E_FAIL);
    if (process.hThread) CloseHandle(process.hThread);
    if (process.hProcess) CloseHandle(process.hProcess);
    CloseHandle(job);
    return success;
}

void mainCRTStartup(void)
{
    HMODULE native, runtime;
    InitializeFn init;
    QueryFn query;
    IXUserImpl *users = NULL;
    XUserHandle user = NULL;
    XGameSaveProviderHandle provider = NULL, other = NULL;
    XGameSaveContainerHandle container = NULL, bad_container = NULL, stale_container;
    XGameSaveUpdateHandle update = NULL, stale_update;
    HRESULT hr;
    UINT64 xuid = 0;
    INT64 quota = -1;
    ULONGLONG deadline;
    HANDLE lock = INVALID_HANDLE_VALUE;
    WCHAR lock_path[1024];
    char xuid_hex[17], container_name[64] = "local-probe-", suffix[17];
    DWORD len;
    BOOL container_created = FALSE, read_only = FALSE;

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (!read_mode(container_name, &read_only)) { check("valid_probe_arguments", FALSE); ExitProcess(1); }
    finished = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!finished || !CreateThread(NULL, 0, watchdog, NULL, 0, NULL)) ExitProcess(1);
    native = LoadLibraryW(L"xgameruntime.dll.threading");
    runtime = LoadLibraryW(L"xgameruntime.dll");
    if (!native || !runtime) { result("LoadRuntime", HRESULT_FROM_WIN32(GetLastError())); ExitProcess(1); }
    init = (InitializeFn)GetProcAddress(runtime, "InitializeApiImpl");
    query = (QueryFn)GetProcAddress(runtime, "QueryApiImpl");
    if (!init || !query) ExitProcess(1);
    hr = init(10001, 3181); result("InitializeApiImpl", hr);
    if (FAILED(hr)) ExitProcess(1);
    hr = query(&CLSID_XThreadingImpl, &IID_IXThreadingImpl, (void **)&threading);
    result("QueryThreading", hr); if (FAILED(hr) || !threading) ExitProcess(1);
    hr = query(&CLSID_XUserImpl, &IID_IXUserImpl, (void **)&users);
    result("QueryUser", hr); if (FAILED(hr) || !users) ExitProcess(1);
    hr = query(&CLSID_XGameSaveImpl, &IID_IXGameSaveImpl, (void **)&saves);
    result("QueryGameSave", hr); if (FAILED(hr) || !saves) ExitProcess(1);
    hr = IXThreadingImpl_XTaskQueueCreate(threading, XTaskQueueDispatchMode_ThreadPool, XTaskQueueDispatchMode_Manual, &auth_block.queue);
    result("AuthQueue", hr); if (FAILED(hr)) ExitProcess(1);
    hr = IXUserImpl_XUserAddAsync(users, XUserAddOptions_AddDefaultUserSilently, &auth_block);
    result("SilentAuthentication", hr); if (FAILED(hr)) ExitProcess(1);
    deadline = GetTickCount64() + 45000;
    while ((hr = IXThreadingImpl_XAsyncGetStatus(threading, &auth_block, FALSE)) == E_PENDING && GetTickCount64() < deadline) Sleep(25);
    if (hr == E_PENDING) ExitProcess(124);
    hr = IXUserImpl_XUserAddResult(users, &auth_block, &user);
    result("UserResult", hr); if (FAILED(hr) || !user) ExitProcess(1);
    hr = IXUserImpl_XUserGetId(users, user, &xuid);
    check("authenticated_nonzero_identity", SUCCEEDED(hr) && xuid != 0);
    if (FAILED(hr) || !xuid) goto done;
    if (!verify_local_user_lookup(users, &user, xuid)) goto done;
    hr = IXThreadingImpl_XTaskQueueCreate(threading, XTaskQueueDispatchMode_Manual, XTaskQueueDispatchMode_Manual, &save_block.queue);
    result("SaveManualQueue", hr); if (FAILED(hr)) { valid = FALSE; goto done; }
    if (read_only)
    {
        SetEnvironmentVariableW(L"WINEGDK_LOCAL_GAMESAVE", L"1");
        hr = initialize_save(user, FALSE, &provider);
        check("fresh_process_provider_created", SUCCEEDED(hr) && provider != NULL);
        if (FAILED(hr) || !provider) goto done;
        hr = saves->lpVtbl->XGameSaveCreateContainer(saves, provider, container_name, &container);
        check("fresh_process_container_opened", SUCCEEDED(hr) && container != NULL);
        if (FAILED(hr) || !container) goto done;
        check("fresh_process_persisted_bytes", read_equals(container, "state", original, sizeof(original)));
        check("fresh_process_no_partial_blob", missing_blob(container, "uncommitted"));
        goto done;
    }
    hex16(suffix, ((UINT64)GetCurrentProcessId() << 32) ^ GetTickCount64());
    strcat(container_name, suffix);

    SetEnvironmentVariableW(L"WINEGDK_LOCAL_GAMESAVE", NULL);
    hr = initialize_save(user, FALSE, &other);
    check("opt_out_rejected", hr == E_NOTIMPL && other == NULL);
    if (other) { saves->lpVtbl->XGameSaveCloseProvider(saves, other); other = NULL; }
    SetEnvironmentVariableW(L"WINEGDK_LOCAL_GAMESAVE", L"1");
    hr = initialize_save(user, TRUE, &other);
    check("on_demand_cloud_mode_rejected", hr == E_NOTIMPL && other == NULL);
    if (other) { saves->lpVtbl->XGameSaveCloseProvider(saves, other); other = NULL; }
    hr = initialize_save(user, FALSE, &provider);
    check("local_provider_created", SUCCEEDED(hr) && provider != NULL);
    if (FAILED(hr) || !provider) goto done;
    hr = saves->lpVtbl->XGameSaveGetRemainingQuota(saves, provider, &quota);
    check("remaining_quota_available", SUCCEEDED(hr) && quota > 0);
    hr = saves->lpVtbl->XGameSaveGetRemainingQuota(saves, (XGameSaveProviderHandle)1, &quota);
    check("invalid_provider_rejected", hr == E_HANDLE);
    hr = saves->lpVtbl->XGameSaveCreateContainer(saves, provider, "../probe-escape", &bad_container);
    check("container_traversal_rejected", FAILED(hr) && bad_container == NULL);
    if (bad_container) { saves->lpVtbl->XGameSaveCloseContainer(saves, bad_container); bad_container = NULL; }
    hr = saves->lpVtbl->XGameSaveCreateContainer(saves, provider, container_name, &container);
    result("CreateDisposableContainer", hr);
    if (FAILED(hr) || !container) { valid = FALSE; goto done; }
    container_created = TRUE;
    hr = saves->lpVtbl->XGameSaveCreateUpdate(saves, container, "Disposable integration probe", &update);
    result("CreateUpdate", hr); if (FAILED(hr) || !update) { valid = FALSE; goto done; }
    hr = saves->lpVtbl->XGameSaveSubmitBlobWrite(saves, update, "../probe-escape", original, sizeof(original));
    check("blob_traversal_rejected", FAILED(hr));
    hr = saves->lpVtbl->XGameSaveSubmitBlobWrite(saves, update, "state", original, sizeof(original));
    result("StageOriginal", hr); if (FAILED(hr)) { valid = FALSE; goto done; }
    hr = saves->lpVtbl->XGameSaveSubmitUpdate(saves, update);
    result("CommitOriginal", hr); if (FAILED(hr)) { valid = FALSE; goto done; }
    hr = saves->lpVtbl->XGameSaveSubmitUpdate(saves, update);
    check("consumed_update_rejected", hr == E_INVALIDARG);
    stale_update = update;
    saves->lpVtbl->XGameSaveCloseUpdate(saves, update); update = NULL;
    hr = saves->lpVtbl->XGameSaveSubmitUpdate(saves, stale_update);
    check("closed_update_rejected", hr == E_HANDLE);
    check("binary_blob_round_trip", read_equals(container, "state", original, sizeof(original)));

    hr = saves->lpVtbl->XGameSaveCreateUpdate(saves, container, "Must not commit", &update);
    result("CreateAtomicFailureUpdate", hr); if (FAILED(hr) || !update) { valid = FALSE; goto done; }
    hr = saves->lpVtbl->XGameSaveSubmitBlobWrite(saves, update, "state", replacement, sizeof(replacement));
    if (FAILED(hr)) { valid = FALSE; goto done; }
    hr = saves->lpVtbl->XGameSaveSubmitBlobWrite(saves, update, "uncommitted", replacement, sizeof(replacement));
    if (FAILED(hr)) { valid = FALSE; goto done; }
    len = GetEnvironmentVariableW(L"LOCALAPPDATA", lock_path, 800);
    if (!len || len >= 800) { check("lock_path_available", FALSE); goto done; }
    lstrcatW(lock_path, L"\\WineGDK\\LocalGameSave\\v1\\");
    hex16(xuid_hex, xuid);
    len = lstrlenW(lock_path);
    for (int i = 0; i < 16; ++i) lock_path[len+i] = xuid_hex[i];
    lock_path[len+16] = 0;
    lstrcatW(lock_path, L"\\"); lstrcatW(lock_path, probe_scid_w); lstrcatW(lock_path, L"\\.lock");
    lock = CreateFileW(lock_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    check("external_storage_lock_acquired", lock != INVALID_HANDLE_VALUE);
    if (lock == INVALID_HANDLE_VALUE) goto done;
    hr = saves->lpVtbl->XGameSaveSubmitUpdate(saves, update);
    result("CommitUnderExclusiveLock", hr);
    check("storage_failure_reported", hr == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
    CloseHandle(lock); lock = INVALID_HANDLE_VALUE;
    saves->lpVtbl->XGameSaveCloseUpdate(saves, update); update = NULL;
    check("failed_commit_retained_original", read_equals(container, "state", original, sizeof(original)));
    check("failed_commit_did_not_add_blob", missing_blob(container, "uncommitted"));
    stale_container = container;
    saves->lpVtbl->XGameSaveCloseContainer(saves, container); container = NULL;
    {
        const char *names[] = {"state"};
        UINT32 count = 1;
        BYTE bytes[128];
        hr = saves->lpVtbl->XGameSaveReadBlobData(saves, stale_container, names, &count, sizeof(bytes), (XGameSaveBlob *)bytes);
        check("closed_container_rejected", hr == E_HANDLE);
    }
    saves->lpVtbl->XGameSaveCloseProvider(saves, provider); provider = NULL;
    hr = initialize_save(user, FALSE, &provider);
    check("provider_reopened", SUCCEEDED(hr) && provider != NULL);
    if (FAILED(hr) || !provider) goto done;
    hr = saves->lpVtbl->XGameSaveCreateContainer(saves, provider, container_name, &container);
    if (FAILED(hr) || !container) { valid = FALSE; goto done; }
    check("reopened_provider_retained_bytes", read_equals(container, "state", original, sizeof(original)));
    check("reopened_provider_has_no_partial_blob", missing_blob(container, "uncommitted"));
    saves->lpVtbl->XGameSaveCloseContainer(saves, container); container = NULL;
    saves->lpVtbl->XGameSaveCloseProvider(saves, provider); provider = NULL;
    check("fresh_process_persistence_verified", verify_fresh_process(container_name));
    hr = initialize_save(user, FALSE, &provider);
    check("cleanup_provider_created", SUCCEEDED(hr) && provider != NULL);
done:
    if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
    if (update) saves->lpVtbl->XGameSaveCloseUpdate(saves, update);
    if (container) saves->lpVtbl->XGameSaveCloseContainer(saves, container);
    if (provider && container_created)
    {
        hr = saves->lpVtbl->XGameSaveDeleteContainer(saves, provider, container_name);
        check("disposable_container_removed", SUCCEEDED(hr));
    }
    if (provider) saves->lpVtbl->XGameSaveCloseProvider(saves, provider);
    if (user)
    {
        XUserLocalId stale_id = {0};
        XUserHandle stale_result = (XUserHandle)1;
        hr = IXUserImpl_XUserGetLocalId(users, user, &stale_id);
        check("closing_user_has_local_id", SUCCEEDED(hr) && stale_id.value != 0);
        IXUserImpl_XUserCloseHandle(users, user); user = NULL;
        hr = IXUserImpl_XUserFindUserByLocalId(users, stale_id, &stale_result);
        result("FindAfterLastUserClose", hr);
        check("last_user_close_expires_local_id", hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && stale_result == NULL);
        if (SUCCEEDED(hr) && stale_result && stale_result != (XUserHandle)1) IXUserImpl_XUserCloseHandle(users, stale_result);
        SecureZeroMemory(&stale_id, sizeof(stale_id));
    }
    if (save_block.queue) IXThreadingImpl_XTaskQueueCloseHandle(threading, save_block.queue);
    if (auth_block.queue) IXThreadingImpl_XTaskQueueCloseHandle(threading, auth_block.queue);
    if (saves) saves->lpVtbl->Release(saves);
    if (users) IXUserImpl_Release(users);
    if (threading) IXThreadingImpl_Release(threading);
    SecureZeroMemory(&xuid, sizeof(xuid));
    SecureZeroMemory(xuid_hex, sizeof(xuid_hex));
    SecureZeroMemory(lock_path, sizeof(lock_path));
    check(read_only ? "fresh_process_read_contract_passed" : "local_save_contract_passed", valid);
    ExitProcess(valid ? 0 : 1);
}
