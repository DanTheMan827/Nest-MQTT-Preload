#include "json_flatten.h"

#include <ctype.h>

#include <sstream>

#include <picojson.h>

namespace {

static std::string lower_copy(const std::string &s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
        out.push_back(static_cast<char>(tolower(static_cast<unsigned char>(s[i]))));
    return out;
}

static void flatten_value(const picojson::value &value, const std::string &path,
                          std::map<std::string, std::string> *out) {
    if (value.is<picojson::object>()) {
        const picojson::object &object = value.get<picojson::object>();
        if (object.empty() && !path.empty()) (*out)[path] = "{}";
        for (picojson::object::const_iterator it = object.begin(); it != object.end(); ++it) {
            const std::string child = path.empty() ? it->first : path + "." + it->first;
            flatten_value(it->second, child, out);
        }
        return;
    }

    if (value.is<picojson::array>()) {
        const picojson::array &array = value.get<picojson::array>();
        if (!path.empty()) (*out)[path] = value.serialize();
        for (size_t i = 0; i < array.size(); ++i) {
            std::ostringstream child;
            child << path << '[' << i << ']';
            flatten_value(array[i], child.str(), out);
        }
        return;
    }

    if (path.empty()) return;
    if (value.is<std::string>()) (*out)[path] = value.get<std::string>();
    else (*out)[path] = value.serialize();
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

    picojson::value root;
    const std::string parse_error = picojson::parse(root, json);
    if (!parse_error.empty()) {
        if (error) *error = parse_error;
        return false;
    }

    flatten_value(root, "", out);
    if (error) error->clear();
    return true;
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
    const std::string serialized = picojson::value(value).serialize();
    if (serialized.size() >= 2 && serialized[0] == '"' &&
        serialized[serialized.size() - 1] == '"') {
        return serialized.substr(1, serialized.size() - 2);
    }
    return serialized;
}
