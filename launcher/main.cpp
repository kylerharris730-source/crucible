#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <wininet.h>
#include <wincrypt.h>
#include <stdio.h>
#include <wchar.h>
#include <string>
#include <vector>

#ifndef ALG_SID_SHA_256
#define ALG_SID_SHA_256 12
#endif
#ifndef CALG_SHA_256
#define CALG_SHA_256 (ALG_CLASS_HASH | ALG_TYPE_ANY | ALG_SID_SHA_256)
#endif

static const wchar_t* MANIFEST_URL =
    L"https://github.com/kylerharris730-source/crucible/releases/latest/download/cinderlift-manifest.txt";
static const wchar_t* RELEASE_PREFIX =
    L"https://github.com/kylerharris730-source/crucible/releases/download/";

static HWND g_title, g_status, g_play, g_check;
static HBRUSH g_bgBrush, g_buttonBrush, g_buttonPressedBrush;
static const COLORREF COL_BG = RGB(26, 28, 34);
static const COLORREF COL_BUTTON = RGB(42, 46, 56);
static const COLORREF COL_BUTTON_HOT = RGB(64, 70, 84);
static const COLORREF COL_BORDER = RGB(88, 94, 108);
static const COLORREF COL_ACCENT = RGB(226, 190, 90);
static const COLORREF COL_TEXT = RGB(214, 216, 224);
static const COLORREF COL_MUTED = RGB(160, 168, 182);
static volatile LONG g_busy;
static const UINT WM_STATUS = WM_APP + 1;
static const int ID_PLAY = 1001, ID_CHECK = 1002;

struct Manifest {
    std::wstring version, gameUrl, gameHash, launcherUrl, launcherHash;
};

static std::wstring exePath() {
    wchar_t path[32768];
    DWORD n = GetModuleFileNameW(0, path, 32768);
    return n ? std::wstring(path, n) : L"cinderlift-launcher.exe";
}

static std::wstring parentDir(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? L"." : path.substr(0, p);
}

static std::wstring joinPath(const std::wstring& a, const wchar_t* b) {
    if (a.empty()) return b;
    return a + (a[a.size() - 1] == L'\\' ? L"" : L"\\") + b;
}

static bool exists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void migrateAdjacentData(const std::wstring& destination) {
    const std::wstring source = parentDir(exePath());
    const wchar_t* fixed[] = { L"cinderlift.sav", L"cinderlift.cfg" };
    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i) {
        const std::wstring from = joinPath(source, fixed[i]);
        const std::wstring to = joinPath(destination, fixed[i]);
        if (exists(from) && !exists(to)) CopyFileW(from.c_str(), to.c_str(), TRUE);
    }
    for (int slot = 1; slot <= 10; ++slot) {
        wchar_t name[32]; _snwprintf(name, 31, L"cinderlift%d.sav", slot); name[31] = 0;
        const std::wstring from = joinPath(source, name), to = joinPath(destination, name);
        if (exists(from) && !exists(to)) CopyFileW(from.c_str(), to.c_str(), TRUE);
    }
}

static std::wstring appDir() {
    wchar_t path[32768];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, 32768);
    std::wstring dir = n && n < 32768 ? std::wstring(path, n) : parentDir(exePath());
    dir = joinPath(dir, L"Crucible");
    CreateDirectoryW(dir.c_str(), 0);
    return dir;
}

static void postStatus(const std::wstring& text, bool canPlay) {
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    wchar_t* copy = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!copy) return;
    memcpy(copy, text.c_str(), bytes);
    PostMessageW(GetParent(g_status), WM_STATUS, canPlay ? 1 : 0, (LPARAM)copy);
}

static std::wstring winError(const wchar_t* what) {
    wchar_t msg[256];
    _snwprintf(msg, 255, L"%ls (Windows error %lu)", what, GetLastError()); msg[255] = 0;
    return msg;
}

