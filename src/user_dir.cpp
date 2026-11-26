// The user folder (saves, settings, caches, title update, DLC): Documents\fm4 (FM2 used Documents\fm2), in the Documents folder
// Windows reports, which the launcher and the installer use too. A moved Documents folder (E:\Documents)
// isn't %USERPROFILE%\Documents: building the path from that made the game miss the title update and
// ignore the launcher's settings (GitHub issue #2). If only the old %USERPROFILE%\Documents\fm4 exists
// (the workaround for that bug), it's kept.
#include "user_dir.h"

#include <windows.h>

#include <shlobj.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
std::filesystem::path DocumentsDir();

// The portable package: portable.txt beside fm4.exe keeps everything in user\ beside it (nothing in Documents, which
// may be a OneDrive folder). The first time, an existing Documents\fm4 is copied in (saves, settings, DLC, caches; the
// original is left where it is). The launcher does the same (launcher/FM4Launcher/Install.cs, Paths.User).
std::filesystem::path PortableDir()
{
    wchar_t exe[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return {};
    const std::filesystem::path root = std::filesystem::path(exe).parent_path();
    std::error_code ec;
    if (!std::filesystem::exists(root / "portable.txt", ec))
        return {};
    const std::filesystem::path user = root / "user";
    if (!std::filesystem::exists(user, ec))
    {
        const std::filesystem::path old = DocumentsDir();
        if (!old.empty() && std::filesystem::is_directory(old, ec))
            std::filesystem::copy(old, user, std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);
        std::filesystem::create_directories(user, ec);
    }
    return user;
}
} // namespace

const std::filesystem::path& DocumentsUserDir()
{
    static const std::filesystem::path dir = [] {
        std::error_code ec;
        // FM4_USER_DIR: keep everything in one folder of the caller's choosing (a copy that travels with the game).
        if (wchar_t custom[MAX_PATH]; GetEnvironmentVariableW(L"FM4_USER_DIR", custom, MAX_PATH) - 1u < MAX_PATH - 1u)
            return std::filesystem::absolute(std::filesystem::path(custom), ec);
        if (std::filesystem::path portable = PortableDir(); !portable.empty())
            return portable;
        return DocumentsDir();
    }();
    return dir;
}

namespace
{
// Documents\fm4 as Windows reports Documents (or the old %USERPROFILE%\Documents\fm4, see the top).
std::filesystem::path DocumentsDir()
{
    std::error_code ec;
    std::filesystem::path documents;
    PWSTR known = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &known)) && known)
        documents = known;
    CoTaskMemFree(known);
    std::filesystem::path old;
    if (wchar_t profile[MAX_PATH]; GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH))
        old = std::filesystem::path(profile) / "Documents" / "fm4";
    if (documents.empty())
        return old.empty() ? std::filesystem::path("user") : old;
    const std::filesystem::path current = documents / "fm4";
    if (!old.empty() && !std::filesystem::exists(current, ec) && std::filesystem::exists(old, ec) && !std::filesystem::equivalent(old.parent_path(), documents, ec))
        return old;
    return current;
}
} // namespace
