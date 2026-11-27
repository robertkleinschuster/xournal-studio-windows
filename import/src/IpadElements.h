#pragma once

// Internal: the elements of a .xopp page that the iPad's markup holds as standard PDF annotations - strokes that PencilKit
// ink cannot hold (highlighters, dashed, filled, square or butt ended, transparent), texts (/FreeText) and pictures
// (/Stamp) - written for the iPad export and read back by the iPad import (user, 2026-11-27: every .xopp function gets an
// iPad counterpart; an export and an import again return the same document, without end).
//
// An annotation shows the element by a standard annotation (what the iPad edits: /Ink with /InkList, /FreeText with
// /Contents, /Stamp), draws it with a normal appearance (the one-page PDF the export draws for the element, as a form
// XObject: the same look as the normal export, fonts embedded) and carries the original element in /XournalStudio:
// its attributes verbatim and its text, the rectangle it was written at. The import takes the element back from there -
// the stored text while the annotation still shows the same, the annotation's own data (points, text, rectangle, color,
// size) where the iPad changed them - so what the iPad did not touch comes back exactly, lap after lap.

#include "xournal/model/Xml.h"
#include "xournal/pdf/DocumentExport.h"
#include "xournal/pdf/PageFrame.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <memory>
#include <string>
#include <vector>

namespace xournal::import::detail
{
    std::string Num(double v);
    std::string PdfDate();

    /// The one-page PDFs the appearances are copied from: qpdf reads their streams only when `pdf` is written, so they
    /// must live until then (declare it before the QPDF that is written to).
    using ForeignKeep = std::vector<std::shared_ptr<QPDF>>;

    /// Adds the annotation of `element` to `page` (/Annots); `origin` is the element as the .xopp has it, `pageHeight` the page's
    /// (y flips: the page's top is its height in PDF space).
    void AddElementAnnotation(QPDF& pdf, ForeignKeep& keep, QPDFObjectHandle page, double pageHeight, pdf::PageElement const& element,
                              XmlElementPtr const& origin, pdf::ExportOptions const& options);

    /// The element an annotation written by AddElementAnnotation holds (nothing for any other annotation).
    XmlElementPtr ReadElementAnnotation(QPDFObjectHandle annotation, pdf::PageFrame const& frame);
}
