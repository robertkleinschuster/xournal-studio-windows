using PdfSharp.Pdf;
using PdfSharp.Pdf.IO;

namespace XournalStudio.Services;

/// <summary>
/// Inserting pages of another PDF the way upstream Xournal++ stores a document: it has one background PDF, and pages
/// refer to its pages by number. So the other PDF's pages are appended to the end of the document's PDF (appending: the
/// numbers of the pages already there stay – another document may use the same PDF), and the document's new pages refer
/// to them. What the other PDF's annotations show (e.g. signatures added on the iPad) is drawn into those pages first
/// (<see cref="PdfAnnotationFlattener"/>), since a page's annotations do not come along as page content.
/// </summary>
public static class PdfPageAppender
{
    /// <summary>
    /// Appends <paramref name="pages"/> (0-based, in this order) of <paramref name="source"/> to <paramref name="target"/>;
    /// returns the index of the first appended page. The source stays as it is. Throws <see cref="PdfSharp.PdfSharpException"/>
    /// when PDFsharp cannot change the target (e.g. a permissions password).
    /// </summary>
    public static int Append(string target, string source, IReadOnlyList<int> pages)
    {
        using var flattened = PdfAnnotationFlattener.Flatten(source);
        using var from = flattened != null
            ? PdfReader.Open(flattened, PdfDocumentOpenMode.Import)
            : PdfReader.Open(source, PdfDocumentOpenMode.Import);
        using var to = PdfReader.Open(target, PdfDocumentOpenMode.Modify);
        int first = to.PageCount;
        foreach (int index in pages)
        {
            var added = to.AddPage(from.Pages[index]);
            if (flattened != null)
            {
                added.Elements.Remove("/Annots"); // drawn into the page: shown once
            }
        }
        string temp = target + "~";
        to.Save(temp);
        File.Move(temp, target, overwrite: true);
        return first;
    }
}
