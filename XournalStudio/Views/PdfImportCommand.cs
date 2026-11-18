using Microsoft.UI;
using Microsoft.UI.Xaml.Controls;
using Microsoft.Windows.Storage.Pickers;
using XournalStudio.Services;

namespace XournalStudio.Views;

/// <summary>
/// Datei → PDF mit iPad-Anmerkungen importieren: the user picks an annotated PDF and where the result goes (a .xopp; its
/// PDF is written beside it with the same name); <see cref="PdfMarkupImport"/> writes the PDF without the markup and the
/// markup becomes the document's strokes, texts and pictures (pictures rendered here, by Windows.Data.Pdf, from a
/// temporary PDF that holds only them). The source PDF stays as it is. The new document is opened afterwards (after the
/// usual question when the open one has changes).
/// </summary>
public sealed class PdfImportCommand(WindowId window, DocumentSession session, Dialogs dialogs, Messages messages)
{
    // Pictures (pasted screenshots: about 1.8 px/pt in the user's PDFs) a little sharper than their source.
    private const double PicturePixelsPerPoint = 2;

    public async Task ImportAsync()
    {
        var open = new FileOpenPicker(window);
        open.FileTypeFilter.Add(".pdf");
        string? source = (await open.PickSingleFileAsync())?.Path;
        if (source == null)
        {
            return;
        }
        var save = new FileSavePicker(window) { SuggestedFileName = Path.GetFileNameWithoutExtension(source) };
        if (Path.GetDirectoryName(source) is { } folder && Directory.Exists(folder))
        {
            save.SuggestedFolder = folder;
        }
        save.FileTypeChoices.Add("Xournal-Dokument", [".xopp"]);
        string? xopp = (await save.PickSaveFileAsync())?.Path;
        if (xopp == null)
        {
            return;
        }
        string pdf = Path.ChangeExtension(xopp, ".pdf");
        string? refused = ProjectFiles.SamePath(pdf, source) ? $"Die neue PDF ({Path.GetFileName(pdf)}) würde die importierte ersetzen."
            : ProjectFiles.SamePath(pdf, session.Document.PdfPath) ? $"{Path.GetFileName(pdf)} ist die PDF des offenen Dokuments."
            : ProjectFiles.SamePath(xopp, session.Document.FilePath) ? $"{Path.GetFileName(xopp)} ist das offene Dokument."
            : null;
        if (refused != null)
        {
            messages.Show(InfoBarSeverity.Warning, "Nicht importiert", refused + " Bitte einen anderen Ordner oder Namen wählen.");
            return;
        }
        if (File.Exists(pdf) && !await dialogs.ConfirmAsync("PDF ersetzen?",
            $"{Path.GetFileName(pdf)} gibt es dort schon. Die importierte PDF ohne Anmerkungen ersetzt sie.", "Ersetzen"))
        {
            return;
        }
        string pictures = Path.Combine(Path.GetTempPath(), $"xournal-studio-import-{Guid.NewGuid():N}.pdf");
        try
        {
            var import = await Task.Run(() => PdfMarkupImport.Prepare(source, pdf, pictures));
            if (import.Pictures.Count > 0)
            {
                var rendered = await PdfBackground.LoadAsync(pictures);
                foreach (var picture in import.Pictures)
                {
                    var (png, width, height) = await rendered.RenderAreaAsync(picture.Page, picture.Area, PicturePixelsPerPoint, transparent: true);
                    var image = Model.XoppImage.Create(picture.Area, png, width, height);
                    picture.Element.ReplaceAll(image.Attributes(), image.Nodes());
                }
            }
            var document = PdfMarkupImport.CreateDocument(import, pdf);
            await document.SaveAsync(xopp);
            messages.Show(InfoBarSeverity.Success, "PDF importiert", Summary(import, xopp));
        }
        catch (Exception e)
        {
            messages.Error("import PDF", "Import fehlgeschlagen", e, source);
            return;
        }
        finally
        {
            Delete(pictures);
        }
        if (await session.ConfirmLeaveAsync())
        {
            await session.OpenAsync(xopp);
        }
    }

    private static string Summary(MarkupImport import, string xopp)
    {
        var parts = new List<string>();
        int strokes = import.Elements.Sum(e => e.Count(x => x.Name == "stroke"));
        if (strokes > 0)
        {
            parts.Add($"{strokes} Striche");
        }
        if (import.Texts > 0)
        {
            parts.Add($"{import.Texts} Texte");
        }
        if (import.Pictures.Count > 0)
        {
            parts.Add($"{import.Pictures.Count} Bilder");
        }
        string text = $"{Path.GetFileName(xopp)}: " + (parts.Count > 0 ? string.Join(", ", parts) : "keine Anmerkungen") + ".";
        if (import.Kept > 0)
        {
            text += $" {import.Kept} Anmerkung(en) ohne Darstellung sind in der PDF geblieben.";
        }
        return text;
    }

    private static void Delete(string path)
    {
        try
        {
            File.Delete(path);
            File.Delete(path + "~");
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            DebugLog.Write($"import: temporary file {path}: {e.GetType().Name} 0x{e.HResult:X8} {e.Message}");
        }
    }
}
