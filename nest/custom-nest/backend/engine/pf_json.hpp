#pragma once
#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace pf {

struct Json {
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::map<std::string, Json> o;

    static Json nul() { return Json(); }
    static Json boolean(bool v) { Json j; j.type = BOOL; j.b = v; return j; }
    static Json num(double v) { Json j; j.type = NUM; j.n = v; return j; }
    static Json str(const std::string& v) { Json j; j.type = STR; j.s = v; return j; }
    static Json arr() { Json j; j.type = ARR; return j; }
    static Json obj() { Json j; j.type = OBJ; return j; }

    bool isNull() const { return type == NUL; }
    bool isObj() const { return type == OBJ; }
    bool isArr() const { return type == ARR; }
    bool has(const std::string& k) const { return type == OBJ && o.count(k) != 0; }
    const Json& get(const std::string& k) const {
        static Json empty;
        auto it = o.find(k);
        return it == o.end() ? empty : it->second;
    }
    double number(const std::string& k, double def = 0) const {
        if (!has(k)) return def;
        const Json& v = o.at(k);
        if (v.type == NUM) return v.n;
        if (v.type == STR) {
            try { return std::stod(v.s); } catch (...) { return def; }
        }
        return def;
    }
    std::string strVal(const std::string& k, const std::string& def = "") const {
        if (!has(k)) return def;
        const Json& v = o.at(k);
        if (v.type == STR) return v.s;
        if (v.type == NUM) {
            std::ostringstream os;
            os << v.n;
            return os.str();
        }
        return def;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& src) : t(src), i(0) {}

    Json parse() {
        skip();
        Json v = value();
        skip();
        return v;
    }

private:
    const std::string& t;
    size_t i;

    void skip() {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) i++;
    }
    char peek() { skip(); return i < t.size() ? t[i] : 0; }
    char take() { skip(); return i < t.size() ? t[i++] : 0; }

    Json value() {
        char c = peek();
        if (c == '{') return object();
        if (c == '[') return array();
        if (c == '"') return Json::str(string());
        if (c == 't' || c == 'f') return boolean();
        if (c == 'n') { null(); return Json::nul(); }
        return number();
    }

    Json object() {
        take();
        Json j = Json::obj();
        skip();
        if (peek() == '}') { take(); return j; }
        while (true) {
            std::string k = string();
            if (take() != ':') throw std::runtime_error("JSON expected ':'");
            j.o[k] = value();
            skip();
            char c = take();
            if (c == '}') break;
            if (c != ',') throw std::runtime_error("JSON expected ',' or '}'");
        }
        return j;
    }

    Json array() {
        take();
        Json j = Json::arr();
        skip();
        if (peek() == ']') { take(); return j; }
        while (true) {
            j.a.push_back(value());
            skip();
            char c = take();
            if (c == ']') break;
            if (c != ',') throw std::runtime_error("JSON expected ',' or ']'");
        }
        return j;
    }

    std::string string() {
        if (take() != '"') throw std::runtime_error("JSON expected string");
        std::string out;
        while (i < t.size()) {
            char c = t[i++];
            if (c == '"') return out;
            if (c == '\\' && i < t.size()) {
                char e = t[i++];
                if (e == '"' || e == '\\' || e == '/') out.push_back(e);
                else if (e == 'n') out.push_back('\n');
                else if (e == 'r') out.push_back('\r');
                else if (e == 't') out.push_back('\t');
                else if (e == 'u' && i + 3 < t.size()) {
                    i += 4;
                    out.push_back('?');
                } else out.push_back(e);
            } else out.push_back(c);
        }
        throw std::runtime_error("JSON unterminated string");
    }

    Json number() {
        skip();
        size_t start = i;
        if (i < t.size() && (t[i] == '-' || t[i] == '+')) i++;
        while (i < t.size() && ((t[i] >= '0' && t[i] <= '9') || t[i] == '.' || t[i] == 'e' || t[i] == 'E' || t[i] == '+' || t[i] == '-')) {
            if (t[i] == '+' || t[i] == '-') {
                if (t[i - 1] != 'e' && t[i - 1] != 'E') break;
            }
            i++;
        }
        return Json::num(std::stod(t.substr(start, i - start)));
    }

    Json boolean() {
        if (t.compare(i, 4, "true") == 0) { i += 4; return Json::boolean(true); }
        if (t.compare(i, 5, "false") == 0) { i += 5; return Json::boolean(false); }
        throw std::runtime_error("JSON bad bool");
    }

    void null() {
        if (t.compare(i, 4, "null") == 0) { i += 4; return; }
        throw std::runtime_error("JSON bad null");
    }
};

inline Json parseJson(const std::string& src) {
    return JsonParser(src).parse();
}

inline std::string jsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else o.push_back(c);
    }
    return o;
}

struct JsonOut {
    std::ostringstream os;
    void raw(const std::string& s) { os << s; }
    void comma() { os << ','; }
    void key(const char* k) { os << '"' << k << "\":"; }
    void str(const std::string& s) { os << '"' << jsonEscape(s) << '"'; }
    void num(double v) {
        if (!std::isfinite(v)) { os << "0"; return; }
        os.setf(std::ios::fixed);
        os.precision(6);
        os << v;
    }
    void boolean(bool v) { os << (v ? "true" : "false"); }
    void nul() { os << "null"; }
    std::string str() const { return os.str(); }
};

} // namespace pf