static bool download(const std::wstring& url, const std::wstring& output, std::wstring& error) {
    if (url.find(L"https://") != 0) {
        error = L"Release URL is not valid HTTPS"; return false;
    }
    HINTERNET session = InternetOpenW(L"Cinderlift Launcher/1.0", INTERNET_OPEN_TYPE_PRECONFIG, 0, 0, 0);
    if (!session) { error = winError(L"Could not start the network client"); return false; }
    DWORD timeout = 30000;
    InternetSetOptionW(session, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(session, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    HINTERNET request = InternetOpenUrlW(session, url.c_str(), L"Cache-Control: no-cache\r\n", (DWORD)-1,
        INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
        INTERNET_FLAG_KEEP_CONNECTION, 0);
    bool ok = request != 0;
    DWORD status = 0, statusSize = sizeof(status);
    if (ok) ok = HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                                &status, &statusSize, 0) && status == 200;
    HANDLE file = INVALID_HANDLE_VALUE;
    if (ok) file = CreateFileW(output.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) ok = false;
    std::vector<unsigned char> buffer(64 * 1024);
    while (ok) {
        DWORD got = 0;
        if (!InternetReadFile(request, &buffer[0], (DWORD)buffer.size(), &got)) { ok = false; break; }
        if (!got) break;
        DWORD wrote = 0;
        if (!WriteFile(file, &buffer[0], got, &wrote, 0) || wrote != got) { ok = false; break; }
    }
    if (file != INVALID_HANDLE_VALUE) { if (ok) FlushFileBuffers(file); CloseHandle(file); }
    if (request) InternetCloseHandle(request);
    InternetCloseHandle(session);
    if (!ok) {
        DeleteFileW(output.c_str());
        if (status && status != 200) {
            wchar_t msg[128]; _snwprintf(msg, 127, L"GitHub returned HTTP %lu", status); msg[127] = 0; error = msg;
        } else error = winError(L"Download failed");
    }
    return ok;
}

static bool sha256(const std::wstring& path, std::wstring& out) {
    HCRYPTPROV provider = 0; HCRYPTHASH hash = 0;
    bool ok = CryptAcquireContextW(&provider, 0, 0, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) != 0 &&
              CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != 0;
    std::vector<unsigned char> digest(32), data(64 * 1024);
    HANDLE f = ok ? CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0)
                  : INVALID_HANDLE_VALUE;
    if (f == INVALID_HANDLE_VALUE) ok = false;
    while (ok) {
        DWORD got = 0;
        if (!ReadFile(f, &data[0], (DWORD)data.size(), &got, 0)) { ok = false; break; }
        if (!got) break;
        if (!CryptHashData(hash, &data[0], got, 0)) ok = false;
    }
    DWORD hashBytes = (DWORD)digest.size();
    if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, &digest[0], &hashBytes, 0) != 0;
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    if (!ok) return false;
    static const wchar_t HEX[] = L"0123456789abcdef";
    out.clear(); out.reserve(hashBytes * 2);
    for (DWORD i = 0; i < hashBytes; ++i) { out += HEX[digest[i] >> 4]; out += HEX[digest[i] & 15]; }
    return true;
}

static std::wstring trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == 0xFEFF || s[a] == L' ' || s[a] == L'\r' || s[a] == L'\n' || s[a] == L'\t')) ++a;
    while (b > a && (s[b-1] == L' ' || s[b-1] == L'\r' || s[b-1] == L'\n' || s[b-1] == L'\t')) --b;
    return s.substr(a, b - a);
}

static bool validHash(const std::wstring& s) {
    if (s.size() != 64) return false;
    for (size_t i = 0; i < s.size(); ++i)
        if (!((s[i] >= L'0' && s[i] <= L'9') || (s[i] >= L'a' && s[i] <= L'f'))) return false;
    return true;
}

static bool parseManifest(const std::wstring& path, Manifest& m) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(f, 0), got = 0;
    if (size == INVALID_FILE_SIZE || size > 64 * 1024) { CloseHandle(f); return false; }
    std::vector<char> bytes(size + 1, 0);
    bool ok = ReadFile(f, &bytes[0], size, &got, 0) && got == size; CloseHandle(f);
    if (!ok) return false;
    int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, &bytes[0], size, 0, 0);
    if (chars <= 0) return false;
    std::vector<wchar_t> text(chars + 1, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, &bytes[0], size, &text[0], chars);
    std::wstring all(&text[0], chars);
    size_t at = 0;
    while (at <= all.size()) {
        size_t end = all.find(L'\n', at); if (end == std::wstring::npos) end = all.size();
        std::wstring line = trim(all.substr(at, end - at));
        size_t eq = line.find(L'=');
        if (eq != std::wstring::npos) {
            std::wstring key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
            if (key == L"version") m.version = value;
            else if (key == L"game_url") m.gameUrl = value;
            else if (key == L"game_sha256") m.gameHash = value;
            else if (key == L"launcher_url") m.launcherUrl = value;
            else if (key == L"launcher_sha256") m.launcherHash = value;
        }
        if (end == all.size()) break;
        at = end + 1;
    }
    return !m.version.empty() && validHash(m.gameHash) && validHash(m.launcherHash) &&
           m.gameUrl.find(RELEASE_PREFIX) == 0 && m.launcherUrl.find(RELEASE_PREFIX) == 0;
}

