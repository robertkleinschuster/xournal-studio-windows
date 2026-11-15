using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace XournalStudio.Views;

/// <summary>The app's ContentDialogs: confirmations and name input, all shown through <see cref="ShowAsync"/>.</summary>
public sealed class Dialogs(Func<XamlRoot> root, Func<IEnumerable<PageView>> pages)
{
    /// <summary>Raised before a dialog opens (flyouts at the page close: they would stay above the dialog).</summary>
    public event EventHandler? Opening;

    // WinUI shows one ContentDialog at a time (a second ShowAsync throws): later ones wait for the open one.
    private readonly SemaphoreSlim oneAtATime = new(1, 1);

    /// <summary>
    /// Shows a dialog with the pages' ink hidden. The experimental InkCanvas (2.6.6-experimental) renders and takes
    /// pointer input above XAML popups, so mouse and pen clicks on dialog buttons went to the ink
    /// (microsoft-ui-xaml #10461/#11928). Remove the hiding when the Windows App SDK fixes it.
    /// </summary>
    public async Task<ContentDialogResult> ShowAsync(ContentDialog dialog)
    {
        await oneAtATime.WaitAsync();
        try
        {
            return await ShowNowAsync(dialog);
        }
        finally
        {
            oneAtATime.Release();
        }
    }

    private async Task<ContentDialogResult> ShowNowAsync(ContentDialog dialog)
    {
        Opening?.Invoke(this, EventArgs.Empty);
        dialog.XamlRoot = root();
        var views = pages().ToList();
        foreach (var view in views)
        {
            view.InkCanvas.Visibility = Visibility.Collapsed;
        }
        try
        {
            return await dialog.ShowAsync();
        }
        finally
        {
            foreach (var view in views)
            {
                view.InkCanvas.Visibility = Visibility.Visible;
            }
        }
    }

    /// <summary>A question whose safe answer (Abbrechen) is the default. True: the user chose <paramref name="action"/>.</summary>
    public async Task<bool> ConfirmAsync(string title, string text, string action) =>
        await ShowAsync(new ContentDialog
        {
            Title = title,
            Content = Text(text),
            PrimaryButtonText = action,
            CloseButtonText = "Abbrechen",
            DefaultButton = ContentDialogButton.Close,
        }) == ContentDialogResult.Primary;

    /// <summary>Asks for a name; the button is enabled only while <paramref name="validate"/> finds no problem.</summary>
    public async Task<string?> AskNameAsync(string title, string initial, string primary, Func<string, string?> validate)
    {
        var box = new TextBox { Text = initial, Header = "Name" };
        var dialog = new ContentDialog
        {
            Title = title,
            Content = box,
            PrimaryButtonText = primary,
            CloseButtonText = "Abbrechen",
            DefaultButton = ContentDialogButton.Primary,
        };
        void Validate()
        {
            string? error = validate(box.Text);
            box.Description = error;
            dialog.IsPrimaryButtonEnabled = error == null;
        }
        box.TextChanged += (_, _) => Validate();
        dialog.Opened += (_, _) =>
        {
            box.Focus(FocusState.Programmatic);
            box.SelectAll();
        };
        Validate();
        return await ShowAsync(dialog) == ContentDialogResult.Primary ? box.Text : null;
    }

    public static TextBlock Text(string text) => new() { Text = text, TextWrapping = TextWrapping.Wrap };
}
