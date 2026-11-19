#pragma once

// Git sign-in: the credentials Git for Windows keeps in the Windows Credential Manager (the Git Credential Manager and
// git's wincred helper store the generic credential "git:https://host"), so the sync works wherever git was used
// before. The app never asks for credentials itself.

#include "xournal/git/GitSync.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace xournal::git::GitCredentials
{
    /// The stored credential for a remote URL (CredReadW of the targets below, first found wins). Throws (German message,
    /// for the app's InfoBar) when none is stored.
    Credential Get(std::string const& url, std::string const& userNameFromUrl);

    /// A CredentialProvider reading the Credential Manager (Get).
    CredentialProvider FromCredentialManager();

    /// The generic credential names Git for Windows uses for a URL: "git:https://user@host" (when the URL names a user),
    /// then "git:https://host"; the port is part of the host only when it is not the scheme's default.
    std::vector<std::wstring> Targets(std::string const& url, std::string const& userNameFromUrl);

    /// The stored secret as UTF-8: the Git Credential Manager stores UTF-8, git's wincred helper UTF-16.
    std::string DecodeSecret(std::span<std::uint8_t const> blob);
}
