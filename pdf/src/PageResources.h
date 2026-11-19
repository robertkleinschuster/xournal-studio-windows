#pragma once

// Internal: a page's resource dictionary (names for the XObjects, fonts and graphics states its content uses) and the
// constant-alpha graphics states shared by all pages.

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <cstdint>
#include <format>
#include <map>
#include <string>

namespace xournal::pdf::detail
{
    class AlphaStates
    {
    public:
        explicit AlphaStates(QPDF& pdf) : pdf(pdf) {}

        QPDFObjectHandle For(std::uint8_t strokeAlpha, std::uint8_t fillAlpha)
        {
            int key = strokeAlpha << 8 | fillAlpha;
            auto it = states.find(key);
            if (it != states.end())
            {
                return it->second;
            }
            auto gs = QPDFObjectHandle::newDictionary();
            gs.replaceKey("/Type", QPDFObjectHandle::newName("/ExtGState"));
            gs.replaceKey("/CA", QPDFObjectHandle::newReal(strokeAlpha / 255.0, 4));
            gs.replaceKey("/ca", QPDFObjectHandle::newReal(fillAlpha / 255.0, 4));
            return states.emplace(key, pdf.makeIndirectObject(gs)).first->second;
        }

    private:
        QPDF& pdf;
        std::map<int, QPDFObjectHandle> states;
    };

    class PageResources
    {
    public:
        std::string XObject(QPDFObjectHandle object) { return Name(xobjects, "Im", object); }
        std::string Font(QPDFObjectHandle object) { return Name(fonts, "F", object); }
        std::string State(QPDFObjectHandle object) { return Name(states, "GS", object); }

        QPDFObjectHandle Dictionary() const
        {
            auto resources = QPDFObjectHandle::newDictionary();
            auto add = [&](char const* key, Names const& names) {
                if (names.byObject.empty()) return;
                auto dict = QPDFObjectHandle::newDictionary();
                for (auto const& [name, object] : names.byName) dict.replaceKey(name, object);
                resources.replaceKey(key, dict);
            };
            add("/XObject", xobjects);
            add("/Font", fonts);
            add("/ExtGState", states);
            return resources;
        }

    private:
        struct Names
        {
            std::map<QPDFObjGen, std::string> byObject;
            std::map<std::string, QPDFObjectHandle> byName;
        };

        static std::string Name(Names& names, char const* prefix, QPDFObjectHandle object)
        {
            auto it = names.byObject.find(object.getObjGen());
            if (it != names.byObject.end())
            {
                return it->second;
            }
            std::string name = std::format("/{}{}", prefix, names.byObject.size());
            names.byObject.emplace(object.getObjGen(), name);
            names.byName.emplace(name, object);
            return name;
        }

        Names xobjects, fonts, states;
    };
}
