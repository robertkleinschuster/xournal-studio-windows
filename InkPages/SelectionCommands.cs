using Microsoft.UI.Xaml.Controls;

namespace InkPages;

/// <summary>
/// The selection's commands (cut, copy, duplicate, delete) in the toolbar row: always in their place (nothing jumps,
/// the row keeps its length), enabled while something is selected – the content commands only when there is content
/// (a kept area alone can only be copied). In the row and not in a flyout at the selection: a flyout over the page is
/// a windowed popup (drawn above the ink), and while it is open the release of a barrel drag never reaches the app –
/// the selection could not be moved with the barrel (sandbox 2026-11-16). Nothing here lies over the page.
/// </summary>
/// (The buttons take no focus – AllowFocusOnInteraction="False", IsTabStop="False" – so disabling one never moves it.)
public sealed class SelectionCommands(Control copy, IReadOnlyList<Control> contentCommands)
{
    public void ShowFor(Selection? selection)
    {
        copy.IsEnabled = selection != null;
        foreach (var command in contentCommands)
        {
            command.IsEnabled = selection?.HasContent == true;
        }
    }
}
