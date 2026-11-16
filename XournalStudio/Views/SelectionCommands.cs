using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace XournalStudio.Views;

/// <summary>
/// The selection's commands (Ausschneiden, Kopieren, Duplizieren, Löschen) as contextual buttons in the command row:
/// shown while something is selected, the content commands only when there is content (a PDF area alone can only be
/// copied). In the row and not in a flyout at the selection: a flyout over the page is a windowed popup (drawn above
/// the ink), and while it is open the release of a barrel drag never reaches the app – the selection could not be moved
/// with the barrel (sandbox 2026-11-16, see CLAUDE.md). Nothing here lies over the page.
/// </summary>
public sealed class SelectionCommands(UIElement panel, IReadOnlyList<UIElement> contentCommands)
{
    public void ShowFor(Selection? selection)
    {
        panel.Visibility = selection != null ? Visibility.Visible : Visibility.Collapsed;
        foreach (var command in contentCommands)
        {
            command.Visibility = selection?.HasContent == true ? Visibility.Visible : Visibility.Collapsed;
        }
    }
}
