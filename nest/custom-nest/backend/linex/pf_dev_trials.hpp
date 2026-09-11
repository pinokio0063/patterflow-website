#pragma once
#include "pf_dev.hpp"
#if PF_DEV_TRIALS

#include "pf_geom.hpp"
#include "pf_json.hpp"
#include <string>
#include <vector>

namespace pf {

struct DevSnapPiece {
    std::string kind, size, label;
    double rot = 0, x = 0, y = 0, w = 0, h = 0;
    bool overlap = false;
    std::vector<Pt> pts;
};

struct DevSnapPage {
    int index = 1, rowFrom = 0, rowTo = 0;
    double height = 0, contentY0 = 0;
    std::vector<DevSnapPiece> bodies, sleeves;
};

struct DevTry {
    double rot = 0, x = 0, y = 0, w = 0, h = 0;
    bool ok = false;
    bool overlay = false;
    std::string reason, label;
    std::vector<Pt> pts;
};

struct DevTrial {
    int i = 0;
    std::string pass, kind, reason, size, label;
    int row = -1;
    bool ok = false;
    bool overlay = false;
    std::vector<DevSnapPage> pages;
    std::vector<DevTry> tries;
};

struct DevLog {
    bool on = false;
    std::string pass = "init";
    int row = -1;
    std::string size, label;
    int failN = 0;
    std::vector<DevTry> pending;
    std::vector<DevTrial> items;

    static const int MAX = 140;
    static const int PENDING_MAX = 80;
    static const int FAIL_STRIDE = 5;

    void reset() {
        on = false;
        pass = "init";
        row = -1;
        size.clear();
        label.clear();
        failN = 0;
        pending.clear();
        items.clear();
    }

    void ctx(const char* p, int r, const std::string& sz, const std::string& lb) {
        if (p) pass = p;
        row = r;
        size = sz;
        label = lb;
        failN = 0;
    }

    void geom(const Poly& piece, double rot, bool ok, const char* reason, bool force) {
        if (!on || pending.size() >= (size_t)PENDING_MAX) return;
        const bool overlay = reason && std::string(reason) == "overlay";
        if (!ok && !force && !overlay) {
            failN += 1;
            if (failN != 1 && (failN % FAIL_STRIDE) != 0) return;
        }
        DevTry t;
        t.rot = rot;
        t.x = piece.bbox.x;
        t.y = piece.bbox.y;
        t.w = piece.bbox.w;
        t.h = piece.bbox.h;
        t.ok = ok;
        t.overlay = overlay;
        t.reason = reason ? reason : (ok ? "ok" : "fail");
        t.label = label;
        const size_t n = piece.points.size();
        if (n && (ok || overlay || force)) {
            const int step = n > 14 ? (int)(n / 14) : 1;
            for (size_t k = 0; k < n; k += (size_t)step) t.pts.push_back(piece.points[k]);
        }
        pending.push_back(t);
    }

