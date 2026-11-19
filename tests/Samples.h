#pragma once

// The sample document of XournalStudio.Tests/FileFormatTests.cs.

namespace xournal::tests
{
    inline constexpr char const* Sample = R"(<?xml version="1.0" standalone="no"?>
<xournal creator="xournalpp 1.3.8+dev" fileversion="5" fork-export="x_kommentiert.pdf">
<title>Xournal++ document - see https://xournalpp.github.io/</title>
<page width="595.27559" height="841.88976">
<background type="pdf" domain="absolute" filename="lecture.pdf" pageno="1"/>
<layer>
<stroke tool="pen" ts="0" fn="" color="#3333ccff" width="1.41 1.1 1.2" capStyle="round">10 20 30 40 50 60</stroke>
<text font="Sans" size="12" x="10" y="10" color="#000000ff">Hallo</text>
</layer>
</page>
<page width="595.27559" height="841.88976">
<background type="pdf" pageno="2"/>
<layer name="Ebene 2"><stroke tool="highlighter" color="#ffff007f" width="8.5">1 2 3 4</stroke></layer>
</page>
<page width="595.27559" height="841.88976">
<background type="solid" color="#ffffffff" style="graph"/>
<layer/>
</page>
</xournal>)";
}
