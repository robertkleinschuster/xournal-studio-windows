#include "xournal/settings/Settings.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/io/Files.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

#include <format>

using namespace winrt::Windows::Data::Json;

namespace xournal::settings
{
    namespace
    {
        bool Has(JsonObject const& o, wchar_t const* key, JsonValueType type)
        {
            return o.HasKey(key) && o.GetNamedValue(key).ValueType() == type;
        }

        std::optional<std::wstring> String(JsonObject const& o, wchar_t const* key)
        {
            return Has(o, key, JsonValueType::String) ? std::optional(std::wstring(o.GetNamedString(key))) : std::nullopt;
        }

        double Number(JsonObject const& o, wchar_t const* key, double fallback)
        {
            return Has(o, key, JsonValueType::Number) ? o.GetNamedNumber(key) : fallback;
        }

        bool Boolean(JsonObject const& o, wchar_t const* key, bool fallback)
        {
            return Has(o, key, JsonValueType::Boolean) ? o.GetNamedBoolean(key) : fallback;
        }

        std::optional<WindowState> ReadWindow(JsonObject const& o)
        {
            if (!Has(o, L"window", JsonValueType::Object))
            {
                return std::nullopt;
            }
            JsonObject w = o.GetNamedObject(L"window");
            WindowState state;
            state.normal = { static_cast<LONG>(Number(w, L"left", 0)), static_cast<LONG>(Number(w, L"top", 0)),
                static_cast<LONG>(Number(w, L"right", 0)), static_cast<LONG>(Number(w, L"bottom", 0)) };
            state.maximized = Boolean(w, L"maximized", true);
            if (state.normal.right <= state.normal.left || state.normal.bottom <= state.normal.top)
            {
                return std::nullopt;
            }
            return state;
        }

        JsonObject WriteWindow(WindowState const& window)
        {
            JsonObject w;
            w.SetNamedValue(L"left", JsonValue::CreateNumberValue(window.normal.left));
            w.SetNamedValue(L"top", JsonValue::CreateNumberValue(window.normal.top));
            w.SetNamedValue(L"right", JsonValue::CreateNumberValue(window.normal.right));
            w.SetNamedValue(L"bottom", JsonValue::CreateNumberValue(window.normal.bottom));
            w.SetNamedValue(L"maximized", JsonValue::CreateBooleanValue(window.maximized));
            return w;
        }
    }

    std::filesystem::path Settings::FileIn(std::filesystem::path const& appFolder)
    {
        return appFolder / L"settings-native.json";
    }

    Settings Settings::Load(std::filesystem::path file)
    {
        Settings settings;
        settings.file = std::move(file);
        if (!Paths::FileExists(settings.file))
        {
            return settings;
        }
        auto bytes = io::Files::ReadAll(settings.file);
        std::wstring text = FromUtf8(std::string_view(reinterpret_cast<char const*>(bytes.data()), bytes.size()));
        JsonObject o{ nullptr };
        if (!JsonObject::TryParse(text, o))
        {
            LogError(std::format("settings: {} is not JSON - defaults are used, the file stays until the next save",
                ToUtf8(settings.file.native())));
            return settings;
        }
        settings.readJson = text;
        settings.window = ReadWindow(o);
        if (auto last = String(o, L"lastFile")) settings.lastFile = *last;
        if (auto folder = String(o, L"projectFolder")) settings.projectFolder = *folder;
        settings.paneOpen = Boolean(o, L"paneOpen", false);
        settings.darkMode = Boolean(o, L"darkMode", false);
        settings.paneTab = static_cast<int>(Number(o, L"paneTab", 0));
        settings.doneTasksShown = Boolean(o, L"doneTasksShown", false);
        settings.penHasWritten = Boolean(o, L"penHasWritten", false);
        settings.mouseHintShown = Boolean(o, L"mouseHintShown", false);
        if (auto choices = String(o, L"toolChoices")) settings.toolChoices = ToUtf8(*choices);
        if (Has(o, L"documentPositions", JsonValueType::Object))
        {
            for (auto const& [key, value] : o.GetNamedObject(L"documentPositions"))
            {
                if (value.ValueType() != JsonValueType::Object)
                {
                    continue;
                }
                JsonObject position = value.GetObject();
                settings.documentPositions[std::wstring(key)] = { static_cast<int>(Number(position, L"page", 0)),
                    Number(position, L"x", 0), Number(position, L"y", 0), Number(position, L"zoom", 1) };
            }
        }
        if (Has(o, L"recentFiles", JsonValueType::Array))
        {
            for (auto const& value : o.GetNamedArray(L"recentFiles"))
            {
                if (value.ValueType() == JsonValueType::String && settings.recentFiles.size() < MaxRecentFiles)
                {
                    settings.recentFiles.emplace_back(std::wstring(value.GetString()));
                }
            }
        }
        return settings;
    }

