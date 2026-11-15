using System.Text;
using LibGit2Sharp;
using Meziantou.Framework.Win32;

namespace XournalStudio.Services;

/// <summary>
/// Git sign-in for LibGit2Sharp: the credentials Git for Windows keeps in the Windows Credential Manager (the Git
/// Credential Manager and git's wincred helper use the generic credential "git:https://host"), so the sync works
/// wherever git was used before. The app does not sign in itself.
/// </summary>
public static class GitCredentials
{
    public static Credentials Get(string url, string usernameFromUrl, SupportedCredentialTypes types)
    {
        var uri = new Uri(url);
        string host = uri.IsDefaultPort ? uri.Host : $"{uri.Host}:{uri.Port}";
        string[] targets = string.IsNullOrEmpty(usernameFromUrl)
            ? [$"git:{uri.Scheme}://{host}"]
            : [$"git:{uri.Scheme}://{usernameFromUrl}@{host}", $"git:{uri.Scheme}://{host}"];
        foreach (string target in targets)
        {
            if (CredentialManager.ReadCredential(target, CredentialType.Generic) is { } stored)
            {
                return new UsernamePasswordCredentials
                {
                    Username = stored.UserName ?? usernameFromUrl,
                    Password = Decode(stored.Secret.Span),
                };
            }
        }
        throw new InvalidOperationException(
            $"Keine Git-Anmeldung für {uri.Host} gespeichert. Einmal mit Git für Windows anmelden " +
            "(z. B. „git pull“ im Ordner), dann erneut synchronisieren.");
    }

    // The Git Credential Manager stores UTF-8, git's wincred helper UTF-16.
    private static string Decode(ReadOnlySpan<byte> blob)
    {
        bool utf16 = blob.Length >= 2 && blob.Length % 2 == 0;
        for (int i = 1; utf16 && i < blob.Length; i += 2)
        {
            utf16 = blob[i] == 0;
        }
        return utf16 ? Encoding.Unicode.GetString(blob) : Encoding.UTF8.GetString(blob);
    }
}
