using Claunia.PropertyList;

namespace XournalStudio.Model;

/// <summary>
/// What Apple's markup (PDFKit/AnnotationKit on iPad and Mac) keeps beside a PDF annotation it wrote: the annotation's
/// <c>/AAPL:AKExtras</c> dictionary holds a base64 binary plist (an NSKeyedArchiver archive) under
/// <c>/AAPL:AKAnnotationV2</c> or <c>/AAPL:AKAnnotationObject</c>. Its root object names the annotation's class
/// (<c>AKInkAnnotation2</c>: a PencilKit drawing in <c>drawing</c>, in a coordinate space of <c>drawingSize</c>;
/// <c>AKImageAnnotation</c>, <c>AKTextBoxAnnotation</c>, ...). Not documented by Apple; read with plist-cil.
/// </summary>
public sealed record AppleMarkup(string ClassName, byte[]? Drawing, double DrawingWidth, double DrawingHeight)
{
    public const string InkClass = "AKInkAnnotation2";

    /// <summary>The archive's root object; throws <see cref="FormatException"/> when it is not such an archive.</summary>
    public static AppleMarkup Read(string base64)
    {
        try
        {
            if (PropertyListParser.Parse(Convert.FromBase64String(base64)) is not NSDictionary archive ||
                archive["$objects"] is not NSArray objects || archive["$top"] is not NSDictionary top)
            {
                throw new FormatException("not a keyed archive");
            }
            NSObject? Resolve(NSObject? o) => o is UID uid ? objects[checked((int)uid.ToUInt64())] : o;
            if (Resolve(top.TryGetValue("root", out var r) ? r : null) is not NSDictionary root)
            {
                throw new FormatException("keyed archive without a root object");
            }
            string className = Resolve(root["$class"]) is NSDictionary c && c["$classname"] is NSString name ? name.Content : "";
            byte[]? drawing = Resolve(root.TryGetValue("drawing", out var d) ? d : null) is NSData data ? data.Bytes : null;
            double width = 0, height = 0;
            if (Resolve(root.TryGetValue("drawingSize", out var s) ? s : null) is NSDictionary size &&
                Resolve(size["NS.keys"]) is NSArray keys && Resolve(size["NS.objects"]) is NSArray values)
            {
                for (int i = 0; i < keys.Count && i < values.Count; i++)
                {
                    double value = Resolve(values[i]) is NSNumber n ? n.ToDouble() : 0;
                    switch ((Resolve(keys[i]) as NSString)?.Content)
                    {
                        case "Width":
                            width = value;
                            break;
                        case "Height":
                            height = value;
                            break;
                    }
                }
            }
            return new AppleMarkup(className, drawing, width, height);
        }
        // What plist-cil and the archive's references throw for data that is not such an archive (damaged, cut short, of
        // another layout); a bug here would show as another exception.
        catch (Exception e) when (e is PropertyListFormatException or ArgumentException or IndexOutOfRangeException
            or InvalidCastException or KeyNotFoundException or System.Xml.XmlException or IOException or NotSupportedException
            or OverflowException)
        {
            throw new FormatException($"unreadable markup archive: {e.GetType().Name}: {e.Message}", e);
        }
    }
}
