#ifndef JSON_HPP
#define JSON_HPP

#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace Json {

inline std::string escape(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if ('\x00' <= c && c <= '\x1f') {
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

inline std::string stringValue(const std::string& str) {
    return "\"" + escape(str) + "\"";
}

inline std::string boolValue(bool val) {
    return val ? "true" : "false";
}

inline std::string numberValue(long long val) {
    return std::to_string(val);
}

inline std::string numberValue(int val) {
    return std::to_string(val);
}

inline std::string numberValue(double val, int precision = 2) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << val;
    return ss.str();
}

inline std::string getStringField(const std::string& json, const std::string& key, const std::string& defaultVal = "") {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return defaultVal;

    pos += needle.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) pos++;
    
    if (pos >= json.length()) return defaultVal;

    if (json[pos] == '"') {
        pos++;
        size_t endQuote = json.find('"', pos);
        if (endQuote != std::string::npos) {
            return json.substr(pos, endQuote - pos);
        }
    }
    return defaultVal;
}

inline bool getBoolField(const std::string& json, const std::string& key, bool defaultVal = false) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return defaultVal;

    pos += needle.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) pos++;

    if (pos + 4 <= json.length() && json.substr(pos, 4) == "true") return true;
    if (pos + 5 <= json.length() && json.substr(pos, 5) == "false") return false;
    return defaultVal;
}

inline int getIntField(const std::string& json, const std::string& key, int defaultVal = 0) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return defaultVal;

    pos += needle.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) pos++;

    try {
        size_t endPos = pos;
        while (endPos < json.length() && ((json[endPos] >= '0' && json[endPos] <= '9') || json[endPos] == '-')) endPos++;
        if (endPos > pos) {
            std::string sub = json.substr(pos, endPos - pos);
            char* endPtr = nullptr;
            long val = std::strtol(sub.c_str(), &endPtr, 10);
            if (endPtr != sub.c_str()) {
                return static_cast<int>(val);
            }
        }
    } catch (...) {}
    return defaultVal;
}

inline double getDoubleField(const std::string& json, const std::string& key, double defaultVal = 0.0) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return defaultVal;

    pos += needle.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) pos++;

    try {
        size_t endPos = pos;
        while (endPos < json.length() && ((json[endPos] >= '0' && json[endPos] <= '9') || json[endPos] == '-' || json[endPos] == '.')) endPos++;
        if (endPos > pos) {
            std::string sub = json.substr(pos, endPos - pos);
            char* endPtr = nullptr;
            double val = std::strtod(sub.c_str(), &endPtr);
            if (endPtr != sub.c_str()) {
                return val;
            }
        }
    } catch (...) {}
    return defaultVal;
}

inline std::vector<std::string> splitJsonObjects(const std::string& arrayJson) {
    std::vector<std::string> objs;
    bool inQuote = false;
    bool escape = false;
    int braceDepth = 0;
    size_t objStart = 0;

    for (size_t i = 0; i < arrayJson.length(); ++i) {
        char c = arrayJson[i];
        if (inQuote) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                inQuote = false;
            }
        } else {
            if (c == '"') {
                inQuote = true;
            } else if (c == '{') {
                if (braceDepth == 0) {
                    objStart = i;
                }
                braceDepth++;
            } else if (c == '}') {
                braceDepth--;
                if (braceDepth == 0) {
                    objs.push_back(arrayJson.substr(objStart, i - objStart + 1));
                }
            }
        }
    }
    return objs;
}

inline std::string getRowLabel(int r) {
    std::string label = "";
    r++;
    while (r > 0) {
        int rem = (r - 1) % 26;
        label = static_cast<char>('A' + rem) + label;
        r = (r - 1) / 26;
    }
    return label;
}

} // namespace Json

#endif // JSON_HPP