    void write(JsonOut& o) const {
        o.key("devTrialsOn"); o.boolean(on); o.comma();
        o.key("devTrialCount"); o.num((double)items.size()); o.comma();
        o.key("devTrials"); o.raw("[");
        for (size_t i = 0; i < items.size(); i++) {
            if (i) o.comma();
            const DevTrial& t = items[i];
            o.raw("{");
            o.key("i"); o.num((double)t.i); o.comma();
            o.key("n"); o.num((double)(t.i + 1)); o.comma();
            o.key("pass"); o.str(t.pass); o.comma();
            o.key("kind"); o.str(t.kind); o.comma();
            o.key("reason"); o.str(t.reason); o.comma();
            o.key("size"); o.str(t.size); o.comma();
            o.key("label"); o.str(t.label); o.comma();
            o.key("row"); o.num((double)t.row); o.comma();
            o.key("ok"); o.boolean(t.ok); o.comma();
            o.key("overlay"); o.boolean(t.overlay); o.comma();
            o.key("pages"); o.raw("[");
            for (size_t p = 0; p < t.pages.size(); p++) {
                if (p) o.comma();
                const DevSnapPage& pg = t.pages[p];
                o.raw("{");
                o.key("index"); o.num((double)pg.index); o.comma();
                o.key("rowFrom"); o.num((double)pg.rowFrom); o.comma();
                o.key("rowTo"); o.num((double)pg.rowTo); o.comma();
                o.key("height"); o.num(pg.height); o.comma();
                o.key("contentY0"); o.num(pg.contentY0); o.comma();
                o.key("bodies"); o.raw("[");
                for (size_t b = 0; b < pg.bodies.size(); b++) {
                    if (b) o.comma();
                    writeSnapPiece(o, pg.bodies[b]);
                }
                o.raw("],");
                o.key("sleeves"); o.raw("[");
                for (size_t s = 0; s < pg.sleeves.size(); s++) {
                    if (s) o.comma();
                    writeSnapPiece(o, pg.sleeves[s]);
                }
                o.raw("]}");
            }
            o.raw("],");
            o.key("tries"); o.raw("[");
            for (size_t k = 0; k < t.tries.size(); k++) {
                if (k) o.comma();
                writeTry(o, t.tries[k]);
            }
            o.raw("]}");
        }
        o.raw("]");
    }

private:
    static void writeSnapPiece(JsonOut& o, const DevSnapPiece& p) {
        o.raw("{");
        o.key("kind"); o.str(p.kind); o.comma();
        o.key("size"); o.str(p.size); o.comma();
        o.key("label"); o.str(p.label); o.comma();
        o.key("rotation"); o.num(p.rot); o.comma();
        o.key("overlap"); o.boolean(p.overlap); o.comma();
        o.key("bbox"); o.raw("{");
        o.key("x"); o.num(p.x); o.comma();
        o.key("y"); o.num(p.y); o.comma();
        o.key("w"); o.num(p.w); o.comma();
        o.key("h"); o.num(p.h);
        o.raw("},");
        o.key("points"); o.raw("[");
        for (size_t k = 0; k < p.pts.size(); k++) {
            if (k) o.comma();
            o.raw("{\"x\":"); o.num(p.pts[k].x); o.raw(",\"y\":"); o.num(p.pts[k].y); o.raw("}");
        }
        o.raw("]}");
    }

    static void writeTry(JsonOut& o, const DevTry& t) {
        o.raw("{");
        o.key("rot"); o.num(t.rot); o.comma();
        o.key("x"); o.num(t.x); o.comma();
        o.key("y"); o.num(t.y); o.comma();
        o.key("w"); o.num(t.w); o.comma();
        o.key("h"); o.num(t.h); o.comma();
        o.key("ok"); o.boolean(t.ok); o.comma();
        o.key("overlay"); o.boolean(t.overlay); o.comma();
        o.key("reason"); o.str(t.reason); o.comma();
        o.key("label"); o.str(t.label);
        if (!t.pts.empty()) {
            o.comma();
            o.key("points"); o.raw("[");
            for (size_t k = 0; k < t.pts.size(); k++) {
                if (k) o.comma();
                o.raw("{\"x\":"); o.num(t.pts[k].x); o.raw(",\"y\":"); o.num(t.pts[k].y); o.raw("}");
            }
            o.raw("]");
        }
        o.raw("}");
    }
};

static DevLog gDev;

void devSnap(const char* kind, const char* reason);

} // namespace pf

#define PF_CTX(pass, row, sz, lb) do { if (::pf::gDev.on) ::pf::gDev.ctx((pass), (row), (sz), (lb)); } while (0)
#define PF_DECIDE(kind, why) do { if (::pf::gDev.on) ::pf::devSnap((kind), (why)); } while (0)
#define PF_GEOM(piece, rot, ok, why, force) do { if (::pf::gDev.on) ::pf::gDev.geom((piece), (rot), (ok), (why), (force)); } while (0)

#else

#define PF_CTX(...)
#define PF_DECIDE(...)
#define PF_GEOM(...)

#endif
