using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace XournalStudio.Views;

/// <summary>
/// The selection's commands (Ausschneiden, Kopieren, Duplizieren, Löschen) in the command row: always in their place
/// (nothing jumps, the row keeps its length), enabled while something is selected – the content commands only when
/// there is content (a PDF area alone can only be copied). In the row and not in a flyout at the selection: a flyout
/// over the page is a windowed popup (drawn above the ink), and while it is open the release of a barrel drag never
/// reaches the app – the selection could not be moved with the barrel (sandbox 2026-11-16, see CLAUDE.md). Nothing
/// here lies over the page.
/// </summary>
/// <param name="focusDocument">Where the keyboard focus goes when a focused button is disabled (WinUI would hand it to
/// the next control – maybe the tasks box, where Del and Ctrl+A edit text).</param>
public sealed class SelectionCommands(Control copy, IReadOnlyList<Control> contentCommands, Action focusDocument)
{
    public void ShowFor(Selection? selection)
    {
        bool hadFocus = copy.FocusState != FocusState.Unfocused ||
            contentCommands.Any(c => c.FocusState != FocusState.Unfocused);
        copy.IsEnabled = selection != null;
        foreach (var command in contentCommands)
        {
            command.IsEnabled = selection?.HasContent == true;
        }
        if (hadFocus && (!copy.IsEnabled || contentCommands.Any(c => !c.IsEnabled && c.FocusState != FocusState.Unfocused)))
        {
            focusDocument();
        }
    }
}
