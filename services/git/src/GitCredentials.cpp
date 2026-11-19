#include "xournal/git/GitCredentials.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <windows.h>
#include <wincred.h>
#include <winhttp.h>

#include <format>
#include <memory>

namespace xournal::git::GitCredentials
{
    namespace
    {
        struct CredentialFree
        {
            void operator()(CREDENTIALW* c) const { CredFree(c); }
        };

        struct Remote
        {
            std::wstring scheme;
            std::wstring host;
            std::wstring hostAndPort; // the port only when it is not the scheme's default
        };

        Remote Crack(std::string const& url)
        {
            std::wstring wideUrl = FromUtf8(url);
            URL_COMPONENTS parts{};
            parts.dwStructSize = sizeof(parts);
            parts.dwSchemeLength = static_cast<DWORD>(-1);
            parts.dwHostNameLength = static_cast<DWORD>(-1);
            if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &parts))
            {
                ThrowLastError(std::format("git sign-in: URL {}", url));
            }
            Remote remote{ std::wstring(parts.lpszScheme, parts.dwSchemeLength), std::wstring(parts.lpszHostName, parts.dwHostNameLength), {} };
            remote.hostAndPort = remote.host;
            INTERNET_PORT defaultPort = parts.nScheme == INTERNET_SCHEME_HTTPS ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
            if (parts.nPort != defaultPort)
            {
                remote.hostAndPort += std::format(L":{}", parts.nPort);
            }
            return remote;
        }

        [[noreturn]] void ThrowMessage(std::string const& message)
        {
            LogError(message);
            throw Error(message, E_FAIL);
        }
    }

    std::vector<std::wstring> Targets(std::string const& url, std::string const& userNameFromUrl)
    {
        Remote remote = Crack(url);
        std::vector<std::wstring> targets;
        if (!userNameFromUrl.empty())
        {
            targets.push_back(std::format(L"git:{}://{}@{}", remote.scheme, FromUtf8(userNameFromUrl), remote.hostAndPort));
        }
        targets.push_back(std::format(L"git:{}://{}", remote.scheme, remote.hostAndPort));
        return targets;
    }

    std::string DecodeSecret(std::span<std::uint8_t const> blob)
    {
        bool utf16 = blob.size() >= 2 && blob.size() % 2 == 0;
        for (size_t i = 1; utf16 && i < blob.size(); i += 2)
        {
            utf16 = blob[i] == 0;
        }
        if (utf16)
        {
            return ToUtf8(std::wstring_view(reinterpret_cast<wchar_t const*>(blob.data()), blob.size() / 2));
        }
        return std::string(reinterpret_cast<char const*>(blob.data()), blob.size());
    }

    Credential Get(std::string const& url, std::string const& userNameFromUrl)
    {
        for (std::wstring const& target : Targets(url, userNameFromUrl))
        {
            CREDENTIALW* found = nullptr;
            if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &found))
            {
                if (GetLastError() == ERROR_NOT_FOUND)
                {
                    continue;
                }
                ThrowLastError(std::format("git sign-in: read credential {}", ToUtf8(target)));
            }
            std::unique_ptr<CREDENTIALW, CredentialFree> stored(found);
            Credential credential;
            credential.userName = stored->UserName ? ToUtf8(stored->UserName) : userNameFromUrl;
            credential.password = DecodeSecret({ stored->CredentialBlob, stored->CredentialBlobSize });
            return credential;
        }
        ThrowMessage(std::format("Keine Git-Anmeldung für {} gespeichert. Einmal mit Git für Windows anmelden "
            "(z. B. „git pull“ im Ordner), dann erneut synchronisieren.", ToUtf8(Crack(url).host)));
    }

    CredentialProvider FromCredentialManager()
    {
        return [](std::string const& url, std::string const& userNameFromUrl) { return Get(url, userNameFromUrl); };
    }
}
