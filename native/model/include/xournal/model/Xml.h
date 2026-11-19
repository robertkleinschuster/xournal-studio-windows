#pragma once

// The document's XML as a tree that keeps everything read: elements, attributes in their order, text, CDATA, comments,
// processing instructions and whitespace - so what the app does not understand is written back as it was (rule: never
// delete from files). Reading and writing the text form is the io module's job (XmlLite).
//
// Elements are shared (XmlElementPtr): a page refers to its stroke elements while they are taken out of the tree and put
// back (erase, undo), as a removed picture's place does - an element's identity outlives its place in the tree.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace xournal
{
    class XmlElement;
    using XmlElementPtr = std::shared_ptr<XmlElement>;

    enum class XmlNodeKind { Element, Text, Whitespace, CData, Comment, ProcessingInstruction };

    struct XmlAttribute
    {
        std::wstring name;         // qualified ("prefix:local")
        std::wstring value;
        std::wstring namespaceUri; // empty: none
    };

    struct XmlNode
    {
        XmlNodeKind kind = XmlNodeKind::Text;
        XmlElementPtr element;     // kind Element
        std::wstring text;         // the other kinds: their text (a processing instruction's data)
        std::wstring target;       // processing instruction
    };

    class XmlElement
    {
    public:
        explicit XmlElement(std::wstring name, std::wstring namespaceUri = {});
        ~XmlElement();
        XmlElement(XmlElement const&) = delete;
        XmlElement& operator=(XmlElement const&) = delete;

        /// A new element with these attributes (in this order).
        static XmlElementPtr Make(std::wstring name, std::initializer_list<std::pair<std::wstring, std::wstring>> attributes = {});

        std::wstring const& Name() const { return name; }
        std::wstring const& NamespaceUri() const { return namespaceUri; }

        std::vector<XmlAttribute> const& Attributes() const { return attributes; }
        /// The attribute's value, or null when it is missing.
        std::wstring const* Attribute(std::wstring_view attributeName) const;
        std::wstring AttributeOr(std::wstring_view attributeName, std::wstring_view fallback) const;
        /// Sets an attribute; an existing one keeps its place, a new one goes last.
        void SetAttribute(std::wstring_view attributeName, std::wstring value);
        void RemoveAttribute(std::wstring_view attributeName);
        void ClearAttributes();
        /// Appends an attribute as read (no check for an existing one: the reader gives each once).
        void AddAttribute(XmlAttribute attribute);

        std::vector<XmlNode> const& Nodes() const { return nodes; }
        std::vector<XmlElementPtr> Elements() const;
        std::vector<XmlElementPtr> Elements(std::wstring_view elementName) const;
        /// The first child element of this name, or null.
        XmlElementPtr Element(std::wstring_view elementName) const;
        /// The position among the child elements (not counting other nodes); -1 when it is not a child.
        int IndexOf(XmlElement const* child) const;

        XmlElement* Parent() const { return parent; }

        /// Adds a child (it must have no parent).
        void Append(XmlElementPtr child);
        void Prepend(XmlElementPtr child);
        /// Inserts `child` before the child element `next`.
        void InsertBefore(XmlElement const* next, XmlElementPtr child);
        /// Appends a node that is not an element (text, comment, ...); adjacent text is merged as a parser would.
        void AppendNode(XmlNode node);
        /// Takes this element out of its parent; returns it (the caller keeps it alive). Null when it has no parent.
        XmlElementPtr Remove();

        /// The text of all descendants, as XLinq's XElement.Value (comments and processing instructions not counted).
        std::wstring Value() const;
        /// Replaces the children by one text node.
        void SetValue(std::wstring value);

        /// A deep copy without parent.
        XmlElementPtr Clone() const;
        /// A copy with the attributes, without children.
        XmlElementPtr CloneShallow() const;

        /// Same names, attributes (in order) and nodes, recursively.
        bool DeepEquals(XmlElement const& other) const;

    private:
        void Adopt(XmlElementPtr const& child);

        std::wstring name;
        std::wstring namespaceUri;
        std::vector<XmlAttribute> attributes;
        std::vector<XmlNode> nodes;
        XmlElement* parent = nullptr; // set by the parent that owns this element; cleared when it lets go
    };
}
