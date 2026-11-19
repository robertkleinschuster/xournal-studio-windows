#include "TestSupport.h"

#include "xournal/io/Gzip.h"
#include "xournal/io/XmlText.h"

#include <gtest/gtest.h>

namespace xournal::tests
{
    TEST(Xml, EverythingReadIsWrittenBack)
    {
        auto root = Xml(R"(<?xml version="1.0" standalone="no"?>
<a x="1" b="&lt;&amp;&quot;"><!-- note --><b/>text &amp; more<![CDATA[<raw>]]><?pi data?>
  <c y="2">ü €</c></a>)");
        auto text = io::SerializeXml(*root);
        auto again = io::ParseXml(text);
        EXPECT_TRUE(root->DeepEquals(*again));
        ASSERT_EQ(2u, again->Attributes().size());
        EXPECT_EQ(L"x", again->Attributes()[0].name); // attribute order kept
        EXPECT_EQ(L"<&\"", again->Attributes()[1].value);
        EXPECT_EQ(L"text & more<raw>\n  ü €", again->Value());
        std::string written(text.begin(), text.end());
        EXPECT_TRUE(written.starts_with("<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"no\"?>")) << written;
        EXPECT_NE(std::string::npos, written.find("<b />")) << written; // empty elements stay empty
    }

    TEST(Xml, TextLineEndsAreWrittenAsLineFeeds)
    {
        // Upstream writes a <text>'s lines with LF; XmlLite must not turn them into CR LF or &#xD;.
        auto root = Xml("<text>x</text>");
        root->SetValue(L"a\nb");
        auto text = io::SerializeXml(*root);
        std::string written(text.begin(), text.end());
        EXPECT_NE(std::string::npos, written.find("<text>a\nb</text>")) << written;
        EXPECT_EQ(L"a\nb", io::ParseXml(text)->Value());
        EXPECT_EQ(std::string::npos, written.find('\r')) << written;

        auto layered = Xml("<page>\n  <layer>x &amp; y\nz</layer>\n</page>");
        auto bytes = io::SerializeXml(*layered);
        std::string again(bytes.begin(), bytes.end());
        EXPECT_NE(std::string::npos, again.find("<page>\n  <layer>x &amp; y\nz</layer>\n</page>")) << again;
        EXPECT_TRUE(layered->DeepEquals(*io::ParseXml(bytes)));
    }

    TEST(Xml, DocumentTypesAreRefused)
    {
        EXPECT_ANY_THROW(Xml(R"(<!DOCTYPE a [<!ENTITY e "x">]><a>&e;</a>)"));
        EXPECT_ANY_THROW(Xml("<a><b></a>"));
    }

    TEST(Xml, RemovedElementsKeepTheirIdentity)
    {
        auto layer = Xml("<layer><a/><b/><c/></layer>");
        auto b = layer->Elements()[1];
        EXPECT_EQ(1, layer->IndexOf(b.get()));
        auto removed = b->Remove();
        EXPECT_EQ(b, removed);
        EXPECT_EQ(nullptr, b->Parent());
        layer->InsertBefore(layer->Elements()[1].get(), b);
        EXPECT_EQ(b, layer->Elements()[1]);
        EXPECT_EQ(layer.get(), b->Parent());
    }

    TEST(Gzip, RoundTripsAndRejectsDamage)
    {
        std::string text(100000, 'x');
        std::span<std::uint8_t const> bytes(reinterpret_cast<std::uint8_t const*>(text.data()), text.size());
        auto packed = io::Gzip::Compress(bytes);
        EXPECT_TRUE(io::Gzip::IsGzip(packed));
        auto unpacked = io::Gzip::Decompress(packed);
        EXPECT_EQ(text, std::string(unpacked.begin(), unpacked.end()));
        packed.resize(packed.size() / 2);
        EXPECT_ANY_THROW(io::Gzip::Decompress(packed));
    }
}
