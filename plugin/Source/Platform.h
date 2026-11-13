// Platform.h - the four lines that are different on Windows, in one file.
//
// This exists because the Windows build failed on exactly this: `std::filesystem::path::c_str()`
// is `const char*` on Linux and `const wchar_t*` on Windows, so a line that compiles here
// (`measureFile (p.path().c_str(), ...)`) is a C2664 there.  Worse than a compile error is
// the quiet version of the same mistake: `fopen (argv[i], "rb")` compiles everywhere and
// then fails to open a file whose name is Arabic, because MSVC's `main` hands over the
// ANSI code page, not UTF-8 - and Amir's stems are named in Arabic.
//
// So: paths enter through `pathFromUtf8`, leave as text through `pathUtf8`, and files are
// opened through `openRead`.  Nothing else in the project is allowed to call `fopen`.
#pragma once
#include <filesystem>
#include <string>
#include <cstdio>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace aeq {

// argv -> path.  On Windows an argument is not necessarily UTF-8: a name typed in a shell
// or handed over by Explorer arrives in the ANSI code page.  Try UTF-8 strictly; if it is
// not valid UTF-8, decode it as ACP, which is what the argument actually is.
inline std::filesystem::path pathFromUtf8 (const char* s)
{
    if (s == nullptr) return {};
#if defined(_WIN32)
    const std::string str (s);
    if (str.empty()) return {};
    const int wlen = ::MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, str.c_str(),
                                            int (str.size()), nullptr, 0);
    if (wlen > 0)
    {
        std::wstring w (std::size_t (wlen), L'\0');
        ::MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, str.c_str(), int (str.size()),
                               w.data(), wlen);
        return std::filesystem::path (w);
    }
    const int alen = ::MultiByteToWideChar (CP_ACP, 0, str.c_str(), int (str.size()), nullptr, 0);
    std::wstring w (std::size_t (alen > 0 ? alen : 0), L'\0');
    if (alen > 0)
        ::MultiByteToWideChar (CP_ACP, 0, str.c_str(), int (str.size()), w.data(), alen);
    return std::filesystem::path (w);
#else
    return std::filesystem::path (s);
#endif
}

// path -> text for a printf or a log line.  UTF-8 on every platform, so the Arabic file
// names print as Arabic instead of as question marks.
inline std::string pathUtf8 (const std::filesystem::path& p)
{
#if defined(__cpp_char8_t)
    const auto u8 = p.u8string();
    return std::string (u8.begin(), u8.end());
#else
    return p.u8string();
#endif
}

// The one place a file is opened for reading.  `_wfopen` on Windows is what makes the
// Arabic names work; on Linux this is a plain fopen with the native bytes.
inline std::FILE* openRead (const std::filesystem::path& p)
{
#ifdef _WIN32
    return ::_wfopen (p.native().c_str(), L"rb");
#else
    return std::fopen (p.native().c_str(), "rb");
#endif
}

// The one place a file is opened for writing.  Same reason as openRead.
inline std::FILE* openWrite (const std::filesystem::path& p)
{
#ifdef _WIN32
    return ::_wfopen (p.native().c_str(), L"wb");
#else
    return std::fopen (p.native().c_str(), "wb");
#endif
}

} // namespace aeq
