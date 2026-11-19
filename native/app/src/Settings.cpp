#include "Settings.h"

#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/io/Files.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <format>

using namespace winrt::Windows::Data::Json;

namespace xournal::app
{
    namespace
    {
        std::optional<std::wstring> String(JsonObject const& o, wchar_t const* key)
        {
            if (!o.HasKey(key) || o.GetNamedValue(key).ValueType() != JsonValueType::String)
            {
                return std::nullopt;
            }
            return std::wstring(o.GetNamedString(key));
        }

        double Number(JsonObject const& o, wchar_t const* key, double fallback)
        {
            return o.HasKey(key) && o.GetNamedValue(key).ValueType() == JsonValueType::Number ? o.GetNamedNumber(key) : fallback;
        }

        bool Boolean(JsonObject const& o, wchar_t const* key, bool fallback)
        {
            return o.HasKey(key) && o.GetNamedValue(key).ValueType() == JsonValueType::Boolean ? o.GetNamedBoolean(key) : fallback;
        }
    }

    Settings Settings::Load(std::filesystem::path file)
    {
        Settings settings;
        settings.file = file;
        if (!std::filesystem::exists(file))
        {
            return settings;
        }
        auto bytes = io::Files::ReadAll(file);
        std::wstring text = FromUtf8(std::string_view(reinterpret_cast<char const*>(bytes.data()), bytes.size()));
        JsonObject o{ nullptr };
        if (!JsonObject::TryParse(text, o))
        {
            LogError(std::format("settings: {} is not JSON - defaults are used", ToUtf8(file.native())));
            return settings;
        }
        settings.unknownJson = text;
        if (o.HasKey(L"window") && o.GetNamedValue(L"window").ValueType() == JsonValueType::Object)
        {
            auto w = o.GetNamedObject(L"window");
            WindowState state;
            state.normal = { static_cast<LONG>(Number(w, L"left", 0)), static_cast<LONG>(Number(w, L"top", 0)),
                static_cast<LONG>(Number(w, L"right", 0)), static_cast<LONG>(Number(w, L"bottom", 0)) };
            state.maximized = Boolean(w, L"maximized", true);
            if (state.normal.right > state.normal.left && state.normal.bottom > state.normal.top)
            {
                settings.window = state;
            }
        }
        if (auto last = String(o, L"lastFile")) settings.lastFile = *last;
        if (auto folder = String(o, L"projectFolder")) settings.projectFolder = *folder;
        settings.paneOpen = Boolean(o, L"paneOpen", false);
        settings.paneTab = static_cast<int>(Number(o, L"paneTab", 0));
        if (auto choices = String(o, L"toolChoices")) settings.toolChoices = ToUtf8(*choices);
        return settings;
    }

    void Settings::Save() const
    {
        JsonObject o{ nullptr };
        if (unknownJson.empty() || !JsonObject::TryParse(unknownJson, o))
        {
            o = JsonObject();
        }
        if (window)
        {
            JsonObject w;
            w.SetNamedValue(L"left", JsonValue::CreateNumberValue(window->normal.left));
            w.SetNamedValue(L"top", JsonValue::CreateNumberValue(window->normal.top));
            w.SetNamedValue(L"right", JsonValue::CreateNumberValue(window->normal.right));
            w.SetNamedValue(L"bottom", JsonValue::CreateNumberValue(window->normal.bottom));
            w.SetNamedValue(L"maximized", JsonValue::CreateBooleanValue(window->maximized));
            o.SetNamedValue(L"window", w);
        }
        auto setPath = [&](wchar_t const* key, std::optional<std::filesystem::path> const& value)
        {
            if (value) o.SetNamedValue(key, JsonValue::CreateStringValue(value->native()));
            else if (o.HasKey(key)) o.Remove(key);
        };
        setPath(L"lastFile", lastFile);
        setPath(L"projectFolder", projectFolder);
        o.SetNamedValue(L"paneOpen", JsonValue::CreateBooleanValue(paneOpen));
        o.SetNamedValue(L"paneTab", JsonValue::CreateNumberValue(paneTab));
        o.SetNamedValue(L"toolChoices", JsonValue::CreateStringValue(FromUtf8(toolChoices)));
        std::string text = ToUtf8(o.Stringify());
        io::Files::WriteReplacing(file, { reinterpret_cast<std::uint8_t const*>(text.data()), text.size() });
    }
}
