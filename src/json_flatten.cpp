#include "json_flatten.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include <sstream>

namespace {

class Parser {
public:
    Parser(const std::string &s, std::map<std::string, std::string> *out)
        : s_(s), pos_(0), out_(out) {}

    bool parse(std::string *error) {
        skip_ws();
        if (!value("", error)) return false;
        skip_ws();
        if (pos_ != s_.size()) return fail("trailing data", error);
        return true;
    }

private:
    const std::string &s_;
    size_t pos_;
    std::map<std::string, std::string> *out_;

    bool fail(const char *msg, std::string *error) {
        if (error) {
            std::ostringstream os;
            os << msg << " at byte " << pos_;
            *error = os.str();
        }
        return false;
    }

    void skip_ws() {
        while (pos_ < s_.size() && isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }

    bool string_value(std::string *out, std::string *error) {
        if (pos_ >= s_.size() || s_[pos_] != '"') return fail("expected string", error);
        ++pos_;
        std::string v;
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') {
                *out = v;
                return true;
            }
            if (c != '\\') {
                v.push_back(c);
                continue;
            }
            if (pos_ >= s_.size()) return fail("truncated escape", error);
            char e = s_[pos_++];
            switch (e) {
                case '"': v.push_back('"'); break;
                case '\\': v.push_back('\\'); break;
                case '/': v.push_back('/'); break;
                case 'b': v.push_back('\b'); break;
                case 'f': v.push_back('\f'); break;
                case 'n': v.push_back('\n'); break;
                case 'r': v.push_back('\r'); break;
                case 't': v.push_back('\t'); break;
                case 'u': {
                    if (pos_ + 4 > s_.size()) return fail("truncated unicode escape", error);
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = s_[pos_++];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code += h - '0';
                        else if (h >= 'a' && h <= 'f') code += h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') code += h - 'A' + 10;
                        else return fail("invalid unicode escape", error);
                    }
                    if (code < 0x80) v.push_back(static_cast<char>(code));
                    else if (code < 0x800) {
                        v.push_back(static_cast<char>(0xc0 | (code >> 6)));
                        v.push_back(static_cast<char>(0x80 | (code & 0x3f)));
                    } else {
                        v.push_back(static_cast<char>(0xe0 | (code >> 12)));
                        v.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
                        v.push_back(static_cast<char>(0x80 | (code & 0x3f)));
                    }
                    break;
                }
                default: return fail("invalid escape", error);
            }
        }
        return fail("unterminated string", error);
    }

    bool object(const std::string &path, std::string *error) {
        ++pos_;
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            if (!path.empty()) (*out_)[path] = "{}";
            return true;
        }
        while (pos_ < s_.size()) {
            std::string key;
            if (!string_value(&key, error)) return false;
            skip_ws();
            if (pos_ >= s_.size() || s_[pos_] != ':') return fail("expected colon", error);
            ++pos_;
            skip_ws();
            std::string child = path.empty() ? key : path + "." + key;
            if (!value(child, error)) return false;
            skip_ws();
            if (pos_ >= s_.size()) return fail("unterminated object", error);
            if (s_[pos_] == '}') {
                ++pos_;
                return true;
            }
            if (s_[pos_] != ',') return fail("expected comma", error);
            ++pos_;
            skip_ws();
        }
        return fail("unterminated object", error);
    }

    bool array(const std::string &path, std::string *error) {
        size_t start = pos_;
        ++pos_;
        skip_ws();
        size_t index = 0;
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            if (!path.empty()) (*out_)[path] = "[]";
            return true;
        }
        while (pos_ < s_.size()) {
            std::ostringstream child;
            child << path << '[' << index << ']';
            if (!value(child.str(), error)) return false;
            ++index;
            skip_ws();
            if (pos_ >= s_.size()) return fail("unterminated array", error);
            if (s_[pos_] == ']') {
                ++pos_;
                if (!path.empty()) (*out_)[path] = s_.substr(start, pos_ - start);
                return true;
            }
            if (s_[pos_] != ',') return fail("expected comma", error);
            ++pos_;
            skip_ws();
        }
        return fail("unterminated array", error);
    }

    bool primitive(const std::string &path, std::string *error) {
        size_t start = pos_;
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == ',' || c == '}' || c == ']' || isspace(static_cast<unsigned char>(c))) break;
            ++pos_;
        }
        if (pos_ == start) return fail("expected value", error);
        std::string v = s_.substr(start, pos_ - start);
        if (v != "true" && v != "false" && v != "null") {
            char *end = 0;
            strtod(v.c_str(), &end);
            if (!end || *end != '\0') return fail("invalid primitive", error);
        }
        if (!path.empty()) (*out_)[path] = v;
        return true;
    }

    bool value(const std::string &path, std::string *error) {
        skip_ws();
        if (pos_ >= s_.size()) return fail("expected value", error);
        if (s_[pos_] == '{') return object(path, error);
        if (s_[pos_] == '[') return array(path, error);
        if (s_[pos_] == '"') {
            std::string v;
            if (!string_value(&v, error)) return false;
            if (!path.empty()) (*out_)[path] = v;
            return true;
        }
        return primitive(path, error);
    }
};

static std::string lower_copy(const std::string &s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) out.push_back(static_cast<char>(tolower(static_cast<unsigned char>(s[i]))));
    return out;
}

}  // namespace

bool extract_json_object(const std::string &text, std::string *json) {
    if (!json) return false;
    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    size_t start = std::string::npos;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (in_string) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') {
            in_string = true;
            continue;
        }
        if (c == '{') {
            if (depth == 0) start = i;
            ++depth;
        } else if (c == '}' && depth > 0) {
            --depth;
            if (depth == 0 && start != std::string::npos) {
                *json = text.substr(start, i - start + 1);
                return true;
            }
        }
    }
    return false;
}

bool flatten_json(const std::string &json, std::map<std::string, std::string> *out,
                  std::string *error) {
    if (!out) return false;
    out->clear();
    Parser p(json, out);
    return p.parse(error);
}

bool json_key_is_sensitive(const std::string &path) {
    std::string p = lower_copy(path);
    static const char *needles[] = {
        "password", "passwd", "secret", "credential", "assigned_cred", "token", "private_key"
    };
    for (size_t i = 0; i < sizeof(needles) / sizeof(needles[0]); ++i) {
        if (p.find(needles[i]) != std::string::npos) return true;
    }
    return false;
}

std::string topic_component(const std::string &path) {
    std::string out;
    for (size_t i = 0; i < path.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(path[i]);
        if (isalnum(c) || c == '_' || c == '-' || c == '.') out.push_back(static_cast<char>(c));
        else if (c == '[') out.push_back('_');
        else if (c == ']') continue;
        else out.push_back('_');
    }
    return out;
}

std::string json_escape(const std::string &value) {
    std::ostringstream os;
    for (size_t i = 0; i < value.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(value[i]);
        switch (c) {
            case '"': os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\b': os << "\\b"; break;
            case '\f': os << "\\f"; break;
            case '\n': os << "\\n"; break;
            case '\r': os << "\\r"; break;
            case '\t': os << "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    os << buf;
                } else os << static_cast<char>(c);
        }
    }
    return os.str();
}
