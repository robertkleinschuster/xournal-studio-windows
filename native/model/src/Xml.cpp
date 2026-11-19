#include "xournal/model/Xml.h"

#include <algorithm>
#include <stdexcept>

namespace xournal
{
    XmlElement::XmlElement(std::wstring name, std::wstring namespaceUri)
        : name(std::move(name)), namespaceUri(std::move(namespaceUri))
    {
    }

    XmlElement::~XmlElement()
    {
        // Children others still hold (a page's stroke elements) must not point at a parent that is gone.
        for (auto& node : nodes)
        {
            if (node.element)
            {
                node.element->parent = nullptr;
            }
        }
    }

    XmlElementPtr XmlElement::Make(std::wstring name, std::initializer_list<std::pair<std::wstring, std::wstring>> attributes)
    {
        auto element = std::make_shared<XmlElement>(std::move(name));
        for (auto const& [key, value] : attributes)
        {
            element->attributes.push_back({ key, value, {} });
        }
        return element;
    }

    std::wstring const* XmlElement::Attribute(std::wstring_view attributeName) const
    {
        for (auto const& a : attributes)
        {
            if (a.name == attributeName)
            {
                return &a.value;
            }
        }
        return nullptr;
    }

    std::wstring XmlElement::AttributeOr(std::wstring_view attributeName, std::wstring_view fallback) const
    {
        auto value = Attribute(attributeName);
        return value ? *value : std::wstring(fallback);
    }

    void XmlElement::SetAttribute(std::wstring_view attributeName, std::wstring value)
    {
        for (auto& a : attributes)
        {
            if (a.name == attributeName)
            {
                a.value = std::move(value);
                return;
            }
        }
        attributes.push_back({ std::wstring(attributeName), std::move(value), {} });
    }

    void XmlElement::RemoveAttribute(std::wstring_view attributeName)
    {
        std::erase_if(attributes, [&](XmlAttribute const& a) { return a.name == attributeName; });
    }

    void XmlElement::ClearAttributes() { attributes.clear(); }

    void XmlElement::AddAttribute(XmlAttribute attribute) { attributes.push_back(std::move(attribute)); }

    std::vector<XmlElementPtr> XmlElement::Elements() const
    {
        std::vector<XmlElementPtr> result;
        for (auto const& node : nodes)
        {
            if (node.element)
            {
                result.push_back(node.element);
            }
        }
        return result;
    }

    std::vector<XmlElementPtr> XmlElement::Elements(std::wstring_view elementName) const
    {
        std::vector<XmlElementPtr> result;
        for (auto const& node : nodes)
        {
            if (node.element && node.element->name == elementName)
            {
                result.push_back(node.element);
            }
        }
        return result;
    }

    XmlElementPtr XmlElement::Element(std::wstring_view elementName) const
    {
        for (auto const& node : nodes)
        {
            if (node.element && node.element->name == elementName)
            {
                return node.element;
            }
        }
        return nullptr;
    }

    int XmlElement::IndexOf(XmlElement const* child) const
    {
        int index = 0;
        for (auto const& node : nodes)
        {
            if (node.element)
            {
                if (node.element.get() == child)
                {
                    return index;
                }
                ++index;
            }
        }
        return -1;
    }

    void XmlElement::Adopt(XmlElementPtr const& child)
    {
        if (!child || child->parent)
        {
            throw std::logic_error("XmlElement: a child must exist and have no parent");
        }
        child->parent = this;
    }

    void XmlElement::Append(XmlElementPtr child)
    {
        Adopt(child);
        nodes.push_back({ XmlNodeKind::Element, std::move(child), {}, {} });
    }

    void XmlElement::Prepend(XmlElementPtr child)
    {
        Adopt(child);
        nodes.insert(nodes.begin(), { XmlNodeKind::Element, std::move(child), {}, {} });
    }