static bool writeText(const std::wstring& path, const std::wstring& text) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    bool ok = WriteFile(f, text.data(), (DWORD)(text.size() * sizeof(wchar_t)), &wrote, 0) != 0;
    CloseHandle(f); return ok;
}

static std::wstring readText(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (f == INVALID_HANDLE_VALUE) return L"";
    wchar_t value[80] = {}; DWORD got = 0; ReadFile(f, value, sizeof(value) - sizeof(wchar_t), &got, 0);
    CloseHandle(f); return trim(std::wstring(value, got / sizeof(wchar_t)));
}

static bool launch(const std::wstring& exe, const std::wstring& args, const std::wstring& cwd) {
    std::wstring command = L"\"" + exe + L"\"" + (args.empty() ? L"" : L" " + args);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end()); mutableCommand.push_back(0);
    STARTUPINFOW si; PROCESS_INFORMATION pi; ZeroMemory(&si, sizeof(si)); ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    bool ok = CreateProcessW(exe.c_str(), &mutableCommand[0], 0, 0, FALSE, 0, 0,
                             cwd.empty() ? 0 : cwd.c_str(), &si, &pi) != 0;
    if (ok) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    return ok;
}

static bool stageSelfUpdate(const Manifest& m, std::wstring& error) {
    std::wstring currentHash;
    if (!sha256(exePath(), currentHash)) {
        error = L"Could not verify the installed launcher"; return false;
    }
    if (currentHash == m.launcherHash) return true;
    const std::wstring dir = parentDir(exePath());
    const std::wstring temp = joinPath(dir, L"cinderlift-launcher.next.tmp");
    const std::wstring next = joinPath(dir, L"cinderlift-launcher.next.exe");
    if (!download(m.launcherUrl, temp, error)) return false;
    std::wstring downloaded;
    if (!sha256(temp, downloaded) || downloaded != m.launcherHash) {
        DeleteFileW(temp.c_str()); error = L"Launcher checksum did not match the release manifest"; return false;
    }
    if (!MoveFileExW(temp.c_str(), next.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str()); error = winError(L"Could not stage launcher update"); return false;
    }
    writeText(joinPath(dir, L"cinderlift-launcher.next.sha256"), m.launcherHash);
    return true;
}

static DWORD WINAPI updateThread(void*) {
    const std::wstring dir = appDir();
    migrateAdjacentData(dir);
    const std::wstring game = joinPath(dir, L"cinderlift.exe");
    const std::wstring adjacentGame = joinPath(parentDir(exePath()), L"cinderlift.exe");
    bool importedLocalBuild = false;
    if (!exists(game) && exists(adjacentGame))
        importedLocalBuild = CopyFileW(adjacentGame.c_str(), game.c_str(), TRUE) != 0;
    const std::wstring manifestPath = joinPath(dir, L"cinderlift-manifest.tmp");
    postStatus(L"Checking GitHub Releases...", false);
    std::wstring error;
    if (!download(MANIFEST_URL, manifestPath, error)) {
        const bool haveGame = exists(game);
        if (error.find(L"HTTP 404") != std::wstring::npos) {
            postStatus(haveGame
                ? (importedLocalBuild ? L"No GitHub Release exists yet. Using the adjacent development build."
                                      : L"No GitHub Release exists yet. Using the installed build.")
                : L"No GitHub Release exists yet. Publish the first v* tag to enable downloads.", haveGame);
        } else {
            postStatus(haveGame ? L"Could not check for updates. You can still play offline.\r\n" + error
                                : L"Could not download Cinderlift.\r\n" + error, haveGame);
        }
        InterlockedExchange(&g_busy, 0); return 0;
    }
    Manifest m;
    if (!parseManifest(manifestPath, m)) {
        DeleteFileW(manifestPath.c_str());
        postStatus(L"The release manifest was invalid; no files were changed.", exists(game));
        InterlockedExchange(&g_busy, 0); return 0;
    }
    DeleteFileW(manifestPath.c_str());
    std::wstring installedHash;
    const bool current = exists(game) && sha256(game, installedHash) && installedHash == m.gameHash;
    if (!current) {
        postStatus(L"Downloading Cinderlift " + m.version + L"...", false);
        const std::wstring temp = joinPath(dir, L"cinderlift.download.exe");
        if (!download(m.gameUrl, temp, error)) {
            postStatus(L"Game update failed; your previous version was kept.\r\n" + error, exists(game));
            InterlockedExchange(&g_busy, 0); return 0;
        }
        std::wstring hash;
        if (!sha256(temp, hash) || hash != m.gameHash) {
            DeleteFileW(temp.c_str());
            postStatus(L"Game checksum did not match; the download was discarded.", exists(game));
            InterlockedExchange(&g_busy, 0); return 0;
        }
        if (!MoveFileExW(temp.c_str(), game.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temp.c_str());
            postStatus(L"Could not install the update. Close Cinderlift and try again.", exists(game));
            InterlockedExchange(&g_busy, 0); return 0;
        }
    }
    writeText(joinPath(dir, L"version.txt"), m.version);
    std::wstring selfError;
    stageSelfUpdate(m, selfError);
    const bool launcherReady = exists(joinPath(parentDir(exePath()), L"cinderlift-launcher.next.exe"));
    std::wstring message = current ? L"Cinderlift " + m.version + L" is up to date."
                                   : L"Updated to Cinderlift " + m.version + L".";
    if (launcherReady) message += L"\r\nLauncher update staged; it will install next time you open it.";
    else if (!selfError.empty()) message += L"\r\nLauncher update check failed: " + selfError;
    postStatus(message, true);
    InterlockedExchange(&g_busy, 0); return 0;
}

