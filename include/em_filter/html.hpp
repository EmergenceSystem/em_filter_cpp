#pragma once
#include <string>
#include <vector>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace em {

/** Remove all <script>...</script> blocks from html. */
inline std::string strip_scripts(const std::string& html) {
    static const std::regex pat(
        R"(<script[^>]*>.*?</script>)",
        std::regex::icase | std::regex::optimize);
    return std::regex_replace(html, pat, "");
}

/** Strip all HTML tags, returning plain text. */
inline std::string get_text(const std::string& html) {
    static const std::regex pat(R"(<[^>]+>)");
    return std::regex_replace(html, pat, "");
}

/** Extract inner-HTML of elements matching a simple CSS selector. */
inline std::vector<std::string> extract_elements(const std::string& html,
                                                   const std::string& selector) {
    std::string pat_str;
    if (selector == "li.b_algo")
        pat_str = R"(<li[^>]*class=['"]b_algo['"][^>]*>(.*?)</li>)";
    else if (selector == "div a")
        pat_str = R"(<a[^>]*>(.*?)</a>)";
    else if (selector == "div p")
        pat_str = R"(<p[^>]*>(.*?)</p>)";
    else if (!selector.empty() && selector[0] == '.') {
        std::string cls = std::regex_replace(selector.substr(1),
            std::regex(R"([-[\]{}()*+?.,\\^$|#\s])"), R"(\$&)");
        pat_str = R"(<[^>]*class=['"][^'"]*)" + cls + R"([^'"]*['"][^>]*>(.*?)</[^>]+>)";
    } else if (!selector.empty() && selector[0] == '#') {
        std::string id = selector.substr(1);
        pat_str = R"(<[^>]*id=['"])" + id + R"(['"][^>]*>(.*?)</[^>]+>)";
    } else {
        auto dot = selector.find('.');
        if (dot != std::string::npos) {
            std::string tag = selector.substr(0, dot);
            std::string cls = selector.substr(dot + 1);
            pat_str = "<" + tag + R"([^>]*class=['"][^'"]*)" + cls +
                      R"([^'"]*['"][^>]*>(.*?)</)" + tag + ">";
        } else {
            pat_str = "<" + selector + R"([^>]*>(.*?)</)" + selector + ">";
        }
    }

    std::vector<std::string> results;
    std::regex pat(pat_str,
        std::regex::icase | std::regex::optimize);
    auto begin = std::sregex_iterator(html.begin(), html.end(), pat);
    auto end   = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        results.push_back((*it)[1].str());
    }
    return results;
}

/** Extract the value of an attribute from an HTML element string. */
inline std::optional<std::string> extract_attribute(const std::string& element,
                                                      const std::string& attr) {
    std::regex pat(attr + R"(=['"]([^'"]*)['"]]?)",
        std::regex::icase | std::regex::optimize);
    std::smatch m;
    if (std::regex_search(element, m, pat)) return m[1].str();
    return std::nullopt;
}

/** Decode &#N;, &#xHH;, and &name; HTML entities. */
inline std::string decode_html_entities(const std::string& text) {
    static const std::unordered_map<std::string, std::string> named = {
        {"nbsp",   "\xc2\xa0"}, {"amp",    "&"},    {"lt",    "<"},
        {"gt",     ">"},        {"quot",   "\""},   {"apos",  "'"},
        {"eacute", "\xc3\xa9"}, {"egrave", "\xc3\xa8"}, {"agrave","\xc3\xa0"},
        {"ccedil", "\xc3\xa7"}, {"ocirc",  "\xc3\xb4"}, {"ecirc", "\xc3\xaa"},
        {"icirc",  "\xc3\xae"}, {"ugrave", "\xc3\xb9"}, {"aacute","\xc3\xa1"},
    };

    // Helper: code point -> UTF-8
    auto cp_to_utf8 = [](unsigned int cp) -> std::string {
        std::string s;
        if (cp <= 0x7f) { s += (char)cp; }
        else if (cp <= 0x7ff) {
            s += (char)(0xc0|(cp>>6)); s += (char)(0x80|(cp&0x3f));
        } else if (cp <= 0xffff) {
            s += (char)(0xe0|(cp>>12)); s += (char)(0x80|((cp>>6)&0x3f));
            s += (char)(0x80|(cp&0x3f));
        } else {
            s += (char)(0xf0|(cp>>18)); s += (char)(0x80|((cp>>12)&0x3f));
            s += (char)(0x80|((cp>>6)&0x3f)); s += (char)(0x80|(cp&0x3f));
        }
        return s;
    };

    std::string result;
    result.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '&') {
            auto semi = text.find(';', i);
            if (semi != std::string::npos && semi - i <= 10) {
                std::string inner = text.substr(i + 1, semi - i - 1);
                if (!inner.empty() && inner[0] == '#') {
                    unsigned int cp;
                    if (inner.size() > 1 && (inner[1]=='x'||inner[1]=='X'))
                        cp = (unsigned int)std::stoul(inner.substr(2), nullptr, 16);
                    else
                        cp = (unsigned int)std::stoul(inner.substr(1));
                    result += cp_to_utf8(cp);
                    i = semi + 1;
                    continue;
                }
                auto it = named.find(inner);
                if (it != named.end()) {
                    result += it->second;
                    i = semi + 1;
                    continue;
                }
            }
        }
        result += text[i++];
    }
    return result;
}

/** Return true if the URL should be skipped. */
inline bool should_skip_link(const std::string& url,
                               const std::vector<std::string>& excluded) {
    if (url.substr(0, 4) != "http") return true;
    for (const auto& excl : excluded) {
        if (url.find(excl) != std::string::npos) return true;
    }
    return false;
}

} // namespace em