    void XmlElement::InsertBefore(XmlElement const* next, XmlElementPtr child)
    {
        auto at = std::find_if(nodes.begin(), nodes.end(), [&](XmlNode const& n) { return n.element.get() == next; });
        if (at == nodes.end())
        {
            throw std::logic_error("XmlElement::InsertBefore: not a child");
        }
        Adopt(child);
        nodes.insert(at, { XmlNodeKind::Element, std::move(child), {}, {} });
    }

    void XmlElement::AppendNode(XmlNode node)
    {
        if (node.kind == XmlNodeKind::Element)
        {
            Append(std::move(node.element));
            return;
        }
        bool textual = node.kind == XmlNodeKind::Text || node.kind == XmlNodeKind::Whitespace;
        if (textual && !nodes.empty())
        {
            auto& last = nodes.back();
            if (last.kind == XmlNodeKind::Text || last.kind == XmlNodeKind::Whitespace)
            {
                last.text += node.text;
                if (node.kind == XmlNodeKind::Text)
                {
                    last.kind = XmlNodeKind::Text;
                }
                return;
            }
        }
        nodes.push_back(std::move(node));
    }

    XmlElementPtr XmlElement::Remove()
    {
        if (!parent)
        {
            return nullptr;
        }
        auto& siblings = parent->nodes;
        auto at = std::find_if(siblings.begin(), siblings.end(), [&](XmlNode const& n) { return n.element.get() == this; });
        XmlElementPtr self = at->element;
        at = siblings.erase(at);
        // The text before and after the element becomes one node, as a parser reads the tree without it - so a tree
        // edited in place equals a copy built without the element.
        auto isText = [](XmlNode const& n) { return n.kind == XmlNodeKind::Text || n.kind == XmlNodeKind::Whitespace; };
        if (at != siblings.begin() && at != siblings.end() && isText(*(at - 1)) && isText(*at))
        {
            auto& before = *(at - 1);
            before.text += at->text;
            if (at->kind == XmlNodeKind::Text)
            {
                before.kind = XmlNodeKind::Text;
            }
            siblings.erase(at);
        }
        parent = nullptr;
        return self;
    }

    std::wstring XmlElement::Value() const
    {
        std::wstring value;
        for (auto const& node : nodes)
        {
            switch (node.kind)
            {
            case XmlNodeKind::Element: value += node.element->Value(); break;
            case XmlNodeKind::Text:
            case XmlNodeKind::Whitespace:
            case XmlNodeKind::CData: value += node.text; break;
            default: break;
            }
        }
        return value;
    }

    void XmlElement::SetValue(std::wstring value)
    {
        for (auto& node : nodes)
        {
            if (node.element)
            {
                node.element->parent = nullptr;
            }
        }
        nodes.clear();
        if (!value.empty())
        {
            nodes.push_back({ XmlNodeKind::Text, nullptr, std::move(value), {} });
        }
    }

    XmlElementPtr XmlElement::CloneShallow() const
    {
        auto copy = std::make_shared<XmlElement>(name, namespaceUri);
        copy->attributes = attributes;
        return copy;
    }

    XmlElementPtr XmlElement::Clone() const
    {
        auto copy = CloneShallow();
        for (auto const& node : nodes)
        {
            if (node.element)
            {
                copy->Append(node.element->Clone());
            }
            else
            {
                copy->nodes.push_back(node);
            }
        }
        return copy;
    }

    bool XmlElement::DeepEquals(XmlElement const& other) const
    {
        if (name != other.name || namespaceUri != other.namespaceUri || nodes.size() != other.nodes.size() ||
            attributes.size() != other.attributes.size())
        {
            return false;
        }
        for (size_t i = 0; i < attributes.size(); ++i)
        {
            if (attributes[i].name != other.attributes[i].name || attributes[i].value != other.attributes[i].value)
            {
                return false;
            }
        }
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            auto const& a = nodes[i];
            auto const& b = other.nodes[i];
            if (a.kind != b.kind || a.text != b.text || a.target != b.target)
            {
                return false;
            }
            if (a.element && !a.element->DeepEquals(*b.element))
            {
                return false;
            }
        }
        return true;
    }
}
