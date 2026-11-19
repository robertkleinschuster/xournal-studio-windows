#pragma once

// One page: its <page> element with everything in it. Strokes are bound to the ink layer while the page has a view
// (PageInk); pictures are listed and changed in place (PageImages); texts and strokes the ink cannot draw are shown from
// the element. Everything else stays in the element and is written back as loaded.

#include "xournal/model/Background.h"
#include "xournal/model/PageImages.h"
#include "xournal/model/PageInk.h"
#include "xournal/model/Xml.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace xournal
{
    class XoppPage
    {
    public:
        /// Takes the element over; adds a plain background and a layer when the file has none (as upstream reads it).
        explicit XoppPage(XmlElementPtr element);
        XoppPage(XoppPage const&) = delete;
        XoppPage& operator=(XoppPage const&) = delete;

        static std::shared_ptr<XoppPage> FromXml(XmlElementPtr element);
        static std::shared_ptr<XoppPage> CreateGraph(double width, double height);
        static std::shared_ptr<XoppPage> CreatePdf(double width, double height, int pdfPageIndex);

        double Width() const { return width; }
        double Height() const { return height; }

        XmlElement& Background() const;
        bool IsPdf() const { return Background::IsPdf(Background()); }
        int PdfPageIndex() const { return Background::PdfPageIndex(Background()); }
        Color BackgroundColor() const { return Background::ColorOf(Background()); }
        BackgroundStyle Style() const { return Background::StyleOf(Background()); }
        /// The background's "config" attribute (ruling parameters, see BackgroundRuling); null: none.
        std::wstring const* BackgroundConfig() const { return Background().Attribute(L"config"); }
        bool IsImageBackground() const { return Background::IsPixmap(Background()); }

        /// The file a picture background names, resolved when the document was loaded (it may be missing).
        std::optional<std::filesystem::path> const& BackgroundImagePath() const { return backgroundImagePath; }
        void SetBackgroundImagePath(std::optional<std::filesystem::path> path) { backgroundImagePath = std::move(path); }
        /// The page whose picture background this page's "clone" background shows (empty: not a clone).
        std::shared_ptr<XoppPage> BackgroundCloneSource() const { return backgroundCloneSource.lock(); }
        void SetBackgroundCloneSource(std::shared_ptr<XoppPage> const& page) { backgroundCloneSource = page; }

        PageInk& Ink() { return ink; }
        PageInk const& Ink() const { return ink; }
        PageImages& Images() { return images; }
        PageImages const& Images() const { return images; }

        /// The page's layers, bottom to top (their elements in drawing order, as upstream draws them).
        std::vector<XmlElementPtr> Layers() const { return element->Elements(L"layer"); }

        /// Texts and links of all layers, bottom to top (shown, not edited).
        std::vector<XmlElementPtr> Texts() const;
        /// Strokes shown from the file, not edited (Strokes::NeedsOwnDrawing).
        std::vector<XmlElementPtr> const& DrawnStrokes() const;

        /// Whether anything lies on the page: from the ink while it has a view, otherwise from the element - without
        /// turning strokes into ink.
        bool HasContent() const;

        /// The lowest edge of the page's content (strokes, pictures, drawn strokes, estimated texts); 0 if empty.
        double ContentBottom() const;

        /// Adds elements to the top layer of a page without view (building a document: the iPad import).
        void AddToTopLayer(std::vector<XmlElementPtr> const& elements);

        /// The page as it is now, as a detached copy (see PageInk::SnapshotOf; without view: the element).
        XmlElementPtr Snapshot();

    private:
        XmlElementPtr element;
        double width;
        double height;
        PageInk ink;
        PageImages images;
        std::optional<std::filesystem::path> backgroundImagePath;
        std::weak_ptr<XoppPage> backgroundCloneSource;
        // Found once (the page view draws them once too): nothing edits these elements - they are not ink - and strokes
        // added later are ink.
        mutable std::optional<std::vector<XmlElementPtr>> drawnStrokes;
    };
}
