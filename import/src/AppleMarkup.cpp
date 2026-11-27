#include "xournal/import/AppleMarkup.h"

#include "BinaryPlist.h"
#include "BinaryPlistWriter.h"

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

    std::vector<std::uint8_t> AppleMarkup::DecodeBase64(std::string_view text) { return FromBase64(text); }

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

    namespace
    {
        using detail::BinaryPlistWriter;

        std::string ToBase64(std::span<std::uint8_t const> bytes)
        {
            DWORD size = 0;
            if (!CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &size))
            {
                ThrowLastError("CryptBinaryToString (markup archive)");
            }
            std::string text(size, '\0');
            if (!CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, text.data(), &size))
            {
                ThrowLastError("CryptBinaryToString (markup archive)");
            }
            text.resize(size);
            return text;
        }
    }

    // The objects in the order iPadOS archives them (object numbers as in the user's PDFs): 0 $null, 1 the annotation, 2 its
    // UUID, 3 the rectangle (an NSMutableDictionary: Width, Height, Y, X), 12 that class, 13 the drawing, 14 the drawing's
    // size (Width, Height), 17 the annotation's class. A UID is only a number (an index into $objects), so the objects
    // can be written in that order without looking ahead.
    std::string AppleMarkup::WriteInk(std::span<std::uint8_t const> drawing, double drawingWidth, double drawingHeight,
                                      double x, double y, double width, double height, std::string_view uuid)
    {
        BinaryPlistWriter w;
        using Ref = BinaryPlistWriter::Ref;
        auto uid = [&](std::uint64_t index) { return w.Uid(index); };
        auto key = [&](char const* name) { return w.String(name); };
        auto names = [&](std::vector<char const*> const& classes)
        {
            std::vector<Ref> items;
            for (auto name : classes) items.push_back(w.String(name));
            return w.Array(items);
        };
        std::vector<Ref> objects;
        objects.push_back(w.String("$null"));                                             // 0
        objects.push_back(w.Dictionary({                                                  // 1 the annotation
            { key("textIsFixedHeight"), w.Bool(false) }, { key("originalModelBaseScaleFactor"), w.Real(0) },
            { key("drawing"), uid(13) }, { key("akPlat"), w.Int(2) }, { key("akVers"), w.Int(2) },
            { key("formContentType"), w.Int(0) }, { key("AKIsFormFieldKey"), w.Bool(false) }, { key("UUID"), uid(2) },
            { key("rectangle"), uid(3) }, { key("shouldUsePlaceholderText"), w.Bool(true) },
            { key("textIsClipped"), w.Bool(false) }, { key("drawingSize"), uid(14) },
            { key("editsDisableAppearanceOverride"), w.Bool(true) }, { key("textIsFixedWidth"), w.Bool(false) },
            { key("customPlaceholderText"), uid(0) }, { key("$class"), uid(17) }, { key("originalExifOrientation"), w.Int(1) } }));
        objects.push_back(w.String(uuid));                                                // 2
        objects.push_back(w.Dictionary({ { key("NS.keys"), w.Array({ uid(4), uid(5), uid(6), uid(7) }) },   // 3
            { key("NS.objects"), w.Array({ uid(8), uid(9), uid(10), uid(11) }) }, { key("$class"), uid(12) } }));
        for (char const* name : { "Width", "Height", "Y", "X" }) objects.push_back(w.String(name));         // 4-7
        for (double value : { width, height, y, x }) objects.push_back(w.Real(value));                       // 8-11
        objects.push_back(w.Dictionary({ { key("$classname"), w.String("NSMutableDictionary") },             // 12
            { key("$classes"), names({ "NSMutableDictionary", "NSDictionary", "NSObject" }) } }));
        objects.push_back(w.Data(drawing));                                               // 13
        objects.push_back(w.Dictionary({ { key("NS.keys"), w.Array({ uid(4), uid(5) }) },                    // 14
            { key("NS.objects"), w.Array({ uid(15), uid(16) }) }, { key("$class"), uid(12) } }));
        objects.push_back(w.Real(drawingWidth));                                          // 15
        objects.push_back(w.Real(drawingHeight));                                         // 16
        objects.push_back(w.Dictionary({ { key("$classname"), w.String("AKInkAnnotation2") },                // 17
            { key("$classes"), names({ "AKInkAnnotation2", "AKInkAnnotation", "AKAnnotation", "NSObject" }) } }));
        Ref top = w.Dictionary({ { key("$version"), w.Int(100000) }, { key("$archiver"), w.String("NSKeyedArchiver") },
            { key("$top"), w.Dictionary({ { key("root"), uid(1) } }) }, { key("$objects"), w.Array(objects) } });
        return ToBase64(w.Finish(top));
    }
}
