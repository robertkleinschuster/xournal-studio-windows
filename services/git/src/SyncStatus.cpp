#include "xournal/git/SyncStatus.h"

#include <format>

namespace xournal::git
{
    namespace
    {
        // "1 Commit zu senden", "2 Commits zu senden"; `rest` may be empty.
        std::wstring Count(size_t n, std::wstring_view one, std::wstring_view many, std::wstring_view rest)
        {
            std::wstring text = std::format(L"{} {}", n, n == 1 ? one : many);
            return rest.empty() ? text : text + L" " + std::wstring(rest);
        }
    }

    std::wstring SyncStatus::ButtonText() const
    {
        std::wstring text = L"Synchronisieren";
        if (!repository)
        {
            return text;
        }
        std::wstring pending;
        if (changedFiles > 0)
        {
            pending += std::format(L"  {}", changedFiles);
        }
        if (divergence && divergence->ahead > 0)
        {
            pending += std::format(L"  ↑{}", divergence->ahead);
        }
        if (divergence && !remoteKnown)
        {
            pending += L"  ↓?";
        }
        else if (divergence && divergence->behind > 0)
        {
            pending += std::format(L"  ↓{}", divergence->behind);
        }
        return pending.empty() ? text : text + L" " + pending;
    }

    std::wstring SyncStatus::Details() const
    {
        if (!repository)
        {
            return L"Kein Git-Repository: Synchronisieren braucht einen Projektordner mit Git.";
        }
        std::wstring lines;
        auto add = [&lines](std::wstring const& line) { lines += (lines.empty() ? L"" : L"\n") + line; };
        if (changedFiles > 0)
        {
            add(Count(changedFiles, L"geänderte Datei", L"geänderte Dateien", L""));
        }
        if (!divergence)
        {
            add(L"Kein Upstream-Zweig: es gibt nichts zu senden oder zu holen.");
            return lines;
        }
        if (divergence->ahead > 0)
        {
            add(Count(divergence->ahead, L"Commit", L"Commits", L"zu senden"));
        }
        if (!remoteKnown)
        {
            add(L"Stand des Servers unbekannt (Abruf fehlgeschlagen).");
        }
        else if (divergence->behind > 0)
        {
            add(Count(divergence->behind, L"Commit", L"Commits", L"zu holen"));
        }
        return lines.empty() ? L"Alles synchronisiert." : lines;
    }
}