    void Settings::RememberRecent(std::filesystem::path const& recent)
    {
        std::erase_if(recentFiles, [&](std::filesystem::path const& p) { return EqualsIgnoreCase(p.native(), recent.native()); });
        recentFiles.insert(recentFiles.begin(), recent);
        if (recentFiles.size() > MaxRecentFiles) recentFiles.resize(MaxRecentFiles);
    }

    void Settings::Save() const
    {
        JsonObject o{ nullptr };
        if (readJson.empty() || !JsonObject::TryParse(readJson, o))
        {
            o = JsonObject();
        }
        auto set = [&](wchar_t const* key, std::optional<JsonValue> const& value)
        {
            if (value)
            {
                o.SetNamedValue(key, *value);
            }
            else if (o.HasKey(key))
            {
                o.Remove(key);
            }
        };
        auto pathValue = [](std::optional<std::filesystem::path> const& p)
        {
            return p ? std::optional(JsonValue::CreateStringValue(p->native())) : std::nullopt;
        };
        if (window)
        {
            o.SetNamedValue(L"window", WriteWindow(*window));
        }
        else if (o.HasKey(L"window"))
        {
            o.Remove(L"window");
        }
        set(L"lastFile", pathValue(lastFile));
        set(L"projectFolder", pathValue(projectFolder));
        set(L"paneOpen", JsonValue::CreateBooleanValue(paneOpen));
        set(L"darkMode", JsonValue::CreateBooleanValue(darkMode));
        set(L"paneTab", JsonValue::CreateNumberValue(paneTab));
        set(L"doneTasksShown", JsonValue::CreateBooleanValue(doneTasksShown));
        set(L"penHasWritten", JsonValue::CreateBooleanValue(penHasWritten));
        set(L"mouseHintShown", JsonValue::CreateBooleanValue(mouseHintShown));
        set(L"toolChoices", JsonValue::CreateStringValue(FromUtf8(toolChoices)));
        JsonObject positions;
        for (auto const& [document, position] : documentPositions)
        {
            JsonObject entry;
            entry.SetNamedValue(L"page", JsonValue::CreateNumberValue(position.page));
            entry.SetNamedValue(L"x", JsonValue::CreateNumberValue(position.x));
            entry.SetNamedValue(L"y", JsonValue::CreateNumberValue(position.y));
            entry.SetNamedValue(L"zoom", JsonValue::CreateNumberValue(position.zoom));
            positions.SetNamedValue(document, entry);
        }
        o.SetNamedValue(L"documentPositions", positions);
        JsonArray recent;
        for (auto const& path : recentFiles)
        {
            recent.Append(JsonValue::CreateStringValue(path.native()));
        }
        o.SetNamedValue(L"recentFiles", recent);
        std::filesystem::path folder = file.parent_path();
        if (std::error_code error; !folder.empty() && (std::filesystem::create_directories(folder, error), error))
        {
            ThrowHr(std::format("create folder {}", ToUtf8(folder.native())), HRESULT_FROM_WIN32(error.value()));
        }
        std::string text = ToUtf8(o.Stringify());
        io::Files::WriteReplacing(file, { reinterpret_cast<std::uint8_t const*>(text.data()), text.size() });
    }
}
