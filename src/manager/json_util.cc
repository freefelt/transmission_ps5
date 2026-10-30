// Transmission for the PS5: small JSON helpers: escaping and the parser of flat JSON objects.
//
// License: GPL-3.0-or-later (like Transmission).

#include "ui_internal.h"

namespace ps5tm_ui
{

// ---------------------------------------------------------------- helpers

static std::string json_escape(std::string const& s)
{
    std::string out;
    for (char const c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
        }
    }
    return out;
}

std::string jstr(std::string const& s)
{
    return "\"" + json_escape(s) + "\"";
}

std::string jbool(bool b)
{
    return b ? "true" : "false";
}

std::string hex8(int value)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(value));
    return buf;
}

// Flat JSON object {"key": value, ...}: values are strings, numbers, true/false. Everything else
// (nested objects and arrays) is treated as an error. Returns false on any inexactness.
bool parse_flat_json(std::string const& text, std::map<std::string, std::string>& out)
{
    std::size_t i = 0;
    auto const skip = [&] {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\n' || text[i] == '\r' || text[i] == '\t'))
        {
            ++i;
        }
    };
    auto const read_string = [&](std::string& s) {
        if (i >= text.size() || text[i] != '"')
        {
            return false;
        }
        ++i;
        s.clear();
        while (i < text.size())
        {
            char const c = text[i++];
            if (c == '"')
            {
                return true;
            }
            if (c != '\\')
            {
                s += c;
                continue;
            }
            if (i >= text.size())
            {
                return false;
            }
            char const e = text[i++];
            switch (e)
            {
            case '"':
            case '\\':
            case '/':
                s += e;
                break;
            case 'n':
                s += '\n';
                break;
            case 't':
                s += '\t';
                break;
            case 'r':
                s += '\r';
                break;
            case 'b':
                s += '\b';
                break;
            case 'f':
                s += '\f';
                break;
            case 'u':
            {
                // \uXXXX (and surrogate pairs) to UTF-8: that is how non-ASCII clients escape them when they do not write Russian letters as is
                auto const hex4 = [&](unsigned& v) {
                    if (i + 4 > text.size())
                    {
                        return false;
                    }
                    v = 0;
                    for (int k = 0; k < 4; ++k)
                    {
                        char const h = text[i + static_cast<std::size_t>(k)];
                        unsigned const d = h >= '0' && h <= '9' ? static_cast<unsigned>(h - '0') : h >= 'a' && h <= 'f' ? static_cast<unsigned>(h - 'a' + 10) : h >= 'A' && h <= 'F' ? static_cast<unsigned>(h - 'A' + 10) : 16U;
                        if (d > 15)
                        {
                            return false;
                        }
                        v = v * 16 + d;
                    }
                    i += 4;
                    return true;
                };
                unsigned cp = 0;
                if (!hex4(cp))
                {
                    return false;
                }
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size() && text[i] == '\\' && text[i + 1] == 'u')
                {
                    std::size_t const save = i;
                    i += 2;
                    unsigned lo = 0;
                    if (hex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF)
                    {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    else
                    {
                        i = save;
                    }
                }
                if (cp < 0x80)
                {
                    s += static_cast<char>(cp);
                }
                else if (cp < 0x800)
                {
                    s += static_cast<char>(0xC0 | (cp >> 6));
                    s += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else if (cp < 0x10000)
                {
                    s += static_cast<char>(0xE0 | (cp >> 12));
                    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    s += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else
                {
                    s += static_cast<char>(0xF0 | (cp >> 18));
                    s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    s += static_cast<char>(0x80 | (cp & 0x3F));
                }
                break;
            }
            default:
                return false;
            }
        }
        return false;
    };

    out.clear();
    skip();
    if (i >= text.size() || text[i] != '{')
    {
        return false;
    }
    ++i;
    skip();
    if (i < text.size() && text[i] == '}')
    {
        return true;
    }
    for (;;)
    {
        std::string key;
        std::string value;
        skip();
        if (!read_string(key))
        {
            return false;
        }
        skip();
        if (i >= text.size() || text[i] != ':')
        {
            return false;
        }
        ++i;
        skip();
        if (i >= text.size())
        {
            return false;
        }
        if (text[i] == '"')
        {
            if (!read_string(value))
            {
                return false;
            }
        }
        else
        {
            std::size_t const start = i;
            while (i < text.size() && text[i] != ',' && text[i] != '}' && text[i] != ' ' && text[i] != '\n' && text[i] != '\r' &&
                   text[i] != '\t')
            {
                ++i;
            }
            value = text.substr(start, i - start);
            if (value.empty() || value == "null" || value[0] == '{' || value[0] == '[')
            {
                return false;
            }
        }
        out[key] = value;
        skip();
        if (i >= text.size())
        {
            return false;
        }
        if (text[i] == ',')
        {
            ++i;
            continue;
        }
        return text[i] == '}';
    }
}

} // namespace ps5tm_ui
