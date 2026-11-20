#pragma once

#include <windows.h>

#include <cstdint>
#include <span>
#include <string>

namespace xournal::Clipboard
{
    class Opened
    {
    public:
        explicit Opened(HWND owner);
        ~Opened();
        Opened(Opened const&) = delete;
        Opened& operator=(Opened const&) = delete;

        void Clear() const;
        void SetData(UINT format, void const* bytes, size_t size) const;
        void SetBytes(UINT format, std::span<std::uint8_t const> bytes) const
        {
            SetData(format, bytes.data(), bytes.size());
        }
    };

    void PutText(HWND owner, std::wstring const& text);
}