static void beginUpdate() {
    if (InterlockedCompareExchange(&g_busy, 1, 0) != 0) return;
    EnableWindow(g_play, FALSE); EnableWindow(g_check, FALSE);
    HANDLE t = CreateThread(0, 0, updateThread, 0, 0, 0); if (t) CloseHandle(t);
    else { InterlockedExchange(&g_busy, 0); postStatus(L"Could not start update check.", exists(joinPath(appDir(), L"cinderlift.exe"))); }
}

static bool applyStagedUpdate() {
    const std::wstring current = exePath(), dir = parentDir(current);
    const std::wstring next = joinPath(dir, L"cinderlift-launcher.next.exe");
    const std::wstring hashPath = joinPath(dir, L"cinderlift-launcher.next.sha256");
    if (!exists(next) || !exists(hashPath)) return false;
    std::wstring expected = readText(hashPath), actual;
    if (!validHash(expected) || !sha256(next, actual) || actual != expected) {
        DeleteFileW(next.c_str()); DeleteFileW(hashPath.c_str()); return false;
    }
    wchar_t args[33000];
    _snwprintf(args, 32999, L"--replace \"%ls\" %lu", current.c_str(), GetCurrentProcessId()); args[32999] = 0;
    if (launch(next, args, dir)) return true;
    return false;
}

