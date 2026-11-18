using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// The app's set-up of the library's toolbar: the library's German texts and Xournal++'s palette
/// (<see cref="XournalPalette"/>) with the user's tool choices from the settings (pen presets: black, blue, red by
/// default). The window adds the app's own buttons at the end.
/// </summary>
public static class AppToolbar
{
    // The selection's tip also tells what a rectangle on a PDF page does here.
    private static readonly PenToolbarTexts Texts = PenToolbarTexts.German with
    {
        SelectTip = "Auswählen: Lasso oder Rechteck (nochmal tippen zum Wählen); ziehen verschiebt, Ecken skalieren; " +
            "ein Rechteck auf einer PDF-Seite kopiert diesen Bereich. Mit dem Stift geht das Lasso mit jedem Werkzeug: mit gehaltener Stifttaste ziehen",
    };

    public static PenToolbar Create(AppSettings settings) => new(Texts, settings.ToolChoices());
}
