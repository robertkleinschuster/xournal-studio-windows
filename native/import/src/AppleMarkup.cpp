#include "xournal/import/AppleMarkup.h"

#include "BinaryPlist.h"

#include "xournal/base/Error.h"

#include <windows.h>
#include <wincrypt.h>

namespace xournal::import
{
    namespace
    {
        using detail::BinaryPlist;

        std::vector<std::uint8_t> FromBase64(std::string_view text)
        {
            DWORD size = 0;
            if (!CryptStringToBinaryA(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, nullptr, &size, nullptr, nullptr))
            {
                ThrowInvalidData("markup archive: not base64");
            }
            std::vector<std::uint8_t> data(size);
            if (!CryptStringToBinaryA(text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, data.data(), &size, nullptr, nullptr))
            {
                ThrowInvalidData("markup archive: not base64");
            }
            data.resize(size);
            return data;
        }

        // An archive's objects refer to each other by UID (indexes into $objects).
        class Archive
        {
        public:
            explicit Archive(BinaryPlist const& plist) : plist(plist)
            {
                auto top = plist.Object(plist.Top());
                auto const* root = std::get_if<BinaryPlist::Dictionary>(&top);
                auto objectsRef = root ? plist.Find(*root, L"$objects") : std::nullopt;
                auto topRef = root ? plist.Find(*root, L"$top") : std::nullopt;
                if (!objectsRef || !topRef) ThrowInvalidData("markup archive: not a keyed archive");
                auto o = plist.Object(*objectsRef);
                auto const* array = std::get_if<BinaryPlist::Array>(&o);
                if (!array) ThrowInvalidData("markup archive: $objects is no array");
                objects = array->items;
                topEntries = plist.Object(*topRef);
            }

            // The object behind a reference (UIDs resolved through $objects).
            BinaryPlist::Value Resolve(std::uint64_t number) const
            {
                auto value = plist.Object(number);
                if (auto const* uid = std::get_if<BinaryPlist::Uid>(&value))
                {
                    if (uid->value >= objects.size()) ThrowInvalidData("markup archive: reference beyond $objects");
                    return plist.Object(objects[static_cast<size_t>(uid->value)]);
                }
                return value;
            }

            std::optional<BinaryPlist::Value> Member(BinaryPlist::Dictionary const& d, std::wstring_view key) const
            {
                auto ref = plist.Find(d, key);
                if (!ref) return std::nullopt;
                return Resolve(*ref);
            }

            BinaryPlist const& plist;
            std::vector<std::uint64_t> objects;
            BinaryPlist::Value topEntries;
        };

        double Number(BinaryPlist::Value const& v)
        {
            if (auto const* i = std::get_if<std::int64_t>(&v)) return static_cast<double>(*i);
            if (auto const* d = std::get_if<double>(&v)) return *d;
            return 0;
        }
    }

    AppleMarkup AppleMarkup::Read(std::string_view base64)
    {
        auto bytes = FromBase64(base64);
        BinaryPlist plist(bytes);
        Archive archive(plist);
        auto const* top = std::get_if<BinaryPlist::Dictionary>(&archive.topEntries);
        auto rootValue = top ? archive.Member(*top, L"root") : std::nullopt;
        auto const* root = rootValue ? std::get_if<BinaryPlist::Dictionary>(&*rootValue) : nullptr;
        if (!root) ThrowInvalidData("markup archive: no root object");

        AppleMarkup markup;
        if (auto cls = archive.Member(*root, L"$class"))
        {
            if (auto const* c = std::get_if<BinaryPlist::Dictionary>(&*cls))
            {
                if (auto name = archive.Member(*c, L"$classname"))
                {
                    if (auto const* s = std::get_if<std::wstring>(&*name)) markup.className = *s;
                }
            }
        }
        if (auto drawing = archive.Member(*root, L"drawing"))
        {
            if (auto const* data = std::get_if<BinaryPlist::Bytes>(&*drawing)) markup.drawing = *data;
        }
        if (auto size = archive.Member(*root, L"drawingSize"))
        {
            if (auto const* d = std::get_if<BinaryPlist::Dictionary>(&*size))
            {
                auto keys = archive.Member(*d, L"NS.keys");
                auto values = archive.Member(*d, L"NS.objects");
                auto const* k = keys ? std::get_if<BinaryPlist::Array>(&*keys) : nullptr;
                auto const* v = values ? std::get_if<BinaryPlist::Array>(&*values) : nullptr;
                for (size_t i = 0; k && v && i < k->items.size() && i < v->items.size(); ++i)
                {
                    auto key = archive.Resolve(k->items[i]);
                    double value = Number(archive.Resolve(v->items[i]));
                    if (auto const* s = std::get_if<std::wstring>(&key))
                    {
                        if (*s == L"Width") markup.drawingWidth = value;
                        if (*s == L"Height") markup.drawingHeight = value;
                    }
                }
            }
        }
        return markup;
    }
}