static int replacementMode(int argc, wchar_t** argv) {
    if (argc < 4) return -1;
    const bool replaceOnly = wcscmp(argv[1], L"--replace-only") == 0;
    if (!replaceOnly && wcscmp(argv[1], L"--replace") != 0) return -1;
    DWORD pid = wcstoul(argv[3], 0, 10);
    HANDLE old = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (old) { WaitForSingleObject(old, 30000); CloseHandle(old); }
    const std::wstring target = argv[2], installing = target + L".installing";
    if (!CopyFileW(exePath().c_str(), installing.c_str(), FALSE) ||
        !MoveFileExW(installing.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 2;
    if (replaceOnly) return 0; /* headless verifier; production uses --replace */
    wchar_t args[33000];
    _snwprintf(args, 32999, L"--cleanup \"%ls\" %lu", exePath().c_str(), GetCurrentProcessId()); args[32999] = 0;
    return launch(target, args, parentDir(target)) ? 0 : 3;
}

static void cleanupMode(int argc, wchar_t** argv) {
    if (argc < 4 || wcscmp(argv[1], L"--cleanup") != 0) return;
    DWORD pid = wcstoul(argv[3], 0, 10);
    HANDLE helper = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (helper) { WaitForSingleObject(helper, 30000); CloseHandle(helper); }
    DeleteFileW(argv[2]);
    DeleteFileW(joinPath(parentDir(exePath()), L"cinderlift-launcher.next.sha256").c_str());
}

/* Headless release-pipeline check: the same parser and hashing path the UI
   uses, without touching the network or the user's install directory. */
static int verificationMode(int argc, wchar_t** argv) {
    if (argc < 5 || wcscmp(argv[1], L"--verify-manifest") != 0) return -1;
    Manifest m; if (!parseManifest(argv[2], m)) return 10;
    std::wstring game, launcher;
    if (!sha256(argv[3], game) || game != m.gameHash) return 11;
    if (!sha256(argv[4], launcher) || launcher != m.launcherHash) return 12;
    return 0;
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND && LOWORD(wp) == ID_PLAY) {
        const std::wstring dir = appDir(), game = joinPath(dir, L"cinderlift.exe");
        if (launch(game, L"", dir)) DestroyWindow(hwnd);
        else MessageBoxW(hwnd, L"Cinderlift could not be started.", L"Cinderlift Launcher", MB_ICONERROR);
        return 0;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == ID_CHECK) { beginUpdate(); return 0; }
    if (msg == WM_STATUS) {
        wchar_t* text = (wchar_t*)lp;
        SetWindowTextW(g_status, text ? text : L"");
        if (text) HeapFree(GetProcessHeap(), 0, text);
        EnableWindow(g_play, wp != 0); EnableWindow(g_check, TRUE);
        return 0;
    }
    if (msg == WM_CTLCOLORSTATIC) {
        HDC dc = (HDC)wp;
        SetBkColor(dc, COL_BG);
        SetTextColor(dc, (HWND)lp == g_title ? COL_ACCENT : COL_TEXT);
        return (LRESULT)g_bgBrush;
    }
    if (msg == WM_DRAWITEM) {
        DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)lp;
        if (d->CtlID == ID_PLAY || d->CtlID == ID_CHECK) {
            const bool disabled = (d->itemState & ODS_DISABLED) != 0;
            FillRect(d->hDC, &d->rcItem,
                     (d->itemState & ODS_SELECTED) ? g_buttonPressedBrush : g_buttonBrush);
            HBRUSH frame = CreateSolidBrush(!disabled && d->CtlID == ID_PLAY ? COL_ACCENT : COL_BORDER);
            FrameRect(d->hDC, &d->rcItem, frame); DeleteObject(frame);
            SetBkMode(d->hDC, TRANSPARENT);
            SetTextColor(d->hDC, disabled ? COL_MUTED : (d->CtlID == ID_PLAY ? COL_ACCENT : COL_TEXT));
            wchar_t label[64]; GetWindowTextW(d->hwndItem, label, 64);
            RECT text = d->rcItem;
            DrawTextW(d->hDC, label, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
    }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int verification = verificationMode(argc, argv);
    if (verification >= 0) { LocalFree(argv); return verification; }
    int replacement = replacementMode(argc, argv);
    if (replacement >= 0) { LocalFree(argv); return replacement; }
    cleanupMode(argc, argv);
    if (argc < 2 && applyStagedUpdate()) { LocalFree(argv); return 0; }
    LocalFree(argv);

    WNDCLASSW wc; ZeroMemory(&wc, sizeof(wc)); wc.lpfnWndProc = wndProc; wc.hInstance = instance;
    g_bgBrush = CreateSolidBrush(COL_BG);
    g_buttonBrush = CreateSolidBrush(COL_BUTTON);
    g_buttonPressedBrush = CreateSolidBrush(COL_BUTTON_HOT);
    wc.hCursor = LoadCursor(0, IDC_ARROW); wc.hbrBackground = g_bgBrush;
    wc.lpszClassName = L"CinderliftLauncherWindow";
    if (!RegisterClassW(&wc)) return 1;
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Cinderlift Launcher",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 245, 0, 0, instance, 0);
    if (!hwnd) return 1;
    HFONT titleFont = CreateFontW(30, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
    HFONT font = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
    g_title = CreateWindowW(L"STATIC", L"CINDERLIFT", WS_CHILD | WS_VISIBLE,
                            24, 20, 450, 42, hwnd, 0, instance, 0);
    g_status = CreateWindowW(L"STATIC", L"Starting...", WS_CHILD | WS_VISIBLE,
                             26, 70, 455, 52, hwnd, 0, instance, 0);
    g_play = CreateWindowW(L"BUTTON", L"Play", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                           26, 142, 210, 38, hwnd, (HMENU)ID_PLAY, instance, 0);
    g_check = CreateWindowW(L"BUTTON", L"Check Again", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                            250, 142, 210, 38, hwnd, (HMENU)ID_CHECK, instance, 0);
    SendMessageW(g_title, WM_SETFONT, (WPARAM)titleFont, TRUE);
    SendMessageW(g_status, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(g_play, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(g_check, WM_SETFONT, (WPARAM)font, TRUE);
    EnableWindow(g_play, FALSE); EnableWindow(g_check, FALSE);
    ShowWindow(hwnd, show); UpdateWindow(hwnd); beginUpdate();
    MSG msg; while (GetMessageW(&msg, 0, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    DeleteObject(titleFont); DeleteObject(font);
    DeleteObject(g_buttonPressedBrush); DeleteObject(g_buttonBrush); DeleteObject(g_bgBrush);
    return (int)msg.wParam;
}
