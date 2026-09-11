#define _CRT_SECURE_NO_WARNINGS
#include "pf_json.hpp"
#include "pf_geom.hpp"
#include "pf_dev_trials.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace pf {

static const char* SIZE_ORDER[] = {
    "2Y", "4Y", "6Y", "8Y", "10Y", "12Y", "XS", "S", "M", "L", "XL", "2XL", "3XL", "4XL", "5XL"
};
static const int SIZE_ORDER_N = 15;

struct Outline {
    std::vector<Pt> points;
    BBox bbox;
};

struct Piece : Poly {
    double rotation = 0;
    bool flipX = false;
    std::string kind, size, label, pairId, role;
    int copyIndex = 0;
    bool overlap = false;
    int sleeveBlock = 0;
    int specIndex = -1;
    bool armPair = false;
    bool midX = false; /* sat at the 2-body gap X midpoint */
    bool freezeTop = false; /* DOC-first mid-X: top-align pin — later rows must not climb it */
    bool isFull = false;
    Outline srcOutline;
};

struct BodyItem {
    Outline outline;
    bool flip = false;
    std::string kind, size, label, pairId, role;
    int copyIndex = 0;
    double w = 0;
};

struct SleeveSpec {
    Outline outline;
    bool flip = false;
    bool isFull = false;
    std::string size, label;
    int copyIndex = 0;
};

struct RemSleeve {
    int specIndex;
};

struct JobRow {
    std::string NAME, NUMBER, SIZE, SLV, COMMENTS;
    int jobIndex = -1;
    std::map<std::string, std::string> extra; /* CUST-* and other passthrough keys */
};

struct Group {
    std::string SIZE;
    std::vector<JobRow> copies;
    double width = 0, height = 0;
};

struct BodyRow {
    std::vector<Piece> pieces;
    double y0 = 0, y1 = 0;
    double startRot = 0;
    bool splitPin = false;
    bool pinFlipped = false; /* Body-1 was 180→0 to seat the top HAF */
    bool fullGap = false;    /* Rule 2: FULL sat at mid-X (not HAF top+bottom) */
    bool hafCenter = false;  /* top HAF sat at 2-body X-mid — stack down, no mid-pocket */
    bool layoutPin = false;  /* DIA leftover ~0, or 2-body + 3 seated sleeves — do not shift */
    int hafCap = -1;         /* sizeIndex of the seated top HAF */
};

struct Page {
    int index = 0, rowFrom = 0, rowTo = 0;
    double y0 = 0, y1 = 0, contentY0 = 0, contentY1 = 0, height = 0, width = 0;
    std::vector<Piece> bodies, sleeves;
};

struct SimResult {
    bool ok = false;
    std::string error;
    double dia = 63, minGap = 0.05;
    int rowsPerDoc = 4;
    std::string sleeveKey;
    std::vector<JobRow> jobRows;
    int skippedFull = 0, skipped = 0;
    int bodyRowCount = 0, overflowSleeves = 0, overlapCount = 0;
    std::vector<std::string> warnings;
    double fabricInches = 0, fabricMeters = 0, elapsedMs = 0;
    std::vector<Page> pages;
};

#if PF_DEV_TRIALS
static const std::vector<BodyRow>* gSnapRows = nullptr;
static const std::vector<Piece>* gSnapSleeves = nullptr;
static int gSnapRpd = 4;
static double gSnapGap = 0.05;
static int gOverlaySnaps = 0;

static void devBind(const std::vector<BodyRow>* rows, const std::vector<Piece>* sleeves, int rpd, double gap) {
    gSnapRows = rows;
    gSnapSleeves = sleeves;
    if (rpd > 0) gSnapRpd = rpd;
    if (gap >= 0) gSnapGap = gap;
}
#endif

static int gRowsPerDoc = 4;

/** First body row of each DOC (rowIndex % rowsPerDoc == 0): mid HAF/FULL top-align.
 * Previous DOC blocking that top → drop this whole row and retry; sleeve does not walk down. */
static bool docFirstBodyRow(size_t rowIndex) {
    const int n = std::max(1, gRowsPerDoc);
    return ((int)rowIndex % n) == 0;
}

/** Y floor for later-row FULL climb: this DOC's first body row (not into the previous DOC). */
static double docFirstRowY0(size_t rowIndex, const std::vector<BodyRow>& bodyRows) {
    const int n = std::max(1, gRowsPerDoc);
    const size_t i = (size_t)(((int)rowIndex / n) * n);
    if (i < bodyRows.size()) return bodyRows[i].y0;
    return 0;
}

static int sizeIndex(const std::string& size) {
    for (int i = 0; i < SIZE_ORDER_N; i++) {
        if (size == SIZE_ORDER[i]) return i;
    }
    return 999;
}

static std::string normalizeSize(std::string raw) {
    if (raw.empty()) return "";
    for (char& c : raw) {
        if (c >= 'a' && c <= 'z') c = char(c - 32);
    }
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t')) raw.pop_back();
    size_t p = 0;
    while (p < raw.size() && (raw[p] == ' ' || raw[p] == '\t')) p++;
    raw = raw.substr(p);
    auto repl = [&](const char* a, const char* b) {
        std::string from = a, to = b;
        size_t at = 0;
        while ((at = raw.find(from, at)) != std::string::npos) {
            raw.replace(at, from.size(), to);
            at += to.size();
        }
    };
    repl("XXXXXL", "5XL");
    repl("XXXXL", "4XL");
    repl("XXXL", "3XL");
    repl("XXL", "2XL");
    return raw;
}

static std::string normalizeSlv(std::string raw) {
    if (raw.empty()) return "";
    for (char& c : raw) {
        if (c >= 'a' && c <= 'z') c = char(c - 32);
    }
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t')) raw.pop_back();
    if (!raw.empty() && raw[0] == 'H') return "HAF";
    if (!raw.empty() && raw[0] == 'F') return "FULL";
    return raw;
}

static void partDim(const Json& chart, const std::string& size, const std::string& key, double& w, double& h) {
    w = 0; h = 0;
    if (!chart.has(size)) return;
    const Json& row = chart.get(size);
    if (!row.has(key)) return;
    const Json& part = row.get(key);
    w = part.number("width", 0);
    h = part.number("height", 0);
    if (!(w > 0 && std::isfinite(w))) w = 0;
    if (!(h > 0 && std::isfinite(h))) h = 0;
}

static Outline gradeOutline(const Outline& master, double targetW, double targetH) {
    Outline out;
    const BBox& bb = master.bbox;
    if (bb.w <= 1e-12 || bb.h <= 1e-12) return out;
    double sx = targetW / bb.w, sy = targetH / bb.h;
    out.points.resize(master.points.size());
    for (size_t i = 0; i < master.points.size(); i++) {
        out.points[i].x = (master.points[i].x - bb.x) * sx;
        out.points[i].y = (master.points[i].y - bb.y) * sy;
    }
    out.bbox.x = 0; out.bbox.y = 0; out.bbox.w = targetW; out.bbox.h = targetH;
    out.bbox.r = targetW; out.bbox.b = targetH;
    return out;
}

static Outline readOutline(const Json& j) {
    Outline ol;
    if (!j.has("points") || !j.get("points").isArr()) return ol;
    const auto& pts = j.get("points").a;
    ol.points.reserve(pts.size());
    for (size_t i = 0; i < pts.size(); i++) {
        Pt p;
        p.x = pts[i].number("x", 0);
        p.y = pts[i].number("y", 0);
        ol.points.push_back(p);
    }
    ol.bbox = bboxOf(ol.points);
    return ol;
}

static Piece placePiece(const Outline& outline, double x, double y, double rot, bool flip) {
    Poly g = placePoly(outline.points, x, y, rot, flip);
    Piece p;
    p.points = std::move(g.points);
    p.bbox = g.bbox;
    p.rotation = rot;
    p.flipX = flip;
    return p;
}

static void movePiece(Piece& piece, double dx, double dy) {
    Poly g = translatePoly(piece.points, dx, dy);
    piece.points = std::move(g.points);
    piece.bbox = g.bbox;
}

static bool insideDia(const Poly& piece, double dia) {
    return piece.bbox.x >= -0.02 && piece.bbox.r <= dia + 0.02;
}

static std::vector<const Poly*> refs(const std::vector<Piece>& a) {
    std::vector<const Poly*> o;
    o.reserve(a.size());
    for (size_t i = 0; i < a.size(); i++) o.push_back(&a[i]);
    return o;
}

static std::vector<const Poly*> refs2(const std::vector<Piece>& a, const std::vector<Piece>& b) {
    std::vector<const Poly*> o;
    o.reserve(a.size() + b.size());
    for (size_t i = 0; i < a.size(); i++) o.push_back(&a[i]);
    for (size_t i = 0; i < b.size(); i++) o.push_back(&b[i]);
    return o;
}

static bool canSit(const Poly& piece, const std::vector<const Poly*>& obstacles, double minGap, double dia) {
    if (!insideDia(piece, dia)) return false;
    return !violatesGap(piece, obstacles, minGap);
}

/** Box-only sit — no polygon test. Used to seed body X before ±2 in true-shape. */
static bool canSitBbox(const Poly& piece, const std::vector<const Poly*>& obstacles, double minGap, double dia) {
    if (!insideDia(piece, dia)) return false;
    for (size_t i = 0; i < obstacles.size(); i++) {
        if (!obstacles[i]) continue;
        if (aabbGap(piece.bbox, obstacles[i]->bbox) < minGap - 1e-6) return false;
    }
    return true;
}

#if PF_DEV_TRIALS
static const char* sitReason(const Poly& piece, const std::vector<const Poly*>& obstacles, double minGap, double dia) {
    if (!insideDia(piece, dia)) return "dia";
    if (!violatesGap(piece, obstacles, minGap)) return "ok";
    for (size_t i = 0; i < obstacles.size(); i++) {
        if (obstacles[i] && polygonsIntersect(piece.points, obstacles[i]->points)) return "overlay";
    }
    return "gap";
}
#endif

static void attach(Piece& nest, const BodyItem& spec) {
    nest.kind = spec.kind;
    nest.size = spec.size;
    nest.copyIndex = spec.copyIndex;
    nest.label = spec.label;
    nest.pairId = spec.pairId;
    nest.role = spec.role;
    nest.srcOutline = spec.outline;
    nest.flipX = spec.flip;
}

static bool nestLeft(const Outline& outline, double y, double rot, bool flip,
                     double xStart, const std::vector<const Poly*>& obstacles,
                     double minGap, double dia, double xFloor, double xStep, bool tightFloor,
                     Piece& out) {
    if (xStep < 0.04) xStep = 0.28;
    double maxX = dia - outline.bbox.w;
    if (maxX < 0) maxX = 0;
    if (xFloor > maxX) xFloor = tightFloor ? maxX : 0;
    if (xStart < xFloor) xStart = xFloor;
    if (xStart > maxX) xStart = maxX;

    Piece trial = placePiece(outline, xStart, y, rot, flip);
    if (!canSit(trial, obstacles, minGap, dia)) {
#if PF_DEV_TRIALS
        if (gDev.on) {
            const char* why = sitReason(trial, obstacles, minGap, dia);
            PF_GEOM(trial, rot, false, why, why[0] == 'o');
        }
#endif
        bool found = false;
        for (double x = maxX; x >= xFloor - 1e-6; x -= xStep) {
            Piece t = placePiece(outline, x, y, rot, flip);
            if (canSit(t, obstacles, minGap, dia)) { trial = std::move(t); found = true; break; }
#if PF_DEV_TRIALS
            else if (gDev.on) {
                const char* why = sitReason(t, obstacles, minGap, dia);
                PF_GEOM(t, rot, false, why, why[0] == 'o');
            }
#endif
        }
        if (!found) return false;
    }

    double lo = xFloor, hi = trial.bbox.x;
    Piece best = trial;
    for (int i = 0; i < 14; i++) {
        double mid = (lo + hi) * 0.5;
        Piece t2 = placePiece(outline, mid, y, rot, flip);
        if (canSit(t2, obstacles, minGap, dia)) {
            best = std::move(t2);
            hi = mid;
        } else {
            lo = mid;
        }
    }
    out = std::move(best);
#if PF_DEV_TRIALS
    PF_GEOM(out, rot, true, "ok", true);
#endif
    return true;
}

static void pressRowUp(std::vector<Piece>& pieces, const std::vector<Piece>& obstacles, double minGap) {
    if (pieces.empty()) return;
    double minY = pieces[0].bbox.y;
    for (size_t i = 1; i < pieces.size(); i++) if (pieces[i].bbox.y < minY) minY = pieces[i].bbox.y;
    double lo = 0, hi = minY, bestDy = 0;
    for (int i = 0; i < 12; i++) {
        double mid = (lo + hi) * 0.5;
        double dy = mid - minY;
        bool ok = true;
        auto obs = refs(obstacles);
        for (size_t j = 0; j < pieces.size(); j++) {
            Poly moved = translatePoly(pieces[j].points, 0, dy);
            if (moved.bbox.y < -0.02) { ok = false; break; }
            if (violatesGap(moved, obs, minGap)) { ok = false; break; }
        }
        if (ok) { bestDy = dy; hi = mid; }
        else lo = mid;
    }
    if (std::fabs(bestDy) > 1e-4) {
        for (size_t i = 0; i < pieces.size(); i++) movePiece(pieces[i], 0, bestDy);
    }
}

/** Rigid-body lift: every piece in the row moves the same dy until true minGap hits. */
static void pressRigidRowUp(std::vector<Piece>& row, const std::vector<Piece>& obstacles, double minGap) {
    if (row.empty()) return;
    double minY = row[0].bbox.y;
    double rowH = row[0].bbox.h;
    for (size_t i = 1; i < row.size(); i++) {
        if (row[i].bbox.y < minY) minY = row[i].bbox.y;
        if (row[i].bbox.h > rowH) rowH = row[i].bbox.h;
    }
    /* Search a full sleeve height up so a staggered next row can seat into cuff valleys. */
    double lo = std::max(0.0, minY - std::max(8.0, rowH));
    double hi = minY, bestDy = 0;
    auto obs = refs(obstacles);
    for (int i = 0; i < 22; i++) {
        double mid = (lo + hi) * 0.5;
        double dy = mid - minY;
        bool ok = true;
        for (size_t j = 0; j < row.size(); j++) {
            Poly moved = translatePoly(row[j].points, 0, dy);
            if (moved.bbox.y < -0.02) { ok = false; break; }
            if (violatesGap(moved, obs, minGap)) { ok = false; break; }
        }
        if (ok) { bestDy = dy; hi = mid; }
        else lo = mid;
    }
    if (std::fabs(bestDy) > 1e-4) {
        for (size_t i = 0; i < row.size(); i++) movePiece(row[i], 0, bestDy);
    }
}

static void bottomAlign(std::vector<Piece>& pieces) {
    if (pieces.empty()) return;
    double hem = pieces[0].bbox.b;
    for (size_t i = 1; i < pieces.size(); i++) if (pieces[i].bbox.b > hem) hem = pieces[i].bbox.b;
    for (size_t i = 0; i < pieces.size(); i++) {
        double dy = hem - pieces[i].bbox.b;
        if (std::fabs(dy) > 1e-6) movePiece(pieces[i], 0, dy);
    }
}

static void topAlign(std::vector<Piece>& pieces) {
    if (pieces.empty()) return;
    double top = pieces[0].bbox.y;
    for (size_t i = 1; i < pieces.size(); i++) if (pieces[i].bbox.y < top) top = pieces[i].bbox.y;
    for (size_t i = 0; i < pieces.size(); i++) {
        const double dy = top - pieces[i].bbox.y;
        if (std::fabs(dy) > 1e-6) movePiece(pieces[i], 0, dy);
    }
}

/** Zipper FULL row: 1-based odd (1,3,…) share one top; even (2,4,…) share one hem. */
static void zipperAlignFullRow(std::vector<Piece>& row) {
    if (row.size() < 2) return;
    double top = 1e300, hem = -1e300;
    int nUp = 0, nDn = 0;
    for (size_t i = 0; i < row.size(); i++) {
        if ((i % 2) == 0) {
            nUp += 1;
            if (row[i].bbox.y < top) top = row[i].bbox.y;
        } else {
            nDn += 1;
            if (row[i].bbox.b > hem) hem = row[i].bbox.b;
        }
    }
    if (nUp >= 2) {
        for (size_t i = 0; i < row.size(); i += 2) {
            const double dy = top - row[i].bbox.y;
            if (std::fabs(dy) > 1e-6) movePiece(row[i], 0, dy);
        }
    }
    if (nDn >= 2) {
        for (size_t i = 1; i < row.size(); i += 2) {
            const double dy = hem - row[i].bbox.b;
            if (std::fabs(dy) > 1e-6) movePiece(row[i], 0, dy);
        }
    }
}

/**
 * Zipper drop is discrete (12–60% height), so even sleeves can overshoot.
 * Group Y-minus only (same dy) so odd top-align and even hem-align stay.
 * Horizontal slack is closed separately — every neighbor true-gap X.
 */
static void minusEvenSleevesToTrueGap(std::vector<Piece>& row, const std::vector<Piece>& bodies,
                                      const std::vector<Piece>& placed, double minGap) {
    if (row.size() < 2) return;
    std::vector<size_t> evens;
    for (size_t i = 1; i < row.size(); i += 2) evens.push_back(i);
    if (evens.empty()) return;
    double minY = row[evens[0]].bbox.y;
    for (size_t k = 1; k < evens.size(); k++) {
        if (row[evens[k]].bbox.y < minY) minY = row[evens[k]].bbox.y;
    }
    if (minY <= 0.02) return;
    auto above = refs2(bodies, placed);
    double lo = 0, hi = minY, bestDy = 0;
    for (int it = 0; it < 22; it++) {
        const double mid = (lo + hi) * 0.5;
        const double dy = mid - minY;
        bool ok = true;
        for (size_t k = 0; k < evens.size() && ok; k++) {
            Poly moved = translatePoly(row[evens[k]].points, 0, dy);
            if (moved.bbox.y < -0.02) { ok = false; break; }
            std::vector<const Poly*> blk = above;
            for (size_t j = 0; j < row.size(); j++) {
                if (j == evens[k] || (j % 2) == 1) continue;
                blk.push_back(&row[j]);
            }
            if (violatesGap(moved, blk, minGap)) ok = false;
        }
        if (ok) { bestDy = dy; hi = mid; }
        else lo = mid;
    }
    if (std::fabs(bestDy) > 1e-4) {
        for (size_t k = 0; k < evens.size(); k++) movePiece(row[evens[k]], 0, bestDy);
    }
}

/** 2+ bodies in a row: everyone top-aligns, then the row presses up. */
static void alignBodyRow(std::vector<Piece>& pieces) {
    if (pieces.size() < 2) return;
    std::sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) { return a.bbox.x < b.bbox.x; });
    topAlign(pieces);
}

/** True-shape too close (same test as the red overlay mark). */
static bool pairTooClose(const Piece& a, const Piece& b, double minGap) {
    if (!aabbNear(a.bbox, b.bbox, minGap + 0.6)) return false;
    Poly A, B;
    A.points = a.points; A.bbox = a.bbox;
    B.points = b.points; B.bbox = b.bbox;
    return closerThan(A, B, minGap - 1e-4);
}

/** Red-mark overlay: true-shape gap 0 / intersection. These pieces must never pin. */
static bool piecesOverlay(const Piece& a, const Piece& b) {
    if (!aabbNear(a.bbox, b.bbox, 0.04)) return false;
    Poly A, B;
    A.points = a.points; A.bbox = a.bbox;
    B.points = b.points; B.bbox = b.bbox;
    return closerThan(A, B, 1e-4);
}

static bool bodiesOverlay(const std::vector<Piece>& row) {
    for (size_t i = 0; i < row.size(); i++) {
        for (size_t j = i + 1; j < row.size(); j++) {
            if (piecesOverlay(row[i], row[j])) return true;
        }
    }
    return false;
}

static bool rowHasOverlay(const BodyRow& row, const std::vector<Piece>& placed) {
    if (bodiesOverlay(row.pieces)) return true;
    std::vector<const Piece*> sl;
    for (size_t i = 0; i < placed.size(); i++) {
        const Piece& s = placed[i];
        const double cy = (s.bbox.y + s.bbox.b) * 0.5;
        if (cy < row.y0 - 0.25 || cy > row.y1 + 0.25) continue;
        sl.push_back(&s);
        for (size_t b = 0; b < row.pieces.size(); b++) {
            if (piecesOverlay(s, row.pieces[b])) return true;
        }
    }
    for (size_t i = 0; i < sl.size(); i++) {
        for (size_t j = i + 1; j < sl.size(); j++) {
            if (piecesOverlay(*sl[i], *sl[j])) return true;
        }
    }
    return false;
}

static void unpinOverlayRows(std::vector<BodyRow>& rows, const std::vector<Piece>& placed) {
    for (size_t r = 0; r < rows.size(); r++) {
        if (rows[r].layoutPin && rowHasOverlay(rows[r], placed))
            rows[r].layoutPin = false;
    }
}

/**
 * After bottom-align, a shorter body can drift into its neighbor while leftover DIA sits unused.
 * Nudge only the overlapping piece right — smallest dx that restores minGap. Never open a big gap.
 */
static void nudgeRowRightToClear(std::vector<Piece>& row, const std::vector<Piece>& obstacles,
                                 double minGap, double dia) {
    if (row.size() < 2) return;
    std::sort(row.begin(), row.end(), [](const Piece& a, const Piece& b) { return a.bbox.x < b.bbox.x; });
    for (size_t i = 1; i < row.size(); i++) {
        if (!pairTooClose(row[i - 1], row[i], minGap)) continue;
        double right = row[0].bbox.r;
        for (size_t k = 1; k < row.size(); k++) if (row[k].bbox.r > right) right = row[k].bbox.r;
        const double slack = dia - right;
        if (slack < 0.02) continue;
        double lo = 0, hi = slack, best = 0;
        for (int it = 0; it < 18; it++) {
            const double mid = (lo + hi) * 0.5;
            Piece t = row[i];
            movePiece(t, mid, 0);
            if (!insideDia(t, dia)) { hi = mid; continue; }
            std::vector<const Poly*> blk = refs(obstacles);
            for (size_t j = 0; j < row.size(); j++) {
                if (j != i) blk.push_back(&row[j]);
            }
            if (violatesGap(t, blk, minGap) || pairTooClose(row[i - 1], t, minGap)) lo = mid;
            else { best = mid; hi = mid; }
        }
        if (best > 1e-4) movePiece(row[i], best, 0);
    }
}

static bool rowNeighborOverlay(const std::vector<Piece>& row, double minGap) {
    for (size_t i = 1; i < row.size(); i++) {
        if (pairTooClose(row[i - 1], row[i], minGap)) return true;
    }
    return false;
}

static BodyItem itemFromPiece(const Piece& p) {
    BodyItem b;
    b.outline = p.srcOutline;
    b.flip = p.flipX;
    b.kind = p.kind;
    b.size = p.size;
    b.label = p.label;
    b.pairId = p.pairId;
    b.role = p.role;
    b.copyIndex = p.copyIndex;
    b.w = p.srcOutline.bbox.w > 0 ? p.srcOutline.bbox.w : p.bbox.w;
    return b;
}

static double rowY0(const std::vector<Piece>& pieces) {
    double m = 1e300;
    for (size_t i = 0; i < pieces.size(); i++) if (pieces[i].bbox.y < m) m = pieces[i].bbox.y;
    return m;
}

static double rowY1(const std::vector<Piece>& pieces) {
    double m = -1e300;
    for (size_t i = 0; i < pieces.size(); i++) if (pieces[i].bbox.b > m) m = pieces[i].bbox.b;
    return m;
}

/** Hem of the body row that contains this Y. Side leftover must finish above that hem. */
static double bodyRowBottomAt(const std::vector<BodyRow>& rows, double y) {
    if (rows.empty()) return 1e300;
    for (size_t i = 0; i < rows.size(); i++) {
        if (y + 0.2 >= rows[i].y0 && y <= rows[i].y1 + 0.2) return rows[i].y1;
    }
    double best = rows[0].y1, bestD = 1e300;
    for (size_t i = 0; i < rows.size(); i++) {
        const double mid = (rows[i].y0 + rows[i].y1) * 0.5;
        const double d = std::fabs(y - mid);
        if (d < bestD) { bestD = d; best = rows[i].y1; }
    }
    return best;
}

static bool sideSleeveFitsRow(const Piece& sl, const std::vector<BodyRow>& rows) {
    if (rows.empty()) return true;
    return sl.bbox.b <= bodyRowBottomAt(rows, sl.bbox.y) + 0.04;
}

/**
 * Anti-logic: if another body already sits on this body's left (same Y band),
 * do not try a sleeve there — that is one-body-worth of overlay scans.
 * Center 2+2 / placket tries are a different recipe and do not use this.
 */
static bool bodyOnLeft(const Piece& body, const std::vector<Piece>& bodies) {
    const double y0 = body.bbox.y + 0.15;
    const double y1 = body.bbox.b - 0.15;
    for (size_t i = 0; i < bodies.size(); i++) {
        const Piece& o = bodies[i];
        if (!body.pairId.empty() && o.pairId == body.pairId && o.role == body.role) continue;
        if (o.bbox.b <= y0 || o.bbox.y >= y1) continue;
        if (o.bbox.x < body.bbox.x - 0.05 && o.bbox.r <= body.bbox.x + 0.8) return true;
    }
    return false;
}

/**
 * Body place: AABB seed + ±2 in true-shape (fast path).
 * If the box leftover is too tight (21+21+21 on 62.85) or the ±2 window
 * misses the hem/side curve, fall back to nestLeft — same true-shape sit
 * sleeves use. Size order is unchanged (widest first).
 */
static const double BODY_TRUE_SLACK = 2.0;

static bool nestBodyBoxThenTrue(const Outline& outline, double y, double rot, bool flip,
                                double xStart, const std::vector<const Poly*>& obstacles,
                                double minGap, double dia, Piece& out) {
    double maxX = dia - outline.bbox.w;
    if (maxX < 0) maxX = 0;
    if (xStart < 0) xStart = 0;
    const bool boxRoom = (xStart <= maxX + 1e-6);

    if (boxRoom) {
        Piece seed = placePiece(outline, xStart, y, rot, flip);
        bool boxOk = canSitBbox(seed, obstacles, minGap, dia);
        if (!boxOk) {
            for (double x = xStart + 0.5; x <= maxX + 1e-6; x += 0.5) {
                Piece t = placePiece(outline, x, y, rot, flip);
                if (!canSitBbox(t, obstacles, minGap, dia)) continue;
                seed = std::move(t);
                boxOk = true;
                break;
            }
        }
        if (boxOk) {
            const double winLo = std::max(0.0, seed.bbox.x - BODY_TRUE_SLACK);
            const double winHi = std::min(maxX, seed.bbox.x + BODY_TRUE_SLACK);

            Piece trial = placePiece(outline, seed.bbox.x, y, rot, flip);
            bool found = canSit(trial, obstacles, minGap, dia);
            if (!found) {
                for (double x = seed.bbox.x; x >= winLo - 1e-6; x -= 0.28) {
                    Piece t = placePiece(outline, x, y, rot, flip);
                    if (canSit(t, obstacles, minGap, dia)) { trial = std::move(t); found = true; break; }
#if PF_DEV_TRIALS
                    else if (gDev.on) {
                        const char* why = sitReason(t, obstacles, minGap, dia);
                        PF_GEOM(t, rot, false, why, why[0] == 'o');
                    }
#endif
                }
            }
            if (!found) {
                for (double x = seed.bbox.x + 0.28; x <= winHi + 1e-6; x += 0.28) {
                    Piece t = placePiece(outline, x, y, rot, flip);
                    if (canSit(t, obstacles, minGap, dia)) { trial = std::move(t); found = true; break; }
#if PF_DEV_TRIALS
                    else if (gDev.on) {
                        const char* why = sitReason(t, obstacles, minGap, dia);
                        PF_GEOM(t, rot, false, why, why[0] == 'o');
                    }
#endif
                }
            }
            if (found) {
                double lo = winLo, hi = trial.bbox.x;
                Piece best = trial;
                for (int i = 0; i < 12; i++) {
                    const double mid = (lo + hi) * 0.5;
                    Piece t2 = placePiece(outline, mid, y, rot, flip);
                    if (canSit(t2, obstacles, minGap, dia)) {
                        best = std::move(t2);
                        hi = mid;
                    } else {
                        lo = mid;
                    }
                }
                out = std::move(best);
#if PF_DEV_TRIALS
                PF_GEOM(out, rot, true, "ok", true);
#endif
                return true;
            }
        }
    }

    /* Extra body on the right: allow AABB overlap, nest into the side/hem curve. */
    return nestLeft(outline, y, rot, flip, boxRoom ? xStart : maxX,
                    obstacles, minGap, dia, 0, 0.28, false, out);
}

static bool tryAddBody(const BodyItem& spec, std::vector<Piece>& row, double y,
                       const std::vector<Piece>& obstacles, double minGap, double dia, double rot) {
    double xStart = row.empty() ? 0 : row.back().bbox.r + minGap;
    auto obs = refs2(obstacles, row);
    Piece nest;
    if (!nestBodyBoxThenTrue(spec.outline, y, rot, spec.flip, xStart, obs, minGap, dia, nest))
        return false;
    attach(nest, spec);
    nest.rotation = rot;
    row.push_back(std::move(nest));
    return true;
}

static int findPair(const std::vector<BodyItem>& remaining, const BodyItem& spec) {
    for (size_t i = 0; i < remaining.size(); i++) {
        if (remaining[i].pairId == spec.pairId && remaining[i].role != spec.role) return (int)i;
    }
    return -1;
}

static double flipRot(double deg) { return deg == 0 ? 180 : 0; }

struct PackedBodies {
    std::vector<BodyRow> rows;
    std::vector<Piece> obstacles;
    double y = 0;
};

/** 2-body row: both 0°, left pin / right pin to DIA — opens a center armhole pocket. */
static bool restyleTwoBodySplit(std::vector<Piece>& row, double y,
                                const std::vector<Piece>& obstacles, double minGap, double dia) {
    if (row.size() != 2) return false;
    if (row[0].srcOutline.points.empty() || row[1].srcOutline.points.empty()) return false;
    std::sort(row.begin(), row.end(), [](const Piece& a, const Piece& b) { return a.bbox.x < b.bbox.x; });
    auto obs = refs(obstacles);

    Piece left = placePiece(row[0].srcOutline, 0, y, 0, row[0].flipX);
    if (!canSit(left, obs, minGap, dia)) return false;
    left.kind = row[0].kind; left.size = row[0].size; left.label = row[0].label;
    left.pairId = row[0].pairId; left.role = row[0].role; left.copyIndex = row[0].copyIndex;
    left.srcOutline = row[0].srcOutline; left.flipX = row[0].flipX; left.rotation = 0;

    double rx = dia - row[1].srcOutline.bbox.w;
    if (rx < 0) rx = 0;
    Piece right = placePiece(row[1].srcOutline, rx, y, 0, row[1].flipX);
    std::vector<const Poly*> obsR = obs;
    obsR.push_back(&left);
    if (!canSit(right, obsR, minGap, dia)) return false;
    right.kind = row[1].kind; right.size = row[1].size; right.label = row[1].label;
    right.pairId = row[1].pairId; right.role = row[1].role; right.copyIndex = row[1].copyIndex;
    right.srcOutline = row[1].srcOutline; right.flipX = row[1].flipX; right.rotation = 0;

    row[0] = std::move(left);
    row[1] = std::move(right);
    bottomAlign(row);
    pressRowUp(row, obstacles, minGap);
    return true;
}

/** 2-body row: left 180° (arm bottom) / right 0° (arm top), pinned to DIA edges. */
static bool restyleTwoBodyOppPin(std::vector<Piece>& row, double y,
                                 const std::vector<Piece>& obstacles, double minGap, double dia) {
    if (row.size() != 2) return false;
    if (row[0].srcOutline.points.empty() || row[1].srcOutline.points.empty()) return false;
    std::sort(row.begin(), row.end(), [](const Piece& a, const Piece& b) { return a.bbox.x < b.bbox.x; });
    auto obs = refs(obstacles);

    Piece left = placePiece(row[0].srcOutline, 0, y, 180, row[0].flipX);
    if (!canSit(left, obs, minGap, dia)) return false;
    left.kind = row[0].kind; left.size = row[0].size; left.label = row[0].label;
    left.pairId = row[0].pairId; left.role = row[0].role; left.copyIndex = row[0].copyIndex;
    left.srcOutline = row[0].srcOutline; left.flipX = row[0].flipX; left.rotation = 180;

    double rx = dia - row[1].srcOutline.bbox.w;
    if (rx < 0) rx = 0;
    Piece right = placePiece(row[1].srcOutline, rx, y, 0, row[1].flipX);
    std::vector<const Poly*> obsR = obs;
    obsR.push_back(&left);
    if (!canSit(right, obsR, minGap, dia)) return false;
    right.kind = row[1].kind; right.size = row[1].size; right.label = row[1].label;
    right.pairId = row[1].pairId; right.role = row[1].role; right.copyIndex = row[1].copyIndex;
    right.srcOutline = row[1].srcOutline; right.flipX = row[1].flipX; right.rotation = 0;

    row[0] = std::move(left);
    row[1] = std::move(right);
    bottomAlign(row);
    pressRowUp(row, obstacles, minGap);
    return true;
}

static bool drySitIdx(const std::vector<BodyItem>& remaining, const std::vector<int>& idxs,
                      double y, double rot0, const std::vector<Piece>& obstacles,
                      double minGap, double dia, double& leftover) {
    if (idxs.empty()) return false;
    std::vector<Piece> row;
    double rot = rot0;
    for (size_t i = 0; i < idxs.size(); i++) {
        if (idxs[i] < 0 || idxs[i] >= (int)remaining.size()) return false;
        if (!tryAddBody(remaining[idxs[i]], row, y, obstacles, minGap, dia, rot)) return false;
        rot = flipRot(rot);
    }
    leftover = dia - row.back().bbox.r;
    if (leftover < 0) leftover = 0;
    return true;
}

static bool drySitIdxAnyOrder(const std::vector<BodyItem>& remaining, std::vector<int>& idxs,
                              double y, double rot0, const std::vector<Piece>& obstacles,
                              double minGap, double dia, double& leftover) {
    static const int kPerm3[6][3] = {
        {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}
    };
    if (idxs.size() != 3) return drySitIdx(remaining, idxs, y, rot0, obstacles, minGap, dia, leftover);
    std::vector<int> src = idxs;
    for (int p = 0; p < 6; p++) {
        std::vector<int> tryI = { src[kPerm3[p][0]], src[kPerm3[p][1]], src[kPerm3[p][2]] };
        if (drySitIdx(remaining, tryI, y, rot0, obstacles, minGap, dia, leftover)) {
            idxs.swap(tryI);
            return true;
        }
    }
    return false;
}

static bool sameRoleItems(const std::vector<BodyItem>& items) {
    if (items.empty()) return false;
    for (size_t i = 1; i < items.size(); i++) {
        if (items[i].role != items[0].role) return false;
    }
    return true;
}

/** Same-role bodies that fill DIA (22+21+20). Next row = mates, reverse width. Else empty → greedy. */
static std::vector<int> planComplementRow(const std::vector<BodyItem>& remaining,
                                          const std::vector<BodyItem>& lastPlan,
                                          double y, double rot0, const std::vector<Piece>& obstacles,
                                          double minGap, double dia) {
    const double fillOk = 5.0;
    if (lastPlan.size() >= 3 && sameRoleItems(lastPlan)) {
        std::vector<int> mates;
        bool ok = true;
        for (int i = (int)lastPlan.size() - 1; i >= 0; i--) {
            int m = findPair(remaining, lastPlan[i]);
            if (m < 0) { ok = false; break; }
            mates.push_back(m);
        }
        double left = 0;
        if (ok && drySitIdx(remaining, mates, y, rot0, obstacles, minGap, dia, left) && left < fillOk)
            return mates;
    }

    double bestLeft = 1e300, bestErr = 1e300;
    std::vector<int> best;
    struct DryHit { int role; int zs[3]; int zo[3]; bool ok; double left; };
    std::vector<DryHit> dryCache;
    const char* roles[2] = { "FRONT", "BACK" };
    for (int ri = 0; ri < 2; ri++) {
        std::vector<int> pool;
        for (size_t i = 0; i < remaining.size(); i++) {
            if (remaining[i].role == roles[ri]) pool.push_back((int)i);
        }
        const int n = (int)pool.size();
        if (n < 3) continue;
        /* Widest three still cannot fill (leftover ≥ 5) even flush — skip all triples. */
        std::vector<double> wide;
        wide.reserve((size_t)n);
        for (int i = 0; i < n; i++) wide.push_back(remaining[pool[i]].w);
        std::sort(wide.begin(), wide.end(), [](double u, double v) { return u > v; });
        if (wide[0] + wide[1] + wide[2] + 2.0 * minGap <= dia - fillOk + 1e-9) continue;
        for (int a = 0; a < n; a++) {
            for (int b = a + 1; b < n; b++) {
                for (int c = b + 1; c < n; c++) {
                    std::vector<int> combo = { pool[a], pool[b], pool[c] };
                    std::sort(combo.begin(), combo.end(), [&](int i, int j) {
                        if (std::fabs(remaining[i].w - remaining[j].w) > 1e-6)
                            return remaining[i].w > remaining[j].w;
                        return remaining[i].pairId < remaining[j].pairId;
                    });
                    const double sumW = remaining[combo[0]].w + remaining[combo[1]].w
                        + remaining[combo[2]].w;
                    if (sumW + 2.0 * minGap <= dia - fillOk + 1e-9) continue;
                    double left = 0;
                    /* Same three chart sizes already dry-sat this call — skip the 6 true-shape perms. */
                    const int za = sizeIndex(remaining[combo[0]].size);
                    const int zb = sizeIndex(remaining[combo[1]].size);
                    const int zc = sizeIndex(remaining[combo[2]].size);
                    int zs[3] = { za, zb, zc };
                    if (zs[0] > zs[1]) std::swap(zs[0], zs[1]);
                    if (zs[1] > zs[2]) std::swap(zs[1], zs[2]);
                    if (zs[0] > zs[1]) std::swap(zs[0], zs[1]);
                    bool cached = false;
                    for (size_t ci = 0; ci < dryCache.size(); ci++) {
                        if (dryCache[ci].role != ri || dryCache[ci].zs[0] != zs[0]
                            || dryCache[ci].zs[1] != zs[1] || dryCache[ci].zs[2] != zs[2])
                            continue;
                        cached = true;
                        if (!dryCache[ci].ok || dryCache[ci].left >= fillOk) { left = 1e300; break; }
                        left = dryCache[ci].left;
                        std::vector<int> ordered;
                        for (int k = 0; k < 3; k++) {
                            for (int t = 0; t < 3; t++) {
                                if (sizeIndex(remaining[combo[t]].size) == dryCache[ci].zo[k]
                                    && std::find(ordered.begin(), ordered.end(), combo[t]) == ordered.end()) {
                                    ordered.push_back(combo[t]);
                                    break;
                                }
                            }
                        }
                        if ((int)ordered.size() == 3) combo = std::move(ordered);
                        break;
                    }
                    if (cached) {
                        if (left >= fillOk) continue;
                    } else {
                        DryHit hit;
                        hit.role = ri;
                        hit.zs[0] = zs[0]; hit.zs[1] = zs[1]; hit.zs[2] = zs[2];
                        hit.ok = drySitIdxAnyOrder(remaining, combo, y, rot0, obstacles, minGap, dia, left);
                        hit.left = hit.ok ? left : 1e300;
                        hit.zo[0] = hit.ok ? sizeIndex(remaining[combo[0]].size) : -1;
                        hit.zo[1] = hit.ok ? sizeIndex(remaining[combo[1]].size) : -1;
                        hit.zo[2] = hit.ok ? sizeIndex(remaining[combo[2]].size) : -1;
                        dryCache.push_back(hit);
                        if (!hit.ok || left >= fillOk) continue;
                    }
                    const double sum = remaining[combo[0]].w + remaining[combo[1]].w + remaining[combo[2]].w;
                    const double err = std::fabs(sum - dia);
                    bool take = best.empty() || left < bestLeft - 0.08
                        || (std::fabs(left - bestLeft) <= 0.08 && err < bestErr - 1e-6);
                    if (!take && std::fabs(left - bestLeft) <= 0.08 && std::fabs(err - bestErr) <= 1e-6
                        && remaining[combo[0]].role == "FRONT" && remaining[best[0]].role != "FRONT")
                        take = true;
                    if (take) {
                        best = combo;
                        bestLeft = left;
                        bestErr = err;
                        if (bestLeft < 0.30) { a = n; b = n; c = n; }
                    }
                }
            }
        }
    }
    return best;
}

static void fillRowGreedy(std::vector<BodyItem>& remaining, std::vector<Piece>& row,
                          double y, double& rot, double rowStartRot,
                          const std::vector<Piece>& obstacles, double minGap, double dia) {
    BodyItem seed = remaining.front();
    remaining.erase(remaining.begin());
    bool first = tryAddBody(seed, row, y, obstacles, minGap, dia, rot);
    if (!first) {
        rot = flipRot(rowStartRot);
        first = tryAddBody(seed, row, y, obstacles, minGap, dia, rot);
    }
    if (!first) {
        Piece forced = placePiece(seed.outline, 0, y, rowStartRot, seed.flip);
        auto obs = refs(obstacles);
        if (!insideDia(forced, dia) || violatesGap(forced, obs, minGap)) return;
        attach(forced, seed);
        forced.rotation = rowStartRot;
        row.push_back(std::move(forced));
        rot = rowStartRot;
    }
    rot = flipRot(rot);

    int mate = findPair(remaining, seed);
    if (mate >= 0 && tryAddBody(remaining[mate], row, y, obstacles, minGap, dia, rot)) {
        remaining.erase(remaining.begin() + mate);
        rot = flipRot(rot);
    }

    bool filled = true;
    while (filled) {
        filled = false;
        for (size_t i = 0; i < remaining.size(); i++) {
            if (!tryAddBody(remaining[i], row, y, obstacles, minGap, dia, rot)) continue;
            BodyItem added = remaining[i];
            remaining.erase(remaining.begin() + (int)i);
            rot = flipRot(rot);
            int m2 = findPair(remaining, added);
            if (m2 >= 0 && tryAddBody(remaining[m2], row, y, obstacles, minGap, dia, rot)) {
                remaining.erase(remaining.begin() + m2);
                rot = flipRot(rot);
            }
            filled = true;
            break;
        }
    }
}

static PackedBodies packBodies(std::vector<BodyItem> remaining, double minGap, double dia) {
    PackedBodies out;
#if PF_DEV_TRIALS
    static const std::vector<Piece> kNoSlv;
    if (gDev.on) devBind(&out.rows, &kNoSlv, gSnapRpd, gSnapGap);
#endif
    double y = 0;
    /* First body 180°, then 0,180,0…  Next row starts opposite (0°) for curve nest. */
    double rowStartRot = 180;
    std::vector<BodyItem> lastComplement;
    while (!remaining.empty()) {
        std::vector<Piece> row;
        double rot = rowStartRot;
        std::vector<int> plan = planComplementRow(remaining, lastComplement, y, rot,
                                                 out.obstacles, minGap, dia);
        lastComplement.clear();
        if (!plan.empty()) {
            std::vector<BodyItem> taken;
            std::vector<char> used(remaining.size(), 0);
            bool ok = true;
            for (size_t i = 0; i < plan.size(); i++) {
                if (plan[i] < 0 || plan[i] >= (int)remaining.size() || used[plan[i]]) { ok = false; break; }
                used[plan[i]] = 1;
            }
            if (ok) {
                for (size_t i = 0; i < plan.size(); i++) taken.push_back(remaining[plan[i]]);
                for (int i = (int)remaining.size() - 1; i >= 0; i--) {
                    if (used[i]) remaining.erase(remaining.begin() + i);
                }
                for (size_t i = 0; i < taken.size(); i++) {
                    if (!tryAddBody(taken[i], row, y, out.obstacles, minGap, dia, rot)) {
                        remaining.insert(remaining.begin(), taken.begin() + (int)i, taken.end());
                        ok = false;
                        break;
                    }
                    rot = flipRot(rot);
                }
                if (ok) lastComplement = taken;
            }
            if (!ok && row.empty()) {
                fillRowGreedy(remaining, row, y, rot, rowStartRot, out.obstacles, minGap, dia);
            }
        } else {
            fillRowGreedy(remaining, row, y, rot, rowStartRot, out.obstacles, minGap, dia);
        }
        if (row.empty()) break;

        alignBodyRow(row);
        pressRowUp(row, out.obstacles, minGap);
        nudgeRowRightToClear(row, out.obstacles, minGap, dia);
        while (row.size() >= 2 && rowNeighborOverlay(row, minGap)) {
            remaining.insert(remaining.begin(), itemFromPiece(row.back()));
            row.pop_back();
            lastComplement.clear();
            if (row.size() < 2) break;
            alignBodyRow(row);
            pressRowUp(row, out.obstacles, minGap);
            nudgeRowRightToClear(row, out.obstacles, minGap, dia);
        }
        if (!lastComplement.empty() && lastComplement.size() != row.size()) lastComplement.clear();
        if (lastComplement.empty() && row.size() >= 2) {
            std::vector<BodyItem> keep;
            for (size_t i = 0; i < row.size(); i++) keep.push_back(itemFromPiece(row[i]));
            if (sameRoleItems(keep)) lastComplement.swap(keep);
        }
        BodyRow br;
        br.pieces = row;
        br.y0 = rowY0(row);
        br.y1 = rowY1(row);
        br.startRot = rowStartRot;
        br.splitPin = false;
        br.pinFlipped = false;
        br.fullGap = false;
        br.hafCenter = false;
        br.layoutPin = false;
        br.hafCap = -1;
        {
            double right = 0;
            for (size_t i = 0; i < row.size(); i++) {
                if (row[i].bbox.r > right) right = row[i].bbox.r;
            }
            /* Max DIA used, leftover ~0 — pin these bodies. Overlay never pins. */
            if (dia - right <= std::max(0.08, minGap) && !bodiesOverlay(row))
                br.layoutPin = true;
        }
        out.rows.push_back(br);
        for (size_t i = 0; i < row.size(); i++) out.obstacles.push_back(row[i]);
#if PF_DEV_TRIALS
        if (gDev.on) { PF_CTX("bodies", (int)out.rows.size() - 1, "", ""); devSnap("sit", "body-row"); }
#endif
        y = br.y1 + minGap;
        rowStartRot = flipRot(rowStartRot);
    }
    out.y = y;
    return out;
}

static bool tryAddSleeve(const SleeveSpec& spec, int specIndex, double xStart, double y, double xFloor,
                         const std::vector<const Poly*>& obstacles, double minGap, double dia,
                         double rot, double xStep, Piece& out) {
#if PF_DEV_TRIALS
    gDev.size = spec.size;
    gDev.label = spec.label;
#endif
    if (!nestLeft(spec.outline, y, rot, spec.flip, xStart, obstacles, minGap, dia, xFloor,
                  xStep > 0 ? xStep : 0.28, true, out))
        return false;
    out.kind = "SLEEVE";
    out.size = spec.size;
    out.copyIndex = spec.copyIndex;
    out.label = spec.label;
    out.flipX = spec.flip;
    out.rotation = rot;
    out.specIndex = specIndex;
    out.isFull = spec.isFull;
    return true;
}

static double occupiedRightInBand(const std::vector<Piece>& bodies, double y0, double y1) {
    double right = 0;
    for (size_t i = 0; i < bodies.size(); i++) {
        const BBox& b = bodies[i].bbox;
        if (b.b <= y0 + 1e-6 || b.y >= y1 - 1e-6) continue;
        if (b.r > right) right = b.r;
    }
    return right;
}

static bool pressPieceUp(Piece& piece, const std::vector<const Poly*>& blockers, double minGap, double yFloor = 0) {
    double hi = piece.bbox.y;
    if (hi <= yFloor + 0.02) return false;
    double lo = std::max(0.0, yFloor), bestDy = 0;
    for (int i = 0; i < 20; i++) {
        double mid = (lo + hi) * 0.5;
        double dy = mid - piece.bbox.y;
        Poly moved = translatePoly(piece.points, 0, dy);
        if (moved.bbox.y < yFloor - 0.02 || violatesGap(moved, blockers, minGap)) lo = mid;
        else { bestDy = dy; hi = mid; }
    }
    if (std::fabs(bestDy) > 1e-4) {
        movePiece(piece, 0, bestDy);
        return true;
    }
    return false;
}

/** Each FULL sleeve slides sideways into a taper pocket, then climbs to true minGap. */
static void pressFullRowPiecesUp(std::vector<Piece>& row, const std::vector<Piece>& obstacles,
                                 double minGap, double dia) {
    if (row.empty()) return;
    std::vector<double> floorY(row.size());
    for (size_t i = 0; i < row.size(); i++)
        floorY[i] = row[i].bbox.y - row[i].bbox.h * 0.55;
    for (size_t i = 0; i < row.size(); i++) {
        std::vector<const Poly*> blockers = refs(obstacles);
        for (size_t j = 0; j < row.size(); j++) {
            if (j != i) blockers.push_back(&row[j]);
        }
        Piece best = row[i];
        pressPieceUp(best, blockers, minGap, floorY[i]);
        const double x0 = row[i].bbox.x;
        const double w = row[i].bbox.w;
        for (double dx = -w * 0.42; dx <= w * 0.42 + 1e-6; dx += 0.32) {
            if (std::fabs(dx) < 0.05) continue;
            Piece t = row[i];
            movePiece(t, dx, 0);
            if (!canSit(t, blockers, minGap, dia)) continue;
            pressPieceUp(t, blockers, minGap, floorY[i]);
            if (t.bbox.y < best.bbox.y - 0.02) best = std::move(t);
        }
        row[i] = std::move(best);
    }
}

static bool pressPieceLeft(Piece& piece, const std::vector<const Poly*>& blockers, double minGap, double dia) {
    double lo = 0, hi = piece.bbox.x, bestDx = 0;
    if (hi <= 0.02) return false;
    for (int i = 0; i < 14; i++) {
        double mid = (lo + hi) * 0.5;
        double dx = mid - piece.bbox.x;
        Poly moved = translatePoly(piece.points, dx, 0);
        if (!insideDia(moved, dia) || violatesGap(moved, blockers, minGap)) lo = mid;
        else { bestDx = dx; hi = mid; }
    }
    if (std::fabs(bestDx) > 1e-4) {
        movePiece(piece, bestDx, 0);
        return true;
    }
    return false;
}

static void pressSleevesUp(std::vector<Piece*>& sleeves, const std::vector<Piece>& bodies, double minGap, double dia) {
    for (int pass = 0; pass < 4; pass++) {
        std::sort(sleeves.begin(), sleeves.end(), [](const Piece* a, const Piece* b) {
            return a->bbox.y < b->bbox.y;
        });
        bool changed = false;
        for (size_t i = 0; i < sleeves.size(); i++) {
            std::vector<const Poly*> blockers;
            for (size_t b = 0; b < bodies.size(); b++) blockers.push_back(&bodies[b]);
            for (size_t j = 0; j < sleeves.size(); j++) {
                if (sleeves[j] != sleeves[i]) blockers.push_back(sleeves[j]);
            }
            if (sleeves[i]->armPair) continue;
            if (pressPieceUp(*sleeves[i], blockers, minGap)) changed = true;
            if (pressPieceLeft(*sleeves[i], blockers, minGap, dia)) changed = true;
        }
        if (!changed) break;
    }
}

static double armholeY(const Piece& body, double sleeveH) {
    double rot = body.rotation;
    if (std::fabs(rot) < 1 || std::fabs(rot - 360) < 1) return body.bbox.y;
    return body.bbox.b - sleeveH;
}

static double leftoverFloorFor(const SleeveSpec& spec, double y, const std::vector<Piece>& bodies) {
    double h = spec.outline.bbox.h, w = spec.outline.bbox.w;
    double occ = occupiedRightInBand(bodies, y, y + h);
    return std::max(0.0, occ - w * 0.82);
}

static double channelWidth(const std::vector<Piece>& bodies, double y0, double y1, double dia) {
    return dia - occupiedRightInBand(bodies, y0, y1);
}

static bool hasRightChannel(const std::vector<Piece>& bodies, double bodyTop, double bodyBot, double dia, double minW) {
    for (double y = bodyTop; y < bodyBot; y += 0.85) {
        if (channelWidth(bodies, y, y + 4, dia) >= minW) return true;
    }
    return false;
}

static std::vector<double> leftoverYCandidates(const SleeveSpec& spec, const std::vector<Piece>& bodies,
                                               const std::vector<Piece>& placed, const std::vector<BodyRow>& bodyRows,
                                               double bodyTop, double bodyBot, double minGap, bool fine, double dia) {
    double h = spec.outline.bbox.h;
    std::vector<double> ys;
    std::map<int, int> seen;
    auto add = [&](double y) {
        if (y < bodyTop - 0.08) y = bodyTop;
        double rowBot = bodyRows.empty() ? bodyBot : bodyRowBottomAt(bodyRows, y);
        /* Side leftover must finish above the body-row hem — not hang into the next row. */
        if (y + h > rowBot + 0.04) return;
        int key = (int)(y * 25 + 0.5);
        if (seen.count(key)) return;
        seen[key] = 1;
        ys.push_back(y);
    };
    add(bodyTop);
    for (size_t i = 0; i < bodies.size(); i++) {
        add(bodies[i].bbox.y);
        add(armholeY(bodies[i], h));
        add(bodies[i].bbox.b - h);
        add((bodies[i].bbox.y + bodies[i].bbox.b - h) * 0.5);
    }
    for (size_t i = 0; i < placed.size(); i++) {
        if (placed[i].sleeveBlock) continue;
        add(placed[i].bbox.b + minGap);
        add(placed[i].bbox.y);
        add(placed[i].bbox.y - h - minGap);
    }
    double step = fine ? 0.16 : 0.36;
    for (double y = bodyTop; y <= bodyBot - h + 0.02; y += step) add(y);
    std::sort(ys.begin(), ys.end());
    std::vector<double> out;
    for (size_t i = 0; i < ys.size(); i++) {
        if (channelWidth(bodies, ys[i], ys[i] + h, dia) >= 0.45) out.push_back(ys[i]);
    }
    return out;
}

static bool trySleeveInLeftover(const SleeveSpec& spec, int specIndex, const std::vector<Piece>& placed,
                                const std::vector<Piece>& bodies, const std::vector<BodyRow>& bodyRows,
                                double bodyTop, double bodyBot, double dia, double minGap, bool fine, Piece& out) {
    auto ys = leftoverYCandidates(spec, bodies, placed, bodyRows, bodyTop, bodyBot, minGap, fine, dia);
    double w = spec.outline.bbox.w;
    auto blockers = refs2(bodies, placed);
    for (size_t i = 0; i < ys.size(); i++) {
        if (channelWidth(bodies, ys[i], ys[i] + spec.outline.bbox.h, dia) < 0.4) continue;
        Piece t;
        if (!tryAddSleeve(spec, specIndex, std::max(0.0, dia - w), ys[i], leftoverFloorFor(spec, ys[i], bodies),
                          blockers, minGap, dia, 0, fine ? 0.14 : 0.22, t))
            continue;
        if (!sideSleeveFitsRow(t, bodyRows)) continue;
        out = std::move(t);
        return true;
    }
    return false;
}

/** FULL leftover: sit in a right-of-body pocket after HAF. May hang past that row's hem. */
static bool tryFullInLeftover(const SleeveSpec& spec, int specIndex, const std::vector<Piece>& placed,
                              const std::vector<Piece>& bodies, double bodyTop, double bodyBot,
                              double dia, double minGap, Piece& out) {
    const double h = spec.outline.bbox.h;
    const double w = spec.outline.bbox.w;
    std::vector<double> ys;
    ys.push_back(bodyTop);
    for (size_t i = 0; i < bodies.size(); i++) {
        ys.push_back(bodies[i].bbox.y);
        ys.push_back(std::max(bodyTop, bodies[i].bbox.b - h));
    }
    for (size_t i = 0; i < placed.size(); i++) {
        if (placed[i].sleeveBlock) continue;
        ys.push_back(placed[i].bbox.b + minGap);
        ys.push_back(placed[i].bbox.y);
    }
    for (double y = bodyTop; y <= bodyBot; y += 0.18) ys.push_back(y);
    std::sort(ys.begin(), ys.end());
    auto blockers = refs2(bodies, placed);
    const double pinRight = std::max(0.0, dia - w);
    /* Pass 0 = pin on DIA (body right). Later passes nest left through leftover only. */
    for (int pass = 0; pass < 3; pass++) {
        for (size_t i = 0; i < ys.size(); i++) {
            const double y = ys[i];
            if (y < bodyTop - 0.05 || y > bodyBot - 1.0) continue;
            const double bandBot = std::min(y + h, bodyBot + 0.25);
            if (channelWidth(bodies, y, bandBot, dia) < w * 0.50) continue;
            const double bodyFloor = leftoverFloorFor(spec, y, bodies);
            const double xFloor = (pass == 0) ? pinRight
                : (pass == 1) ? std::max(bodyFloor, dia - 2.0 * w - minGap)
                : bodyFloor;
            Piece t;
            if (!tryAddSleeve(spec, specIndex, pinRight, y, xFloor, blockers, minGap, dia, 0, 0.14, t))
                continue;
            out = std::move(t);
            return true;
        }
    }
    return false;
}

static void sortFullBySize(std::vector<RemSleeve>& rem, const std::vector<SleeveSpec>& pool, bool smallFirst) {
    std::sort(rem.begin(), rem.end(), [&](const RemSleeve& a, const RemSleeve& b) {
        const int ia = sizeIndex(pool[a.specIndex].size);
        const int ib = sizeIndex(pool[b.specIndex].size);
        if (ia != ib) return smallFirst ? (ia < ib) : (ia > ib);
        const double wa = pool[a.specIndex].outline.bbox.w;
        const double wb = pool[b.specIndex].outline.bbox.w;
        return smallFirst ? (wa < wb) : (wa > wb);
    });
}

static void sortFullSmallFirst(std::vector<RemSleeve>& rem, const std::vector<SleeveSpec>& pool) {
    sortFullBySize(rem, pool, true);
}

static void fillFullRightLeftover(std::vector<RemSleeve>& remaining, std::vector<Piece>& placed,
                                  const std::vector<SleeveSpec>& pool, const std::vector<Piece>& bodies,
                                  double bodyTop, double bodyBot, double dia, double minGap) {
    if (remaining.empty() || bodies.empty()) return;
    if (!hasRightChannel(bodies, bodyTop, bodyBot, dia, 1.8)) return;
    sortFullSmallFirst(remaining, pool);
    for (int pass = 0; pass < 2 && !remaining.empty(); pass++) {
        bool progressed = false;
        for (size_t i = 0; i < remaining.size();) {
            Piece t;
            const SleeveSpec& spec = pool[remaining[i].specIndex];
            bool ok = tryFullInLeftover(spec, remaining[i].specIndex, placed, bodies,
                                       bodyTop, bodyBot, dia, minGap, t);
            if (!ok) { i++; continue; }
            t.isFull = true;
            placed.push_back(std::move(t));
            remaining.erase(remaining.begin() + (int)i);
            progressed = true;
        }
        if (!progressed) break;
    }
}

static void fillRightLeftover(std::vector<RemSleeve>& remaining, std::vector<Piece>& placed,
                              const std::vector<SleeveSpec>& pool, const std::vector<Piece>& bodies,
                              const std::vector<BodyRow>& bodyRows,
                              double bodyTop, double bodyBot, double dia, double minGap) {
    if (remaining.empty() || bodies.empty()) return;
    if (!hasRightChannel(bodies, bodyTop, bodyBot, dia, 1.8)) return;
    for (int pass = 0; pass < 2 && !remaining.empty(); pass++) {
        bool progressed = false;
        for (size_t i = 0; i < remaining.size();) {
            Piece t;
            const SleeveSpec& spec = pool[remaining[i].specIndex];
            if (trySleeveInLeftover(spec, remaining[i].specIndex, placed, bodies, bodyRows, bodyTop, bodyBot, dia, minGap, false, t)) {
                placed.push_back(std::move(t));
                remaining.erase(remaining.begin() + (int)i);
                progressed = true;
            } else i++;
        }
        if (!progressed) break;
    }
    if (remaining.empty()) return;
    std::sort(remaining.begin(), remaining.end(), [&](const RemSleeve& a, const RemSleeve& b) {
        return pool[a.specIndex].outline.bbox.w < pool[b.specIndex].outline.bbox.w;
    });
    for (size_t i = 0; i < remaining.size();) {
        Piece t;
        const SleeveSpec& spec = pool[remaining[i].specIndex];
        if (trySleeveInLeftover(spec, remaining[i].specIndex, placed, bodies, bodyRows, bodyTop, bodyBot, dia, minGap, true, t)) {
            placed.push_back(std::move(t));
            remaining.erase(remaining.begin() + (int)i);
        } else i++;
    }
    std::sort(remaining.begin(), remaining.end(), [&](const RemSleeve& a, const RemSleeve& b) {
        return pool[b.specIndex].outline.bbox.w < pool[a.specIndex].outline.bbox.w;
    });
}

static bool pickSleeve(const std::vector<RemSleeve>& remaining, const std::vector<SleeveSpec>& pool,
                       double x, double y, double xFloor, const std::vector<const Poly*>& obstacles,
                       double minGap, double dia, double rot, int& outI, Piece& out) {
    for (size_t i = 0; i < remaining.size(); i++) {
        const SleeveSpec& spec = pool[remaining[i].specIndex];
        Piece t;
        if (tryAddSleeve(spec, remaining[i].specIndex, x, y, xFloor, obstacles, minGap, dia, rot, 0.28, t)) {
            outI = (int)i;
            out = std::move(t);
            return true;
        }
    }
    return false;
}

static void tightenHafRowLeft(std::vector<Piece>& row, const std::vector<Piece>& obstacles,
                              double minGap, double dia) {
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < row.size(); i++) {
            std::vector<const Poly*> blk = refs(obstacles);
            for (size_t j = 0; j < row.size(); j++) {
                if (j != i) blk.push_back(&row[j]);
            }
            pressPieceLeft(row[i], blk, minGap, dia);
        }
    }
}

/** Slide a finished 0° row under a top-aligned 180° row — same hem, one dx, then rigid climb. */
static void interlockHafPair(std::vector<Piece>& upper, std::vector<Piece>& lower,
                             const std::vector<Piece>& above, double minGap, double dia) {
    if (upper.empty() || lower.empty()) return;
    double wRef = 0;
    for (size_t i = 0; i < upper.size(); i++) wRef += upper[i].bbox.w;
    wRef /= (double)upper.size();
    const double seedY = rowY1(upper) + minGap;
    const double curTop = rowY0(lower);
    const double lift0 = seedY - curTop;
    if (std::fabs(lift0) > 1e-6) {
        for (size_t i = 0; i < lower.size(); i++) movePiece(lower[i], 0, lift0);
    }
    bottomAlign(lower);

    const double dxs[5] = { 0.0, wRef * 0.28, wRef * 0.42, wRef * 0.50, -wRef * 0.18 };
    std::vector<Piece> best = lower;
    double bestBot = rowY1(lower);
    std::vector<Piece> obs = above;
    for (size_t i = 0; i < upper.size(); i++) obs.push_back(upper[i]);
    for (int s = 0; s < 5; s++) {
        std::vector<Piece> trial = lower;
        bool in = true;
        for (size_t i = 0; i < trial.size(); i++) {
            movePiece(trial[i], dxs[s], 0);
            if (!insideDia(trial[i], dia)) { in = false; break; }
        }
        if (!in) continue;
        bool gapOk = true;
        for (size_t i = 0; i < trial.size() && gapOk; i++) {
            std::vector<const Poly*> blk = refs(obs);
            for (size_t j = 0; j < trial.size(); j++) {
                if (j != i) blk.push_back(&trial[j]);
            }
            if (violatesGap(trial[i], blk, minGap)) gapOk = false;
        }
        if (!gapOk) continue;
        pressRigidRowUp(trial, obs, minGap);
        const double bot = rowY1(trial);
        if (bot < bestBot - 0.02) {
            bestBot = bot;
            best.swap(trial);
        }
    }
    lower.swap(best);
}

/** Both interlocking rows: leftover DIA shared equally (sides + X-order gaps) so the block centers on max DIA. */
static void distributeHafBlockOnDia(std::vector<Piece>& block, double dia) {
    if (block.size() < 2) return;
    double minX = 1e300, maxR = -1e300;
    for (size_t i = 0; i < block.size(); i++) {
        if (block[i].bbox.x < minX) minX = block[i].bbox.x;
        if (block[i].bbox.r > maxR) maxR = block[i].bbox.r;
    }
    if (minX > 1e299) return;
    if (std::fabs(minX) > 1e-6) {
        for (size_t i = 0; i < block.size(); i++) movePiece(block[i], -minX, 0);
        maxR -= minX;
    }
    const double leftover = dia - maxR;
    if (leftover < 0.08) return;
    std::vector<size_t> ord(block.size());
    for (size_t i = 0; i < block.size(); i++) ord[i] = i;
    std::sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
        return block[a].bbox.x < block[b].bbox.x;
    });
    const double share = leftover / (double)(block.size() + 1);
    for (size_t k = 0; k < ord.size(); k++) movePiece(block[ord[k]], share * (double)(k + 1), 0);
}

/** After DIA spread: 0° row keeps its X/hem and climbs as one slab until true minGap with the 180° row. */
static void pressHafLowerToTrueGap(std::vector<Piece>& block, const std::vector<Piece>& extraObs, double minGap) {
    std::vector<Piece> upper, lower;
    std::vector<size_t> lowerIdx;
    for (size_t i = 0; i < block.size(); i++) {
        if (block[i].isFull) continue;
        if (std::fabs(block[i].rotation - 180) < 1) upper.push_back(block[i]);
        else {
            lowerIdx.push_back(i);
            lower.push_back(block[i]);
        }
    }
    if (upper.empty() || lower.empty()) return;
    std::vector<Piece> obs = extraObs;
    for (size_t i = 0; i < upper.size(); i++) obs.push_back(upper[i]);
    pressRigidRowUp(lower, obs, minGap);
    for (size_t k = 0; k < lowerIdx.size(); k++) block[lowerIdx[k]] = std::move(lower[k]);
}

static std::vector<Piece> packBottomSleeveRow(std::vector<RemSleeve>& remaining, const std::vector<SleeveSpec>& pool,
                                              double y, double rot, const std::vector<const Poly*>& obstacles,
                                              double minGap, double dia, int maxN = 999, bool fromRight = false) {
    std::vector<Piece> row;
    double x = 0;
    int guard = 0;
    while (!remaining.empty() && (int)row.size() < maxN && guard++ < 80) {
        const SleeveSpec& spec0 = pool[remaining[0].specIndex];
        double w = spec0.outline.bbox.w;
        auto obs = obstacles;
        for (size_t i = 0; i < row.size(); i++) obs.push_back(&row[i]);
        int hitI = -1;
        Piece nest;
        const double xStart = fromRight ? std::max(0.0, dia - w) : x;
        const double xFloor = fromRight ? 0.0 : std::max(0.0, x - w * 0.12);
        if (!pickSleeve(remaining, pool, xStart, y, xFloor, obs, minGap, dia, rot, hitI, nest))
            break;
        row.push_back(std::move(nest));
        remaining.erase(remaining.begin() + hitI);
        x = fromRight ? row.back().bbox.x : (row.back().bbox.r + minGap);
    }
    return row;
}

static void tagBlock(std::vector<Piece>& row, int blockId, double rot) {
    for (size_t i = 0; i < row.size(); i++) {
        row[i].sleeveBlock = blockId;
        row[i].rotation = rot;
    }
}

/**
 * 0-21 HAF 3/6 interlock, Y-down: 50% X shift, 30% AABB overlap seed, then true minGap.
 * Same-width pair first — that is the shortest column for mixed sizes.
 */
static bool tryInterlockLower(const SleeveSpec& spec, int specIndex, const Piece& upper,
                              const std::vector<const Poly*>& obstacles, double minGap, double dia,
                              Piece& out) {
    const double w = spec.outline.bbox.w;
    const double xFloor = std::max(0.0, upper.bbox.x - w * 0.18);
    double xSeed = upper.bbox.x + upper.bbox.w * 0.50;
    if (xSeed + w > dia + 0.02) xSeed = dia - w;
    if (xSeed < 0) xSeed = 0;

    const double yOverlap[5] = {
        upper.bbox.b - upper.bbox.h * 0.30,
        upper.bbox.b - upper.bbox.h * 0.22,
        upper.bbox.b - upper.bbox.h * 0.12,
        upper.bbox.b + minGap * 0.25,
        upper.bbox.b + minGap
    };
    const double xSeeds[3] = { xSeed, std::max(0.0, upper.bbox.x + upper.bbox.w * 0.28), std::max(0.0, dia - w) };

    for (int yi = 0; yi < 5; yi++) {
        double y = yOverlap[yi];
        if (y < -0.02) y = 0;
        for (int xi = 0; xi < 3; xi++) {
            Piece t;
            if (!tryAddSleeve(spec, specIndex, xSeeds[xi], y, xFloor, obstacles, minGap, dia, 0, 0.14, t))
                continue;
            out = std::move(t);
            return true;
        }
    }
    return false;
}

static std::vector<const Poly*> refsSkip(const std::vector<Piece>& bodies, const std::vector<Piece>& placed,
                                         const Piece* skip) {
    std::vector<const Poly*> o;
    o.reserve(bodies.size() + placed.size());
    for (size_t i = 0; i < bodies.size(); i++) o.push_back(&bodies[i]);
    for (size_t i = 0; i < placed.size(); i++) {
        if (&placed[i] != skip) o.push_back(&placed[i]);
    }
    return o;
}

/** Pull a finished 180/0 block up into the previous block / bodies (min marker height). */
static void pressBlockUp(std::vector<Piece>& placed, size_t from, const std::vector<Piece>& bodies,
                         double minGap, double dia) {
    for (int pass = 0; pass < 5; pass++) {
        bool changed = false;
        /* 180° (upper) first so the next block seats into the previous pair's curves. */
        for (int want180 = 1; want180 >= 0; want180--) {
            for (size_t i = from; i < placed.size(); i++) {
                const bool is180 = std::fabs(placed[i].rotation - 180) < 1;
                if (want180 && !is180) continue;
                if (!want180 && is180) continue;
                auto blk = refsSkip(bodies, placed, &placed[i]);
                if (pressPieceUp(placed[i], blk, minGap)) changed = true;
                if (pressPieceLeft(placed[i], blk, minGap, dia)) changed = true;
            }
        }
        if (!changed) break;
    }
}

static bool pickInterlockLower(std::vector<RemSleeve>& remaining, const std::vector<SleeveSpec>& pool,
                               const Piece& upper, const std::vector<const Poly*>& obstacles,
                               double minGap, double dia, Piece& out, int& outI) {
    int bestI = -1;
    double bestScore = 1e300;
    Piece best;
    for (size_t i = 0; i < remaining.size(); i++) {
        const SleeveSpec& spec = pool[remaining[i].specIndex];
        Piece t;
        if (!tryInterlockLower(spec, remaining[i].specIndex, upper, obstacles, minGap, dia, t))
            continue;
        /* Closest width to the upper nests deepest and keeps the column short. */
        double score = std::fabs(spec.outline.bbox.w - upper.bbox.w);
        if (score < bestScore) {
            bestScore = score;
            bestI = (int)i;
            best = std::move(t);
        }
    }
    if (bestI < 0) return false;
    outI = bestI;
    out = std::move(best);
    return true;
}

static void packBottomSleeveBlocks(std::vector<RemSleeve>& remaining, const std::vector<SleeveSpec>& pool,
                                   double yStart, const std::vector<Piece>& bodies, std::vector<Piece>& placed,
                                   double minGap, double dia) {
    double y = yStart;
    int blockId = 1, stuck = 0;
    while (!remaining.empty() && stuck < 6) {
        auto above = refs2(bodies, placed);
        std::vector<Piece> row180 = packBottomSleeveRow(remaining, pool, y, 180, above, minGap, dia);
        if (row180.empty()) {
            const SleeveSpec& spec = pool[remaining[0].specIndex];
            Piece one;
            if (!tryAddSleeve(spec, remaining[0].specIndex, 0, y, 0, above, minGap, dia, 180, 0.28, one)) {
                y += spec.outline.bbox.h * 0.4;
                stuck += 1;
                continue;
            }
            remaining.erase(remaining.begin());
            row180.push_back(std::move(one));
        }
        stuck = 0;
        topAlign(row180);
        {
            std::vector<Piece> obs;
            for (size_t i = 0; i < bodies.size(); i++) obs.push_back(bodies[i]);
            for (size_t i = 0; i < placed.size(); i++) obs.push_back(placed[i]);
            tightenHafRowLeft(row180, obs, minGap, dia);
        }
        const int n = (int)row180.size();

        std::vector<Piece> row0;
        const bool allow32 = (n == 3 && (int)remaining.size() == 2);
        const bool allow31 = (n == 3 && (int)remaining.size() == 1);
        const int take0 = ((int)remaining.size() >= n) ? n : (allow32 ? 2 : (allow31 ? 1 : 0));
        if (take0 > 0) {
            const double yLow = rowY1(row180) + minGap;
            auto obsLow = refs2(bodies, placed);
            for (size_t i = 0; i < row180.size(); i++) obsLow.push_back(&row180[i]);
            row0 = packBottomSleeveRow(remaining, pool, yLow, 0, obsLow, minGap, dia, take0, true);
            if (!row0.empty()) {
                bottomAlign(row0);
                std::vector<Piece> obs;
                for (size_t i = 0; i < bodies.size(); i++) obs.push_back(bodies[i]);
                for (size_t i = 0; i < placed.size(); i++) obs.push_back(placed[i]);
                for (size_t i = 0; i < row180.size(); i++) obs.push_back(row180[i]);
                tightenHafRowLeft(row0, obs, minGap, dia);
                std::vector<Piece> abovePieces;
                for (size_t i = 0; i < bodies.size(); i++) abovePieces.push_back(bodies[i]);
                for (size_t i = 0; i < placed.size(); i++) abovePieces.push_back(placed[i]);
                interlockHafPair(row180, row0, abovePieces, minGap, dia);
            }
        }

        /* Complete block = 3+3 / 4+4 / 5+5… or 3+2 / 3+1 (same interlock, fewer 0°). */
        const bool pair32 = (n == 3 && (int)row0.size() == 2);
        const bool pair31 = (n == 3 && (int)row0.size() == 1);
        const bool complete = (n >= 3 && (int)row0.size() == n) || pair32 || pair31;
        if (complete) {
            tagBlock(row180, blockId, 180);
            tagBlock(row0, blockId, 0);
            blockId += 1;
        } else {
            tagBlock(row180, 0, 180);
            if (!row0.empty()) tagBlock(row0, 0, 0);
        }

        std::vector<Piece> block = row180;
        for (size_t i = 0; i < row0.size(); i++) block.push_back(row0[i]);
        std::vector<Piece> climbObs;
        for (size_t i = 0; i < bodies.size(); i++) climbObs.push_back(bodies[i]);
        for (size_t i = 0; i < placed.size(); i++) climbObs.push_back(placed[i]);
        if (complete) {
            distributeHafBlockOnDia(block, dia);
            pressHafLowerToTrueGap(block, climbObs, minGap);
        }
        pressRigidRowUp(block, climbObs, minGap);
        for (size_t i = 0; i < block.size(); i++) placed.push_back(block[i]);

        double bot = rowY1(block);
        y = bot + minGap;
    }
}

static bool pickFullAtY(std::vector<RemSleeve>& remaining, const std::vector<SleeveSpec>& pool,
                        double y, double rot, double xHint,
                        const std::vector<const Poly*>& obstacles, double minGap, double dia,
                        int& outI, Piece& out, bool pinFloor = false, bool smallFirst = false) {
    std::vector<size_t> order;
    order.reserve(remaining.size());
    for (size_t i = 0; i < remaining.size(); i++) order.push_back(i);
    if (smallFirst) {
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return pool[remaining[a].specIndex].outline.bbox.w < pool[remaining[b].specIndex].outline.bbox.w;
        });
    }
    for (size_t oi = 0; oi < order.size(); oi++) {
        const size_t i = order[oi];
        const SleeveSpec& spec = pool[remaining[i].specIndex];
        const double w = spec.outline.bbox.w;
        const double xStart = std::max(0.0, dia - w);
        const double xFloor = pinFloor ? std::max(0.0, xHint) : std::max(0.0, xHint - w * 0.92);
        Piece t;
        if (!tryAddSleeve(spec, remaining[i].specIndex, xStart, y, xFloor, obstacles, minGap, dia, rot, 0.08, t))
            continue;
        outI = (int)i;
        out = std::move(t);
        return true;
    }
    return false;
}

/** Fill one FULL row: 0/180/0/180. Odd slots (2nd, 4th…) share the same down Y. No max count. */
static void fillFullRow(std::vector<RemSleeve>& remaining, const std::vector<SleeveSpec>& pool,
                        double yBase, double drop, const std::vector<Piece>& bodies,
                        const std::vector<Piece>& placed, double minGap, double dia,
                        std::vector<Piece>& row, double xShift = 0, double startRot = 0) {
    double rot = startRot;
    int guard = 0;
    while (!remaining.empty() && guard++ < 40) {
        auto obs = refs2(bodies, placed);
        for (size_t i = 0; i < row.size(); i++) obs.push_back(&row[i]);
        const bool down = (row.size() % 2) == 1;
        const double y = down ? (yBase + drop) : yBase;
        const double xHint = row.empty() ? xShift : row.back().bbox.r;
        int hitI = -1;
        Piece nest;
        if (!pickFullAtY(remaining, pool, y, rot, xHint, obs, minGap, dia, hitI, nest,
                         row.empty() && xShift > 0.05))
            break;
        nest.rotation = rot;
        row.push_back(std::move(nest));
        remaining.erase(remaining.begin() + hitI);
        rot = (rot < 90) ? 180 : 0;
    }
}

static void tightenFullRowLeft(std::vector<Piece>& row, const std::vector<Piece>& bodies,
                               const std::vector<Piece>& placed, double minGap, double dia) {
    for (int pass = 0; pass < 3; pass++) {
        for (size_t i = 0; i < row.size(); i++) {
            std::vector<const Poly*> blk = refs2(bodies, placed);
            for (size_t j = 0; j < row.size(); j++) {
                if (j != i) blk.push_back(&row[j]);
            }
            pressPieceLeft(row[i], blk, minGap, dia);
        }
    }
}

static bool addFullSlot(std::vector<RemSleeve>& rem, const std::vector<SleeveSpec>& pool,
                        double y, double rot, const std::vector<Piece>& bodies,
                        const std::vector<Piece>& placed, double minGap, double dia,
                        std::vector<Piece>& row, bool smallFirst = false, bool pinRight = false) {
    auto obs = refs2(bodies, placed);
    for (size_t i = 0; i < row.size(); i++) obs.push_back(&row[i]);
    double xHint = 0;
    for (size_t i = 0; i < row.size(); i++) {
        if (row[i].bbox.r > xHint) xHint = row[i].bbox.r;
    }
    if (pinRight) xHint = dia;
    int hitI = -1;
    Piece nest;
    const bool pinFloor = pinRight;
    if (!pickFullAtY(rem, pool, y, rot, xHint, obs, minGap, dia, hitI, nest, pinFloor, smallFirst))
        return false;
    nest.rotation = rot;
    row.push_back(std::move(nest));
    rem.erase(rem.begin() + hitI);
    return true;
}

static bool fullRowValid(const std::vector<Piece>& row, const std::vector<Piece>& bodies,
                         const std::vector<Piece>& placed, double minGap, double dia) {
    if (row.empty()) return false;
    auto obs = refs2(bodies, placed);
    for (size_t i = 0; i < row.size(); i++) {
        if (!insideDia(row[i], dia)) return false;
        std::vector<const Poly*> blk = obs;
        for (size_t j = 0; j < row.size(); j++) {
            if (j != i) blk.push_back(&row[j]);
        }
        if (violatesGap(row[i], blk, minGap)) return false;
    }
    return true;
}

/** Align zipper groups if staggered, then press every neighbor to input minGap. No Y push. */
static void finishLeftoverFullRow(std::vector<Piece>& row, const std::vector<Piece>& bodies,
                                  const std::vector<Piece>& placed, double minGap, double dia) {
    if (row.empty()) return;
    bool zip = false;
    if (row.size() >= 2) {
        const double t0 = row[0].bbox.y;
        for (size_t i = 1; i < row.size(); i++) {
            if (std::fabs(row[i].bbox.y - t0) > 0.08) { zip = true; break; }
        }
    }
    if (zip) zipperAlignFullRow(row);
    tightenFullRowLeft(row, bodies, placed, minGap, dia);
    if (zip) zipperAlignFullRow(row);
}

/**
 * Fill across DIA: flat startRot/flip first, pull left to true gap, then add more
 * with the smallest per-slot drop so a 4th can sit in the right leftover.
 */
static void fillFullRowToDia(std::vector<RemSleeve>& remaining, const std::vector<SleeveSpec>& pool,
                             double yBase, const std::vector<Piece>& bodies,
                             const std::vector<Piece>& placed, double minGap, double dia,
                             std::vector<Piece>& row, double startRot = 0) {
    double rot = startRot;
    int guard = 0;
    while (!remaining.empty() && guard++ < 40) {
        if (!addFullSlot(remaining, pool, yBase, rot, bodies, placed, minGap, dia, row))
            break;
        rot = (rot < 90) ? 180 : 0;
    }
    tightenFullRowLeft(row, bodies, placed, minGap, dia);
    const double fracs[13] = { 0.0, 0.05, 0.08, 0.11, 0.14, 0.18, 0.22, 0.28, 0.34, 0.42, 0.50, 0.56, 0.60 };
    int extra = 0;
    while (!remaining.empty() && extra++ < 8) {
        rot = ((row.size() % 2) == 0) ? startRot : ((startRot < 90) ? 180.0 : 0.0);
        double href = pool[remaining[0].specIndex].outline.bbox.h;
        for (size_t ri = 1; ri < remaining.size(); ri++) {
            const double hh = pool[remaining[ri].specIndex].outline.bbox.h;
            if (hh > href) href = hh;
        }
        const bool preferSmall = row.size() >= 3;
        bool got = false;
        for (int k = 0; k < 13 && !got; k++) {
            const double yy = yBase + href * fracs[k];
            if (addFullSlot(remaining, pool, yy, rot, bodies, placed, minGap, dia, row, preferSmall, false))
                got = true;
            else if (addFullSlot(remaining, pool, yy, rot, bodies, placed, minGap, dia, row, preferSmall, true))
                got = true;
        }
        if (!got) break;
        tightenFullRowLeft(row, bodies, placed, minGap, dia);
    }
}

/**
 * Leftover FULL only (not Rule 2). First row does not push into body/HAF.
 * Later leftover FULL rows rigid-slab push toward the FULL row above; stop when
 * any one sleeve hits true minGap. Structure stays (no per-sleeve climb).
 * Flat L→R true gap first (5/6 if they fit). If 4 remain and will not sit flat:
 * smallest zipper drop such that DIA is used, neighbors = input minGap, odd top / even hem.
 */
static void packFullSleeveRows(std::vector<RemSleeve>& remaining, const std::vector<SleeveSpec>& pool,
                               double yStart, const std::vector<Piece>& bodies, std::vector<Piece>& placed,
                               double minGap, double dia) {
    if (remaining.empty()) return;
    sortFullSmallFirst(remaining, pool);
    int blockId = 1;
    for (size_t i = 0; i < placed.size(); i++) {
        if (placed[i].sleeveBlock >= blockId) blockId = placed[i].sleeveBlock + 1;
    }
    double y = yStart;
    double rowStartRot = 0;
    std::vector<Piece> prevFullRow;
    int stuck = 0;
    while (!remaining.empty() && stuck < 8) {
        double href = pool[remaining[0].specIndex].outline.bbox.h;
        for (size_t ri = 1; ri < remaining.size(); ri++) {
            const double hh = pool[remaining[ri].specIndex].outline.bbox.h;
            if (hh > href) href = hh;
        }
        std::vector<Piece> bestRow;
        std::vector<RemSleeve> bestRem;
        auto consider = [&](std::vector<Piece>& row, std::vector<RemSleeve>& rem) {
            if (row.empty()) return;
            finishLeftoverFullRow(row, bodies, placed, minGap, dia);
            if (!fullRowValid(row, bodies, placed, minGap, dia)) return;
            bool better = row.size() > bestRow.size();
            if (!better && row.size() == bestRow.size() && !bestRow.empty()) {
                double right = 0, bestRight = 0;
                for (size_t i = 0; i < row.size(); i++) if (row[i].bbox.r > right) right = row[i].bbox.r;
                for (size_t i = 0; i < bestRow.size(); i++) {
                    if (bestRow[i].bbox.r > bestRight) bestRight = bestRow[i].bbox.r;
                }
                better = right > bestRight + 0.15;
            }
            if (better) {
                bestRow.swap(row);
                bestRem.swap(rem);
            }
        };
        auto tryDrop = [&](double drop, double startRot = -1) {
            if (startRot < 0) startRot = rowStartRot;
            std::vector<RemSleeve> rem = remaining;
            std::vector<Piece> row;
            fillFullRow(rem, pool, y, drop, bodies, placed, minGap, dia, row, 0, startRot);
            consider(row, rem);
        };
        /* Flat first — 4 / 5 / 6 if they sit L→R at true gap. */
        tryDrop(0);
        /* 4+ remain but flat could not seat 4: min zipper drop that meets the 3 conditions. */
        if (remaining.size() >= 4 && bestRow.size() < 4) {
            const double zip[12] = { 0.04, 0.08, 0.12, 0.16, 0.20, 0.24, 0.30, 0.36, 0.42, 0.48, 0.54, 0.60 };
            for (int k = 0; k < 12; k++) {
                tryDrop(href * zip[k]);
                if (bestRow.size() >= 4) break;
            }
        }
        if (remaining.size() >= 4 && bestRow.size() < 4) {
            const double opp = (rowStartRot < 90) ? 180.0 : 0.0;
            tryDrop(0, opp);
            if (bestRow.size() < 4) {
                const double zip[12] = { 0.04, 0.08, 0.12, 0.16, 0.20, 0.24, 0.30, 0.36, 0.42, 0.48, 0.54, 0.60 };
                for (int k = 0; k < 12; k++) {
                    tryDrop(href * zip[k], opp);
                    if (bestRow.size() >= 4) break;
                }
            }
        }
        if (bestRow.empty()) {
            y += href * 0.35;
            stuck += 1;
            continue;
        }
        stuck = 0;
        remaining.swap(bestRem);
        for (size_t i = 0; i < bestRow.size(); i++) bestRow[i].sleeveBlock = blockId;
        /* Later leftover FULL rows only: whole slab up until one sleeve hits the FULL row above. */
        if (!prevFullRow.empty()) {
            pressRigidRowUp(bestRow, prevFullRow, minGap);
            finishLeftoverFullRow(bestRow, bodies, placed, minGap, dia);
        }
        for (size_t i = 0; i < bestRow.size(); i++) placed.push_back(bestRow[i]);
        prevFullRow = bestRow;
        y = rowY1(bestRow) + minGap;
        blockId += 1;
        rowStartRot = (rowStartRot < 90) ? 180.0 : 0.0;
    }
}

static int pullBlockSleevesIntoLeftover(std::vector<Piece>& placed, const std::vector<SleeveSpec>& pool,
                                        const std::vector<Piece>& bodies, const std::vector<BodyRow>& bodyRows,
                                        double bodyTop, double bodyBot,
                                        double dia, double minGap) {
    if (!hasRightChannel(bodies, bodyTop, bodyBot, dia, 1.8)) return 0;
    std::vector<int> idxs;
    for (size_t i = 0; i < placed.size(); i++) {
        if (placed[i].sleeveBlock && placed[i].specIndex >= 0) idxs.push_back((int)i);
    }
    std::sort(idxs.begin(), idxs.end(), [&](int a, int b) {
        return pool[placed[a].specIndex].outline.bbox.w < pool[placed[b].specIndex].outline.bbox.w;
    });
    if (idxs.size() > 8) idxs.resize(8);

    int pulled = 0;
    for (size_t n = 0; n < idxs.size(); n++) {
        int idx = idxs[n];
        if (!placed[idx].sleeveBlock || placed[idx].specIndex < 0) continue;
        const SleeveSpec& spec = pool[placed[idx].specIndex];
        if (spec.isFull) continue;
        int specIndex = placed[idx].specIndex;
        double w = spec.outline.bbox.w, h = spec.outline.bbox.h;
        std::vector<Piece> blockers;
        blockers.reserve(placed.size());
        for (size_t i = 0; i < placed.size(); i++) {
            if ((int)i != idx) blockers.push_back(placed[i]);
        }
        Piece t;
        bool ok = trySleeveInLeftover(spec, specIndex, blockers, bodies, bodyRows, bodyTop, bodyBot, dia, minGap, true, t);
        if (!ok) {
            auto obs = refs2(bodies, blockers);
            for (size_t i = 0; i < bodies.size() && !ok; i++) {
                double yArm = armholeY(bodies[i], h);
                if (yArm < -0.05) yArm = 0;
                for (int side = 0; side < 2 && !ok; side++) {
                    if (side == 1 && bodyOnLeft(bodies[i], bodies)) continue;
                    double xStart, xFloor;
                    if (side == 0) {
                        xStart = std::min(dia - w, bodies[i].bbox.r + minGap);
                        xFloor = std::max(0.0, bodies[i].bbox.r - w * 0.82);
                    } else {
                        xStart = std::max(0.0, bodies[i].bbox.x - w - minGap);
                        xFloor = std::max(0.0, bodies[i].bbox.x - w);
                    }
                    Piece cand;
                    if (tryAddSleeve(spec, specIndex, xStart, yArm, xFloor, obs, minGap, dia, 0, 0.16, cand)
                        && sideSleeveFitsRow(cand, bodyRows)) {
                        t = std::move(cand);
                        ok = true;
                    }
                }
            }
        }
        if (!ok) continue;
        t.sleeveBlock = 0;
        placed[idx] = std::move(t);
        pulled += 1;
    }
    return pulled;
}

static void compactBottomBlocks(std::vector<Piece>& placed, const std::vector<SleeveSpec>& pool,
                                const std::vector<Piece>& bodies, double bodyBot, double minGap, double dia) {
    std::vector<Piece> keep;
    std::vector<RemSleeve> specs;
    for (size_t i = 0; i < placed.size(); i++) {
        if (placed[i].sleeveBlock) {
            if (placed[i].specIndex >= 0) specs.push_back({ placed[i].specIndex });
        } else {
            keep.push_back(placed[i]);
        }
    }
    std::sort(specs.begin(), specs.end(), [&](const RemSleeve& a, const RemSleeve& b) {
        return pool[b.specIndex].outline.bbox.w < pool[a.specIndex].outline.bbox.w;
    });
    placed.swap(keep);
    if (!specs.empty()) packBottomSleeveBlocks(specs, pool, bodyBot + minGap, bodies, placed, minGap, dia);
}

/** One sleeve in the 0°/0° split-row center (180° first — cap down into the arm valley). */
static bool tryCenterGapSleeve(const BodyRow& row, std::vector<RemSleeve>& remaining,
                               const std::vector<SleeveSpec>& pool, const std::vector<Piece>& bodies,
                               std::vector<Piece>& placed, const std::vector<BodyRow>& bodyRows,
                               double minGap, double dia);

static bool rowHasSideSleeve(const BodyRow& row, const std::vector<Piece>& placed) {
    for (size_t i = 0; i < placed.size(); i++) {
        if (placed[i].sleeveBlock) continue;
        if (placed[i].bbox.b <= row.y0 + 0.15 || placed[i].bbox.y >= row.y1 - 0.15) continue;
        return true;
    }
    return false;
}

static void writeBackBodies(std::vector<Piece>& bodies, const std::vector<Piece>& row) {
    for (size_t i = 0; i < row.size(); i++) {
        for (size_t b = 0; b < bodies.size(); b++) {
            if (bodies[b].pairId == row[i].pairId && bodies[b].role == row[i].role) {
                bodies[b] = row[i];
                break;
            }
        }
    }
}

/** Armhole-path 2-body: top/bottom (and mid) stay where they sat — no later climb.
 * Overlay on that row → not frozen (never lock a red-mark piece). */
static bool rowSleevesFrozen(const BodyRow& row, const std::vector<Piece>& placed) {
    if (!row.splitPin || row.hafCenter || row.fullGap) return false;
    return !rowHasOverlay(row, placed);
}

static bool rowShiftLocked(const BodyRow& row, const std::vector<Piece>& placed) {
    return row.layoutPin && !rowHasOverlay(row, placed);
}

static bool sleeveOnFrozenRow(const std::vector<BodyRow>& rows, const std::vector<Piece>& placed,
                              const Piece& sl) {
    if (sl.sleeveBlock) return false;
    const double cy = (sl.bbox.y + sl.bbox.b) * 0.5;
    for (size_t r = 0; r < rows.size(); r++) {
        const bool frozen = rowSleevesFrozen(rows[r], placed);
        const bool pin = rowShiftLocked(rows[r], placed);
        if (!frozen && !pin) continue;
        if (rows[r].pieces.size() != 2 && !pin) continue;
        if (cy >= rows[r].y0 - 0.25 && cy <= rows[r].y1 + 0.25) {
            if (frozen) return true;
            if (pin && rows[r].pieces.size() == 2) return true;
        }
    }
    return false;
}

static bool sameRowPiece(const Piece& b, const BodyRow& row) {
    for (size_t i = 0; i < row.pieces.size(); i++) {
        if (b.pairId == row.pieces[i].pairId && b.role == row.pieces[i].role) return true;
    }
    return false;
}

static void shiftRowsBelow(std::vector<BodyRow>& rows, std::vector<Piece>& bodies,
                           std::vector<Piece>& placed, size_t afterIndex, double yCut, double dy) {
    if (std::fabs(dy) < 1e-4) return;
    for (size_t r = afterIndex + 1; r < rows.size(); r++) {
        if (rowShiftLocked(rows[r], placed)) continue;
        for (size_t i = 0; i < rows[r].pieces.size(); i++) movePiece(rows[r].pieces[i], 0, dy);
        rows[r].y0 = rowY0(rows[r].pieces);
        rows[r].y1 = rowY1(rows[r].pieces);
        writeBackBodies(bodies, rows[r].pieces);
    }
    for (size_t i = 0; i < placed.size(); i++) {
        if (placed[i].sleeveBlock) continue;
        if (sleeveOnFrozenRow(rows, placed, placed[i])) continue;
        if (placed[i].bbox.y + 0.05 >= yCut) movePiece(placed[i], 0, dy);
    }
}

/** Smallest downward shift so every piece in group has true minGap vs obstacles. */
static double dyDownToClear(const std::vector<Piece>& group, const std::vector<const Poly*>& obs,
                            double minGap) {
    bool hit = false;
    for (size_t i = 0; i < group.size(); i++) {
        if (violatesGap(group[i], obs, minGap)) { hit = true; break; }
    }
    if (!hit) return 0;
    double lo = 0, hi = 10.0, best = 10.0;
    for (int k = 0; k < 18; k++) {
        const double mid = (lo + hi) * 0.5;
        bool ok = true;
        for (size_t i = 0; i < group.size(); i++) {
            Poly t = translatePoly(group[i].points, 0, mid);
            if (violatesGap(t, obs, minGap)) { ok = false; break; }
        }
        if (ok) { best = mid; hi = mid; }
        else lo = mid;
    }
    return (best > 9.5) ? hi : best;
}

/**
 * After a row moves (180° trick), push it and every lower row down until
 * true minGap vs the row above. Adjacent body rows must not overlay.
 */
static void separateOverlappingBodyRows(std::vector<BodyRow>& rows, std::vector<Piece>& bodies,
                                        std::vector<Piece>& placed, double minGap) {
    for (size_t r = 1; r < rows.size(); r++) {
        std::vector<const Poly*> above;
        for (size_t b = 0; b < bodies.size(); b++) {
            if (sameRowPiece(bodies[b], rows[r])) continue;
            if (bodies[b].bbox.b <= rows[r].y0 + 8.0 && bodies[b].bbox.y < rows[r].y1)
                above.push_back(&bodies[b]);
        }
        if (above.empty()) continue;
        if (rowShiftLocked(rows[r], placed)) continue;
        const double dy = dyDownToClear(rows[r].pieces, above, minGap);
        if (dy <= 1e-4) continue;
        const double yCut = rows[r].y0 - 0.15;
        for (size_t k = r; k < rows.size(); k++) {
            if (rowShiftLocked(rows[k], placed)) continue;
            for (size_t i = 0; i < rows[k].pieces.size(); i++)
                movePiece(rows[k].pieces[i], 0, dy);
            rows[k].y0 = rowY0(rows[k].pieces);
            rows[k].y1 = rowY1(rows[k].pieces);
            writeBackBodies(bodies, rows[k].pieces);
        }
        for (size_t i = 0; i < placed.size(); i++) {
            if (placed[i].sleeveBlock) continue;
            if (sleeveOnFrozenRow(rows, placed, placed[i])) continue;
            if (placed[i].bbox.y + 0.05 >= yCut) movePiece(placed[i], 0, dy);
        }
    }
}

static void collectRowSideSleeves(const BodyRow& row, const std::vector<Piece>& placed, std::vector<int>& idxs) {
    idxs.clear();
    for (size_t i = 0; i < placed.size(); i++) {
        if (placed[i].sleeveBlock) continue;
        if (placed[i].bbox.b <= row.y0 + 0.15 || placed[i].bbox.y >= row.y1 - 0.15) continue;
        idxs.push_back((int)i);
    }
}

/** Both bodies the same chart size → cache key. Mixed pair = -1. */
static int rowBodySizeIdx(const BodyRow& row) {
    if (row.pieces.size() != 2) return -1;
    const int a = sizeIndex(row.pieces[0].size);
    const int b = sizeIndex(row.pieces[1].size);
    if (a != b || a >= SIZE_ORDER_N) return -1;
    return a;
}

static bool sleeveAtRowArmhole(const Piece& sl, const BodyRow& row) {
    for (size_t i = 0; i < row.pieces.size(); i++) {
        const Piece& b = row.pieces[i];
        double yArm = armholeY(b, sl.bbox.h);
        if (yArm < -0.05) yArm = 0;
        if (std::fabs(sl.bbox.y - yArm) > 1.25) continue;
        if (sl.bbox.r < b.bbox.x - 0.4 || sl.bbox.x > b.bbox.r + 0.4) continue;
        return true;
    }
    return false;
}

static bool hafSameOrSmaller(const SleeveSpec& spec, int maxSizeIdx) {
    if (spec.isFull) return false;
    return sizeIndex(spec.size) <= maxSizeIdx;
}

static int countHafSameOrSmaller(const std::vector<RemSleeve>& rem, const std::vector<SleeveSpec>& pool,
                                 int maxSizeIdx) {
    int n = 0;
    for (size_t i = 0; i < rem.size(); i++) {
        if (hafSameOrSmaller(pool[rem[i].specIndex], maxSizeIdx)) n += 1;
    }
    return n;
}

static void keepHafSameOrSmaller(std::vector<RemSleeve>& rem, const std::vector<SleeveSpec>& pool,
                                 int maxSizeIdx) {
    std::vector<RemSleeve> keep;
    keep.reserve(rem.size());
    for (size_t i = 0; i < rem.size(); i++) {
        if (hafSameOrSmaller(pool[rem[i].specIndex], maxSizeIdx)) keep.push_back(rem[i]);
    }
    rem.swap(keep);
}

static void sortRemainingSizePref(std::vector<RemSleeve>& remaining, const std::vector<SleeveSpec>& pool,
                                  const std::string& preferSize) {
    const int pref = sizeIndex(preferSize);
    std::sort(remaining.begin(), remaining.end(), [&](const RemSleeve& a, const RemSleeve& b) {
        const int ia = sizeIndex(pool[a.specIndex].size);
        const int ib = sizeIndex(pool[b.specIndex].size);
        auto rank = [&](int idx) {
            if (idx == pref) return 0;
            if (idx < pref) return 1 + (pref - idx);
            return 1000 + (idx - pref);
        };
        const int ra = rank(ia), rb = rank(ib);
        if (ra != rb) return ra < rb;
        return pool[b.specIndex].outline.bbox.w < pool[a.specIndex].outline.bbox.w;
    });
}

/** Deepest true-gap nest of a sleeve into one body's armhole. */
static bool trySleeveAtTargetArm(const SleeveSpec& spec, int specIndex, const Piece& target,
                                 bool targetIsRight, double rot, const std::vector<const Poly*>& obs,
                                 const std::vector<BodyRow>& bodyRows, double minGap, double dia, Piece& out) {
    const double w = spec.outline.bbox.w, h = spec.outline.bbox.h;
    double yArm = armholeY(target, h);
    if (yArm < -0.05) yArm = 0;
    const double ys[10] = {
        yArm, yArm + 0.08, yArm - 0.08, yArm + 0.18, yArm - 0.16,
        yArm + 0.32, yArm + 0.50, yArm + 0.75, yArm + 1.05, yArm + 1.25
    };
    double xLo, xHi;
    if (targetIsRight) {
        xHi = std::min(dia - w, target.bbox.x + w * 0.42);
        xLo = std::max(0.0, target.bbox.x - w * 0.90);
    } else {
        xHi = std::min(dia - w, target.bbox.r + minGap);
        xLo = std::max(0.0, target.bbox.r - w * 0.90);
    }
    if (xHi < xLo) return false;

    Piece best;
    bool found = false;
    double bestNest = -1e300;
    for (int yi = 0; yi < 10; yi++) {
        if (ys[yi] < -0.02) continue;
        for (double x = xHi; x >= xLo - 1e-6; x -= 0.06) {
            Piece t = placePiece(spec.outline, x, ys[yi], rot, spec.flip);
            if (!canSit(t, obs, minGap, dia)) continue;
            t.kind = "SLEEVE";
            t.size = spec.size;
            t.copyIndex = spec.copyIndex;
            t.label = spec.label;
            t.flipX = spec.flip;
            t.rotation = rot;
            t.specIndex = specIndex;
            t.armPair = true;
            const double nest = targetIsRight ? (t.bbox.r - target.bbox.x) : (target.bbox.r - t.bbox.x);
            if (!found || nest > bestNest + 1e-6) {
                best = std::move(t);
                bestNest = nest;
                found = true;
            }
        }
    }
    if (!found) return false;
    out = std::move(best);
    return true;
}

static bool tryExtraCenterSleeve(const BodyRow& row, std::vector<RemSleeve>& remaining,
                                 const std::vector<SleeveSpec>& pool, const std::vector<Piece>& bodies,
                                 std::vector<Piece>& placed, const std::vector<BodyRow>& bodyRows,
                                 double minGap, double dia, int maxSizeIdx = 999) {
    if (remaining.empty() || row.pieces.size() != 2) return false;
    const Piece* L = &row.pieces[0];
    const Piece* R = &row.pieces[1];
    if (L->bbox.x > R->bbox.x) std::swap(L, R);
    const double gapL = L->bbox.r, gapR = R->bbox.x;
    /* Tight nest still has an arm/placket valley — try if AABB leftover is tiny. */
    if (gapR - gapL < 0.12) return false;
    const double y0 = std::min(L->bbox.y, R->bbox.y);
    const double hem = std::min(L->bbox.b, R->bbox.b);
    const double rots[2] = { 180, 0 };
    for (size_t i = 0; i < remaining.size(); i++) {
        const SleeveSpec& spec = pool[remaining[i].specIndex];
        if (sizeIndex(spec.size) > maxSizeIdx) continue;
        const double w = spec.outline.bbox.w, h = spec.outline.bbox.h;
        const double xFloor = std::max(0.0, gapL - w * 0.82);
        const double xMid = (gapL + gapR - w) * 0.5;
        const double xSeeds[3] = {
            std::max(xFloor, std::min(dia - w, xMid)),
            std::max(xFloor, gapL + minGap),
            std::max(0.0, gapR - w - minGap)
        };
        const double ySeeds[3] = { y0 + std::max(1.0, h * 0.35), std::max(y0, (y0 + hem - h) * 0.5), y0 };
        auto obs = refs2(bodies, placed);
        for (int ri = 0; ri < 2; ri++) {
            for (int yi = 0; yi < 3; yi++) {
                for (int xi = 0; xi < 3; xi++) {
                    Piece t;
                    if (!tryAddSleeve(spec, remaining[i].specIndex, xSeeds[xi], ySeeds[yi],
                                      xFloor, obs, minGap, dia, rots[ri], 0.08, t))
                        continue;
                    t.armPair = true;
                    if (!sideSleeveFitsRow(t, bodyRows)) continue;
                    remaining.erase(remaining.begin() + (int)i);
                    placed.push_back(std::move(t));
                    return true;
                }
            }
        }
    }
    return false;
}

/** Snap two side sleeves to the row top and hem so a middle/placket slot opens. */
static void alignTwoSleevesTopBottom(const BodyRow& row, std::vector<Piece>& placed,
                                     const std::vector<Piece>& bodies, double minGap, double dia) {
    std::vector<int> side;
    collectRowSideSleeves(row, placed, side);
    if (side.size() != 2) return;
    int topI = side[0], botI = side[1];
    if (placed[topI].bbox.y > placed[botI].bbox.y) std::swap(topI, botI);
    auto tryMove = [&](int idx, double newY) -> bool {
        Piece t = placed[idx];
        movePiece(t, 0, newY - t.bbox.y);
        if (t.bbox.y < row.y0 - 0.06 || t.bbox.b > row.y1 + 0.04) return false;
        if (!insideDia(t, dia)) return false;
        std::vector<const Poly*> blk = refs(bodies);
        for (size_t i = 0; i < placed.size(); i++) {
            if ((int)i == idx) continue;
            blk.push_back(&placed[i]);
        }
        if (violatesGap(t, blk, minGap)) return false;
        placed[idx] = std::move(t);
        return true;
    };
    tryMove(topI, row.y0);
    tryMove(botI, row.y1 - placed[botI].bbox.h);
}

/** 3rd HAF between the top/bottom pair — leftover column or 2-body placket. */
static bool trySleeveInOpenedMiddle(const BodyRow& row, int topI, int botI,
                                    std::vector<RemSleeve>& remaining, const std::vector<SleeveSpec>& pool,
                                    const std::vector<Piece>& bodies, std::vector<Piece>& placed,
                                    const std::vector<BodyRow>& bodyRows, double minGap, double dia, int maxSizeIdx) {
    const Piece& top = placed[topI];
    const Piece& bot = placed[botI];
    const double yGap0 = top.bbox.b + minGap;
    const double yGap1 = bot.bbox.y - minGap;
    if (yGap1 - yGap0 < 0.8) return false;
    const double xHint = (std::min(top.bbox.x, bot.bbox.x) + std::max(top.bbox.r, bot.bbox.r)) * 0.5;
    for (size_t i = 0; i < remaining.size(); i++) {
        const SleeveSpec& spec = pool[remaining[i].specIndex];
        if (sizeIndex(spec.size) > maxSizeIdx) continue;
        const double w = spec.outline.bbox.w, h = spec.outline.bbox.h;
        if (h > (yGap1 - yGap0) + 0.35) continue;
        const double xFloor = std::max(0.0, xHint - w * 0.95);
        const double yMid = (yGap0 + yGap1 - h) * 0.5;
        const double ys[4] = { yMid, yGap0, std::max(yGap0, yGap1 - h), row.y0 + std::max(1.0, (row.y1 - row.y0 - h) * 0.5) };
        const double rots[2] = { 0, 180 };
        auto obs = refs2(bodies, placed);
        for (int ri = 0; ri < 2; ri++) {
            for (int yi = 0; yi < 4; yi++) {
                Piece t;
                if (!tryAddSleeve(spec, remaining[i].specIndex, std::max(0.0, dia - w), ys[yi],
                                  xFloor, obs, minGap, dia, rots[ri], 0.10, t))
                    continue;
                if (!sideSleeveFitsRow(t, bodyRows)) continue;
                remaining.erase(remaining.begin() + (int)i);
                placed.push_back(std::move(t));
                return true;
            }
        }
    }
    return false;
}

/** 2 side HAF: pin them top + hem, then add a same/smaller third in the opened placket/middle. */
static bool tryPlacketHaf(const BodyRow& row, std::vector<RemSleeve>& remaining,
                          const std::vector<SleeveSpec>& pool, const std::vector<Piece>& bodies,
                          std::vector<Piece>& placed, const std::vector<BodyRow>& bodyRows,
                          double minGap, double dia) {
    std::vector<int> side;
    collectRowSideSleeves(row, placed, side);
    if (side.size() != 2) return false;
    alignTwoSleevesTopBottom(row, placed, bodies, minGap, dia);
    collectRowSideSleeves(row, placed, side);
    if (side.size() != 2) return false;
    if (remaining.empty()) return false;

    int maxIdx = sizeIndex(row.pieces[0].size);
    for (size_t i = 1; i < row.pieces.size(); i++) {
        const int s = sizeIndex(row.pieces[i].size);
        if (s > maxIdx) maxIdx = s;
    }
    for (size_t i = 0; i < side.size(); i++) {
        const int s = sizeIndex(placed[side[i]].size);
        if (s > maxIdx) maxIdx = s;
    }
    sortRemainingSizePref(remaining, pool, row.pieces[0].size);

    int topI = side[0], botI = side[1];
    if (placed[topI].bbox.y > placed[botI].bbox.y) std::swap(topI, botI);
    if (trySleeveInOpenedMiddle(row, topI, botI, remaining, pool, bodies, placed, bodyRows, minGap, dia, maxIdx))
        return true;
    if (row.pieces.size() == 2)
        return tryExtraCenterSleeve(row, remaining, pool, bodies, placed, bodyRows, minGap, dia, maxIdx);
    return false;
}

/**
 * Pin 2 bodies to DIA edges and seat two HAF in the center (180° top, 0° bottom).
 * Used after a size has proven this recipe, or as a retry when opp-arm only got one.
 */
static bool applyCenterPair2(BodyRow& row, size_t rowIndex, std::vector<BodyRow>& bodyRows,
                             std::vector<Piece>& bodies, std::vector<Piece>& placed,
                             std::vector<RemSleeve>& remaining, const std::vector<SleeveSpec>& pool,
                             double minGap, double dia, std::vector<char>& proven2up) {
    if (row.pieces.size() != 2) return false;
    const int sz = rowBodySizeIdx(row);
    if (sz < 0) return false;
    std::vector<int> sideAll;
    collectRowSideSleeves(row, placed, sideAll);
    std::vector<int> side;
    for (size_t i = 0; i < sideAll.size(); i++) {
        if (!placed[sideAll[i]].isFull) side.push_back(sideAll[i]);
    }
    if (side.size() >= 2) return false;
    if (side.size() == 1 && !sleeveAtRowArmhole(placed[side[0]], row))
        return false;
    int cap = sz;
    if (side.size() == 1) cap = sizeIndex(placed[side[0]].size);
    const int need = (side.size() == 1) ? 1 : 2;
    if (countHafSameOrSmaller(remaining, pool, cap) < need) return false;

    std::vector<Piece> above;
    for (size_t b = 0; b < bodies.size(); b++) {
        if (!sameRowPiece(bodies[b], row)) above.push_back(bodies[b]);
    }
    std::vector<Piece> trial = row.pieces;
    if (!restyleTwoBodyOppPin(trial, row.y0, above, minGap, dia))
        return false;

    BodyRow trialRow = row;
    trialRow.pieces = trial;
    trialRow.splitPin = true;
    trialRow.y0 = rowY0(trial);
    trialRow.y1 = rowY1(trial);
    std::vector<Piece> bodiesT = bodies;
    writeBackBodies(bodiesT, trial);
    std::vector<BodyRow> rowsT = bodyRows;
    rowsT[rowIndex] = trialRow;

    std::vector<char> evict(placed.size(), 0);
    for (size_t i = 0; i < side.size(); i++) evict[side[i]] = 1;
    std::vector<Piece> placedT;
    std::vector<RemSleeve> remT = remaining;
    for (size_t i = 0; i < placed.size(); i++) {
        if (evict[i]) {
            if (placed[i].specIndex >= 0) remT.push_back({ placed[i].specIndex });
        } else {
            placedT.push_back(placed[i]);
        }
    }
    keepHafSameOrSmaller(remT, pool, cap);
    sortRemainingSizePref(remT, pool, side.empty() ? row.pieces[0].size : placed[side[0]].size);
    if (remT.size() < 2) return false;

    const Piece* L = &trialRow.pieces[0];
    const Piece* R = &trialRow.pieces[1];
    if (L->bbox.x > R->bbox.x) std::swap(L, R);
    const double slotRot[2] = { 180, 0 };
    const bool slotRight[2] = { true, false };
    const Piece* slotBody[2] = { R, L };
    int got = 0;
    for (int slot = 0; slot < 2 && !remT.empty(); slot++) {
        bool sat = false;
        for (size_t i = 0; i < remT.size() && !sat; i++) {
            const SleeveSpec& spec = pool[remT[i].specIndex];
            auto obs = refs2(bodiesT, placedT);
            Piece t;
            if (!trySleeveAtTargetArm(spec, remT[i].specIndex, *slotBody[slot], slotRight[slot],
                                      slotRot[slot], obs, rowsT, minGap, dia, t))
                continue;
            remT.erase(remT.begin() + (int)i);
            placedT.push_back(std::move(t));
            got += 1;
            sat = true;
        }
    }
    if (got < 2) {
        if (tryExtraCenterSleeve(trialRow, remT, pool, bodiesT, placedT, rowsT, minGap, dia, cap))
            got += 1;
    }
    if (got < 2) return false;

    const double oldY1 = row.y1;
    row = trialRow;
    bodyRows[rowIndex] = trialRow;
    writeBackBodies(bodies, trial);
    placed.swap(placedT);
    remaining.swap(remT);
    shiftRowsBelow(bodyRows, bodies, placed, rowIndex, oldY1, row.y1 - oldY1);
    proven2up[sz] = 1;
    return true;
}

/** 2-body + exactly 1 side sleeve → L 180 / R 0 + stacked center pair (180 top, 0 bottom). */
static bool tryOppArmSplit(BodyRow& row, size_t rowIndex, std::vector<BodyRow>& bodyRows,
                           std::vector<Piece>& bodies, std::vector<Piece>& placed,
                           std::vector<RemSleeve>& remaining, const std::vector<SleeveSpec>& pool,
                           double minGap, double dia, std::vector<char>& proven2up) {
    if (row.pieces.size() != 2) return false;
    std::vector<int> sideAll;
    collectRowSideSleeves(row, placed, sideAll);
    std::vector<int> sideIdx;
    for (size_t i = 0; i < sideAll.size(); i++) {
        if (!placed[sideAll[i]].isFull) sideIdx.push_back(sideAll[i]);
    }
    if (sideIdx.size() != 1) { PF_DECIDE("skip", "not-1-haf"); return false; }

    const Piece evicted = placed[sideIdx[0]];
    if (evicted.specIndex < 0 || evicted.isFull) { PF_DECIDE("skip", "full"); return false; }
    if (pool[evicted.specIndex].isFull) { PF_DECIDE("skip", "full"); return false; }
    if (!sleeveAtRowArmhole(evicted, row)) { PF_DECIDE("skip", "not-arm"); return false; }
    const int cap = sizeIndex(evicted.size);
    /* No same/smaller HAF mate — leave the right sleeve, do not move bodies. */
    if (countHafSameOrSmaller(remaining, pool, cap) < 1) { PF_DECIDE("skip", "nomate"); return false; }

    std::vector<Piece> above;
    for (size_t b = 0; b < bodies.size(); b++) {
        if (!sameRowPiece(bodies[b], row)) above.push_back(bodies[b]);
    }
    std::vector<Piece> trial = row.pieces;
    if (!restyleTwoBodyOppPin(trial, row.y0, above, minGap, dia)) { PF_DECIDE("skip", "pin-fail"); return false; }

    BodyRow trialRow = row;
    trialRow.pieces = trial;
    trialRow.splitPin = true;
    trialRow.y0 = rowY0(trial);
    trialRow.y1 = rowY1(trial);
    std::vector<Piece> bodiesT = bodies;
    writeBackBodies(bodiesT, trial);
    std::vector<BodyRow> rowsT = bodyRows;
    rowsT[rowIndex] = trialRow;

    std::vector<Piece> placedT;
    placedT.reserve(placed.size());
    for (size_t i = 0; i < placed.size(); i++) {
        if ((int)i == sideIdx[0]) continue;
        placedT.push_back(placed[i]);
    }
    std::vector<RemSleeve> remT = remaining;
    remT.push_back({ evicted.specIndex });
    keepHafSameOrSmaller(remT, pool, cap);
    sortRemainingSizePref(remT, pool, evicted.size);
    if (remT.size() < 2) return false;

    const Piece* L = &trialRow.pieces[0];
    const Piece* R = &trialRow.pieces[1];
    if (L->bbox.x > R->bbox.x) std::swap(L, R);

    int got = 0;
    /* Upper 180° → right 0° body arm. Lower 0° → left 180° body arm. */
    const double slotRot[2] = { 180, 0 };
    const bool slotRight[2] = { true, false };
    const Piece* slotBody[2] = { R, L };
    for (int slot = 0; slot < 2 && !remT.empty(); slot++) {
        bool sat = false;
        for (size_t i = 0; i < remT.size() && !sat; i++) {
            const SleeveSpec& spec = pool[remT[i].specIndex];
            auto obs = refs2(bodiesT, placedT);
            Piece t;
            if (!trySleeveAtTargetArm(spec, remT[i].specIndex, *slotBody[slot], slotRight[slot],
                                      slotRot[slot], obs, rowsT, minGap, dia, t))
                continue;
            remT.erase(remT.begin() + (int)i);
            placedT.push_back(std::move(t));
            got += 1;
            sat = true;
        }
    }
    if (got < 2)
        if (tryExtraCenterSleeve(trialRow, remT, pool, bodiesT, placedT, rowsT, minGap, dia, cap))
            got += 1;
    /* One sleeve after pin is the waste case — do not lock it in. */
    if (got < 2) { PF_DECIDE("skip", "pair-fail"); return false; }

    const double oldY1 = row.y1;
    row = trialRow;
    bodyRows[rowIndex] = trialRow;
    writeBackBodies(bodies, trial);
    placed.swap(placedT);
    remaining.swap(remT);
    shiftRowsBelow(bodyRows, bodies, placed, rowIndex, oldY1, row.y1 - oldY1);
    const int sz = rowBodySizeIdx(row);
    if (sz >= 0) proven2up[sz] = 1;
    PF_DECIDE("commit", "haf22");
    return true;
}

/** Last resort: 2-body row with no right/arm sleeve → 0° L/R pin + center sleeve; else keep 180/0. */
static bool tryLastResortSplit(BodyRow& row, size_t rowIndex, std::vector<BodyRow>& bodyRows,
                               std::vector<Piece>& bodies, std::vector<Piece>& placed,
                               std::vector<RemSleeve>& remaining, const std::vector<SleeveSpec>& pool,
                               double minGap, double dia) {
    if (row.pieces.size() != 2 || remaining.empty()) return false;
    if (rowHasSideSleeve(row, placed)) return false;

    std::vector<Piece> above;
    for (size_t b = 0; b < bodies.size(); b++) {
        if (!sameRowPiece(bodies[b], row)) above.push_back(bodies[b]);
    }
    std::vector<Piece> trial = row.pieces;
    if (!restyleTwoBodySplit(trial, row.y0, above, minGap, dia)) return false;

    BodyRow trialRow = row;
    trialRow.pieces = trial;
    trialRow.splitPin = true;
    trialRow.y0 = rowY0(trial);
    trialRow.y1 = rowY1(trial);
    std::vector<Piece> bodiesT = bodies;
    writeBackBodies(bodiesT, trial);
    std::vector<BodyRow> rowsT = bodyRows;
    rowsT[rowIndex] = trialRow;

    if (!tryCenterGapSleeve(trialRow, remaining, pool, bodiesT, placed, rowsT, minGap, dia))
        return false;

    const double oldY1 = row.y1;
    row = trialRow;
    bodyRows[rowIndex] = trialRow;
    writeBackBodies(bodies, trial);
    shiftRowsBelow(bodyRows, bodies, placed, rowIndex, oldY1, row.y1 - oldY1);
    return true;
}

static bool tryCenterGapSleeve(const BodyRow& row, std::vector<RemSleeve>& remaining,
                               const std::vector<SleeveSpec>& pool, const std::vector<Piece>& bodies,
                               std::vector<Piece>& placed, const std::vector<BodyRow>& bodyRows,
                               double minGap, double dia) {
    if (!row.splitPin || row.pieces.size() != 2 || remaining.empty()) return false;
    const Piece* L = &row.pieces[0];
    const Piece* R = &row.pieces[1];
    if (L->bbox.x > R->bbox.x) std::swap(L, R);
    const double gapL = L->bbox.r;
    const double gapR = R->bbox.x;
    if (gapR - gapL < 1.0) return false;
    const double y0 = std::min(L->bbox.y, R->bbox.y);
    const double hem = std::min(L->bbox.b, R->bbox.b);
    int placedN = 0;

    /* Dual armhole pocket: 180° first (cap down into both 0° armholes), then 0°. */
    for (int guard = 0; guard < 8 && !remaining.empty(); guard++) {
        bool got = false;
        for (size_t i = 0; i < remaining.size() && !got; i++) {
            const SleeveSpec& spec = pool[remaining[i].specIndex];
            const double w = spec.outline.bbox.w;
            const double h = spec.outline.bbox.h;
            const double xFloor = std::max(0.0, gapL - w * 0.82);
            const double xMid = (gapL + gapR - w) * 0.5;
            const double xRight = std::max(xFloor, std::min(dia - w, gapR - w + w * 0.82));
            const double xSeeds[5] = {
                std::max(xFloor, std::min(dia - w, xMid)),
                std::max(xFloor, gapL + minGap),
                std::max(0.0, gapR - w - minGap),
                xFloor,
                xRight
            };
            std::vector<double> ySeeds;
            ySeeds.push_back(y0);
            ySeeds.push_back(y0 + 0.12);
            ySeeds.push_back(y0 + 0.28);
            ySeeds.push_back(y0 + std::min(1.4, h * 0.18));
            ySeeds.push_back(std::max(y0, hem - h));
            ySeeds.push_back(std::max(y0, (y0 + hem - h) * 0.5));
            const double rots[2] = { 180, 0 };
            auto obs = refs2(bodies, placed);
            for (int ri = 0; ri < 2 && !got; ri++) {
                for (size_t yi = 0; yi < ySeeds.size() && !got; yi++) {
                    for (int xi = 0; xi < 5 && !got; xi++) {
                        Piece t;
                        if (!tryAddSleeve(spec, remaining[i].specIndex, xSeeds[xi], ySeeds[yi],
                                          xFloor, obs, minGap, dia, rots[ri], 0.08, t))
                            continue;
                        if (!sideSleeveFitsRow(t, bodyRows)) continue;
                        remaining.erase(remaining.begin() + (int)i);
                        placed.push_back(std::move(t));
                        placedN += 1;
                        got = true;
                    }
                }
            }
        }
        if (!got) break;
    }
    return placedN > 0;
}

static void sortFullThenHaf(std::vector<RemSleeve>& rem, const std::vector<SleeveSpec>& pool) {
    std::sort(rem.begin(), rem.end(), [&](const RemSleeve& a, const RemSleeve& b) {
        const bool fa = pool[a.specIndex].isFull;
        const bool fb = pool[b.specIndex].isFull;
        if (fa != fb) return fa;
        const double wa = pool[a.specIndex].outline.bbox.w;
        const double wb = pool[b.specIndex].outline.bbox.w;
        return wa < wb;
    });
}

static bool sleeveOverlapsRow(const Piece& sl, const BodyRow& row) {
    return sl.bbox.b > row.y0 + 0.15 && sl.bbox.y < row.y1 - 0.15;
}

/** Side sleeve sat in this row but its box crossed into another row. */
static bool sleeveCrossesRow(const Piece& sl, const BodyRow& row) {
    if (!sleeveOverlapsRow(sl, row)) return false;
    return sl.bbox.y < row.y0 - 0.10 || sl.bbox.b > row.y1 + 0.10;
}

/** One 0° sleeve in the opened dual-armhole pocket. FULL before HAF. */
static bool tryCenterZeroSleeve(const BodyRow& row, std::vector<RemSleeve>& remaining,
                                const std::vector<SleeveSpec>& pool, const std::vector<Piece>& bodies,
                                std::vector<Piece>& placed, const std::vector<BodyRow>& bodyRows,
                                double minGap, double dia) {
    if (row.pieces.size() != 2 || remaining.empty()) return false;
    const Piece* L = &row.pieces[0];
    const Piece* R = &row.pieces[1];
    if (L->bbox.x > R->bbox.x) std::swap(L, R);
    const double gapL = L->bbox.r;
    const double gapR = R->bbox.x;
    if (gapR - gapL < 1.0) return false;
    const double y0 = std::min(L->bbox.y, R->bbox.y);
    const double hem = std::min(L->bbox.b, R->bbox.b);
    sortFullThenHaf(remaining, pool);
    for (size_t i = 0; i < remaining.size(); i++) {
        const SleeveSpec& spec = pool[remaining[i].specIndex];
        const double w = spec.outline.bbox.w;
        const double h = spec.outline.bbox.h;
        const double xFloor = std::max(0.0, gapL - w * 0.82);
        const double xMid = (gapL + gapR - w) * 0.5;
        const double xSeeds[4] = {
            std::max(xFloor, std::min(dia - w, xMid)),
            std::max(xFloor, gapL + minGap),
            std::max(0.0, gapR - w - minGap),
            xFloor
        };
        const double ys[5] = {
            y0, y0 + 0.10, y0 + 0.22, y0 + std::min(0.80, h * 0.12),
            std::max(y0, (y0 + hem - h) * 0.5)
        };
        auto obs = refs2(bodies, placed);
        for (int yi = 0; yi < 5; yi++) {
            for (int xi = 0; xi < 4; xi++) {
                Piece t;
                if (!tryAddSleeve(spec, remaining[i].specIndex, xSeeds[xi], ys[yi],
                                  xFloor, obs, minGap, dia, 0, 0.08, t))
                    continue;
                if (!sideSleeveFitsRow(t, bodyRows)) continue;
                t.isFull = spec.isFull;
                t.armPair = true;
                remaining.erase(remaining.begin() + (int)i);
                placed.push_back(std::move(t));
                return true;
            }
        }
    }
    return false;
}

/**
 * 2-body row + a side sleeve (FULL first) whose 0° thick top took the armhole and
 * hung into another row: pin both bodies 0° L/R, seat one 0° sleeve in the middle.
 * Does not undo a committed HAF 2+2 (splitPin).
 */
static bool tryOverflowZeroPinSplit(BodyRow& row, size_t rowIndex, std::vector<BodyRow>& bodyRows,
                                    std::vector<Piece>& bodies, std::vector<Piece>& placed,
                                    std::vector<RemSleeve>& remaining, const std::vector<SleeveSpec>& pool,
                                    double minGap, double dia) {
    if (row.pieces.size() != 2 || row.splitPin) return false;

    int overflowI = -1;
    for (size_t i = 0; i < placed.size(); i++) {
        if (placed[i].sleeveBlock) continue;
        if (!sleeveCrossesRow(placed[i], row)) continue;
        if (placed[i].armPair && !placed[i].isFull) continue;
        if (overflowI < 0) overflowI = (int)i;
        else if (placed[i].isFull && !placed[overflowI].isFull) overflowI = (int)i;
        else if (placed[i].isFull == placed[overflowI].isFull
                 && placed[i].bbox.x > placed[overflowI].bbox.x)
            overflowI = (int)i;
    }
    if (overflowI < 0) return false;

    std::vector<Piece> above;
    for (size_t b = 0; b < bodies.size(); b++) {
        if (!sameRowPiece(bodies[b], row)) above.push_back(bodies[b]);
    }
    std::vector<Piece> trial = row.pieces;
    if (!restyleTwoBodySplit(trial, row.y0, above, minGap, dia)) return false;

    BodyRow trialRow = row;
    trialRow.pieces = trial;
    trialRow.splitPin = true;
    trialRow.y0 = rowY0(trial);
    trialRow.y1 = rowY1(trial);
    std::vector<Piece> bodiesT = bodies;
    writeBackBodies(bodiesT, trial);
    std::vector<BodyRow> rowsT = bodyRows;
    rowsT[rowIndex] = trialRow;

    std::vector<char> evict(placed.size(), 0);
    for (size_t i = 0; i < placed.size(); i++) {
        if (placed[i].sleeveBlock) continue;
        if (!sleeveOverlapsRow(placed[i], row)) continue;
        evict[i] = 1;
    }
    std::vector<Piece> placedT;
    std::vector<RemSleeve> remT = remaining;
    for (size_t i = 0; i < placed.size(); i++) {
        if (evict[i]) {
            if (placed[i].specIndex >= 0) remT.push_back({ placed[i].specIndex });
        } else {
            placedT.push_back(placed[i]);
        }
    }
    if (remT.empty()) return false;
    if (!tryCenterZeroSleeve(trialRow, remT, pool, bodiesT, placedT, rowsT, minGap, dia))
        return false;

    const double oldY1 = row.y1;
    row = trialRow;
    bodyRows[rowIndex] = trialRow;
    writeBackBodies(bodies, trial);
    placed.swap(placedT);
    remaining.swap(remT);
    shiftRowsBelow(bodyRows, bodies, placed, rowIndex, oldY1, row.y1 - oldY1);
    return true;
}

static void splitRemByKind(const std::vector<RemSleeve>& rem, const std::vector<SleeveSpec>& pool,
                           std::vector<RemSleeve>& fullRem, std::vector<RemSleeve>& hafRem) {
    fullRem.clear();
    hafRem.clear();
    for (size_t i = 0; i < rem.size(); i++) {
        if (pool[rem[i].specIndex].isFull) fullRem.push_back(rem[i]);
        else hafRem.push_back(rem[i]);
    }
}

static void applyOverflowZeroPinSplits(std::vector<BodyRow>& bodyRows, std::vector<Piece>& bodies,
                                       std::vector<Piece>& placed,
                                       std::vector<RemSleeve>& fullRem, std::vector<RemSleeve>& hafRem,
                                       const std::vector<SleeveSpec>& pool, double minGap, double dia) {
    std::vector<RemSleeve> rem = fullRem;
    for (size_t i = 0; i < hafRem.size(); i++) rem.push_back(hafRem[i]);
    for (size_t ri = 0; ri < bodyRows.size(); ri++) {
        tryOverflowZeroPinSplit(bodyRows[ri], ri, bodyRows, bodies, placed, rem, pool, minGap, dia);
    }
    splitRemByKind(rem, pool, fullRem, hafRem);
}

/** Same leftover + leftover-block pass as placeSleeves — reseat HAF evicted by the 0° pin. */
static void reseatLeftoverHaf(std::vector<RemSleeve>& hafRem, std::vector<Piece>& placed,
                              const std::vector<SleeveSpec>& pool, const std::vector<Piece>& bodies,
                              const std::vector<BodyRow>& bodyRows, double minGap, double dia) {
    if (hafRem.empty() || bodies.empty()) return;
    double bodyTop = 0, bodyBot = 0;
    for (size_t b = 0; b < bodies.size(); b++) {
        if (b == 0 || bodies[b].bbox.y < bodyTop) bodyTop = bodies[b].bbox.y;
        if (bodies[b].bbox.b > bodyBot) bodyBot = bodies[b].bbox.b;
    }
    std::sort(hafRem.begin(), hafRem.end(), [&](const RemSleeve& a, const RemSleeve& b) {
        return pool[b.specIndex].outline.bbox.w < pool[a.specIndex].outline.bbox.w;
    });
    fillRightLeftover(hafRem, placed, pool, bodies, bodyRows, bodyTop, bodyBot, dia, minGap);
    for (size_t i = 0; i < placed.size();) {
        if (placed[i].sleeveBlock || placed[i].armPair || placed[i].isFull
            || sideSleeveFitsRow(placed[i], bodyRows)) { i++; continue; }
        if (placed[i].specIndex >= 0) hafRem.push_back({ placed[i].specIndex });
        placed.erase(placed.begin() + (int)i);
    }
    if (hafRem.empty()) return;
    double bot = bodyBot;
    for (size_t i = 0; i < placed.size(); i++) {
        if (placed[i].bbox.b > bot) bot = placed[i].bbox.b;
    }
    packBottomSleeveBlocks(hafRem, pool, bot + minGap, bodies, placed, minGap, dia);
}

static int pickPrefIndex(const std::vector<RemSleeve>& rem, const std::vector<SleeveSpec>& pool,
                         int cap, bool wantFull) {
    int exact = -1, small = -1, smallZ = -999;
    for (size_t i = 0; i < rem.size(); i++) {
        const SleeveSpec& s = pool[rem[i].specIndex];
        if (s.isFull != wantFull) continue;
        const int z = sizeIndex(s.size);
        if (z > cap) continue;
        if (z == cap) { exact = (int)i; break; }
        if (z > smallZ) { smallZ = z; small = (int)i; }
    }
    return exact >= 0 ? exact : small;
}

static int pickUnusedFull(const std::vector<SleeveSpec>& pool, const std::vector<Piece>& placed, int cap) {
    std::vector<char> used(pool.size(), 0);
    for (size_t i = 0; i < placed.size(); i++) {
        if (placed[i].specIndex >= 0 && placed[i].specIndex < (int)pool.size())
            used[placed[i].specIndex] = 1;
    }
    int exact = -1, small = -1, smallZ = -999;
    for (size_t i = 0; i < pool.size(); i++) {
        if (used[i] || !pool[i].isFull) continue;
        const int z = sizeIndex(pool[i].size);
        if (z > cap) continue;
        if (z == cap) { exact = (int)i; break; }
        if (z > smallZ) { smallZ = z; small = (int)i; }
    }
    return exact >= 0 ? exact : small;
}

static void stampSleeve(Piece& p, const SleeveSpec& spec, int specIndex, double rot) {
    p.kind = "SLEEVE";
    p.size = spec.size;
    p.copyIndex = spec.copyIndex;
    p.label = spec.label;
    p.flipX = spec.flip;
    p.rotation = rot;
    p.specIndex = specIndex;
    p.isFull = spec.isFull;
}

/** Pin Body-1 to DIA left, Body-2 to DIA right. Always — no canSit abort. */
static bool forcePinTwoBodies(std::vector<Piece>& row, double y, double dia, double leftRot) {
    if (row.size() != 2 || row[0].srcOutline.points.empty() || row[1].srcOutline.points.empty())
        return false;
    std::sort(row.begin(), row.end(), [](const Piece& a, const Piece& b) { return a.bbox.x < b.bbox.x; });
    Piece left = placePiece(row[0].srcOutline, 0, y, leftRot, row[0].flipX);
    Piece right = placePiece(row[1].srcOutline, 0, y, 0, row[1].flipX);
    double rx = dia - right.bbox.w;
    if (rx < 0) rx = 0;
    if (std::fabs(right.bbox.x - rx) > 1e-9) movePiece(right, rx - right.bbox.x, 0);
    if (!insideDia(left, dia) || !insideDia(right, dia)) return false;
    left.kind = row[0].kind; left.size = row[0].size; left.label = row[0].label;
    left.pairId = row[0].pairId; left.role = row[0].role; left.copyIndex = row[0].copyIndex;
    left.srcOutline = row[0].srcOutline; left.flipX = row[0].flipX; left.rotation = leftRot;
    right.kind = row[1].kind; right.size = row[1].size; right.label = row[1].label;
    right.pairId = row[1].pairId; right.role = row[1].role; right.copyIndex = row[1].copyIndex;
    right.srcOutline = row[1].srcOutline; right.flipX = row[1].flipX; right.rotation = 0;
    row[0] = std::move(left);
    row[1] = std::move(right);
    topAlign(row);
    return true;
}

/**
 * Center-gap HAF. hugLeft=false (top 180°): Body-2 armhole, X from Body-2 left ← Body-1.
 * hugLeft=true (bottom 0°): Body-1 armhole, X from Body-1 right → Body-2.
 * canSit is true-gap vs Body-1 + Body-2 + already placed (incl. the top HAF).
 */
static bool sitCenterSlv(const SleeveSpec& spec, int specIndex, const Piece& L, const Piece& R,
                         double y, double rot, double rowY0, double rowY1,
                         const std::vector<Piece>& bodies, const std::vector<Piece>& placed,
                         double minGap, double dia, Piece& out, bool hugLeft = false) {
    const double w = spec.outline.bbox.w, h = spec.outline.bbox.h;
    if (y < rowY0 - 0.08 || y + h > rowY1 + 0.12) return false;
    auto obs = refs2(bodies, placed);
    const double tuck = std::min(w * 0.22, 2.4);
    const double xHi = std::min(dia - w, R.bbox.x - w + tuck);
    const double xLo = std::max(0.0, L.bbox.r - tuck);
    if (xHi < xLo - 1e-4) return false;

    auto inGap = [&](const Piece& t) -> bool {
        if (t.bbox.y < rowY0 - 0.10 || t.bbox.b > rowY1 + 0.12) return false;
        if (!hugLeft) {
            if (t.bbox.x > R.bbox.x - 0.20) return false;
            if (t.bbox.r > R.bbox.x + tuck + 0.08) return false;
        } else {
            if (t.bbox.r < L.bbox.r + 0.20) return false;
            if (t.bbox.x < L.bbox.r - tuck - 0.08) return false;
        }
        return true;
    };

    Piece found;
    bool ok = false;
    if (!hugLeft) {
        for (double x = xHi; x >= xLo - 1e-6; x -= 0.12) {
            Piece t = placePiece(spec.outline, x, y, rot, spec.flip);
            if (!canSit(t, obs, minGap, dia) || !inGap(t)) continue;
            found = std::move(t);
            ok = true;
            break;
        }
    } else {
        for (double x = xLo; x <= xHi + 1e-6; x += 0.12) {
            Piece t = placePiece(spec.outline, x, y, rot, spec.flip);
            if (!canSit(t, obs, minGap, dia) || !inGap(t)) continue;
            found = std::move(t);
            ok = true;
            break;
        }
    }
    if (!ok) return false;

    Piece best = found;
    if (!hugLeft) {
        double lo = std::max(xLo, found.bbox.x), hi = xHi;
        for (int i = 0; i < 12; i++) {
            const double mid = (lo + hi) * 0.5;
            Piece t = placePiece(spec.outline, mid, y, rot, spec.flip);
            if (canSit(t, obs, minGap, dia) && inGap(t)) {
                best = std::move(t);
                lo = mid;
            } else {
                hi = mid;
            }
        }
    }
    /* hugLeft (bottom 0°): do not pull left into Body-1's curve — that kills true gap.
     * First canSit from Body-1 is the seed; settleBottom0Right slides into the open X. */
    stampSleeve(best, spec, specIndex, rot);
    out = std::move(best);
#if PF_DEV_TRIALS
    PF_GEOM(out, rot, true, "ok", true);
#endif
    return true;
}

static bool scanCenterSlv(const SleeveSpec& spec, int specIndex, const Piece& L, const Piece& R,
                          double rowY0, double rowY1, double rot, bool fromTop,
                          const std::vector<Piece>& bodies, const std::vector<Piece>& placed,
                          double minGap, double dia, Piece& out, bool hugLeft = false) {
    const double h = spec.outline.bbox.h;
    if (fromTop) {
        for (double y = rowY0; y + h <= rowY1 + 0.10; y += 0.16) {
            if (sitCenterSlv(spec, specIndex, L, R, y, rot, rowY0, rowY1, bodies, placed, minGap, dia, out, hugLeft))
                return true;
        }
    } else {
        for (double y = rowY1 - h; y >= rowY0 - 0.04; y -= 0.16) {
            if (sitCenterSlv(spec, specIndex, L, R, y, rot, rowY0, rowY1, bodies, placed, minGap, dia, out, hugLeft))
                return true;
        }
    }
    return false;
}

/**
 * First try for 2-body HAF: lock X to the gap midpoint (between Body-1 right and Body-2 left).
 * Y / rotation stay the caller's. True-gap only — no armhole hug. Fail → existing sitCenterSlv.
 */
static bool sitMidX(const SleeveSpec& spec, int specIndex, const Piece& L, const Piece& R,
                    double y, double rot, double rowY0, double rowY1,
                    const std::vector<Piece>& bodies, const std::vector<Piece>& placed,
                    double minGap, double dia, Piece& out, bool clipRow = true) {
    const double w = spec.outline.bbox.w, h = spec.outline.bbox.h;
    if (clipRow) {
        if (y < rowY0 - 0.08 || y + h > rowY1 + 0.12) return false;
    }
    const double gapMid = (L.bbox.r + R.bbox.x) * 0.5;
    double x = gapMid - w * 0.5;
    if (x < 0) x = 0;
    if (x + w > dia) x = dia - w;
    Piece t = placePiece(spec.outline, x, y, rot, spec.flip);
    const double cx = (t.bbox.x + t.bbox.r) * 0.5;
    if (cx < L.bbox.r - 0.5 || cx > R.bbox.x + 0.5) return false;
    if (clipRow) {
        if (t.bbox.y < rowY0 - 0.10 || t.bbox.b > rowY1 + 0.12) return false;
    } else if (t.bbox.b < rowY0 - 0.15 || t.bbox.y > rowY1 + 0.15) {
        return false;
    }
    auto obs = refs2(bodies, placed);
    if (!canSit(t, obs, minGap, dia)) return false;
    stampSleeve(t, spec, specIndex, rot);
    t.midX = true;
    out = std::move(t);
#if PF_DEV_TRIALS
    PF_GEOM(out, rot, true, "ok", true);
#endif
    return true;
}

static bool scanMidX(const SleeveSpec& spec, int specIndex, const Piece& L, const Piece& R,
                     double rowY0, double rowY1, double rot, bool fromTop,
                     const std::vector<Piece>& bodies, const std::vector<Piece>& placed,
                     double minGap, double dia, Piece& out, bool clipRow = true, double yFloor = 0) {
    const double h = spec.outline.bbox.h;
    if (!clipRow) {
        const double yLo = std::max(yFloor, std::max(0.0, rowY0 - h));
        if (fromTop) {
            for (double y = yLo; y <= rowY0 + 0.40; y += 0.16) {
                if (sitMidX(spec, specIndex, L, R, y, rot, rowY0, rowY1, bodies, placed, minGap, dia, out, false))
                    return true;
            }
        } else {
            for (double y = rowY1 - h; y >= yLo - 0.04; y -= 0.16) {
                if (sitMidX(spec, specIndex, L, R, y, rot, rowY0, rowY1, bodies, placed, minGap, dia, out, false))
                    return true;
            }
        }
        return false;
    }
    if (fromTop) {
        for (double y = rowY0; y + h <= rowY1 + 0.10; y += 0.16) {
            if (sitMidX(spec, specIndex, L, R, y, rot, rowY0, rowY1, bodies, placed, minGap, dia, out))
                return true;
        }
    } else {
        for (double y = rowY1 - h; y >= rowY0 - 0.04; y -= 0.16) {
            if (sitMidX(spec, specIndex, L, R, y, rot, rowY0, rowY1, bodies, placed, minGap, dia, out))
                return true;
        }
    }
    return false;
}

/** Remaining HAF indices, largest size first (then wider). */
static void hafIdxLargestFirst(std::vector<int>& order, const std::vector<RemSleeve>& rem,
                               const std::vector<SleeveSpec>& pool) {
    order.clear();
    for (size_t i = 0; i < rem.size(); i++) {
        if (rem[i].specIndex >= 0 && rem[i].specIndex < (int)pool.size() && !pool[rem[i].specIndex].isFull)
            order.push_back((int)i);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const int za = sizeIndex(pool[rem[a].specIndex].size);
        const int zb = sizeIndex(pool[rem[b].specIndex].size);
        if (za != zb) return za > zb;
        return pool[rem[a].specIndex].outline.bbox.w > pool[rem[b].specIndex].outline.bbox.w;
    });
}

/** Remaining FULL indices, largest size first (then wider). */
static void fullIdxLargestFirst(std::vector<int>& order, const std::vector<RemSleeve>& rem,
                                const std::vector<SleeveSpec>& pool) {
    order.clear();
    for (size_t i = 0; i < rem.size(); i++) {
        if (rem[i].specIndex >= 0 && rem[i].specIndex < (int)pool.size() && pool[rem[i].specIndex].isFull)
            order.push_back((int)i);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const int za = sizeIndex(pool[rem[a].specIndex].size);
        const int zb = sizeIndex(pool[rem[b].specIndex].size);
        if (za != zb) return za > zb;
        return pool[rem[a].specIndex].outline.bbox.w > pool[rem[b].specIndex].outline.bbox.w;
    });
}

/** First-HAF X-mid only: AABB overlay vs the two bodies (true-shape may still clear). */
static bool hafCenterAabbHitsBodies(const Piece& slv, const Piece& L, const Piece& R) {
    const double eps = 0.02;
    auto hit = [&](const BBox& b) -> bool {
        return slv.bbox.x < b.r - eps && slv.bbox.r > b.x + eps
            && slv.bbox.y < b.b - eps && slv.bbox.b > b.y + eps;
    };
    return hit(L.bbox) || hit(R.bbox);
}

/** Mid-X, 0° then 180°, from the row top. Largest-first. wantFull = FULL vs HAF.
 * pinTop = first body row of every DOC: stay in-row (top-align). Else FULL may sit above the row band.
 * Caller drops the row if the previous DOC forces a lower sit. Later-row FULL climb
 * stops at this DOC's first row (climbFloor). First HAF at X-mid: reject if AABB
 * overlays a body (caller then uses armhole). FULL: no AABB gate. */
static bool trySitMidXLargest(const Piece& L, const Piece& R, double band0, double band1,
                              const std::vector<Piece>& pinObs, const std::vector<Piece>& placed,
                              std::vector<RemSleeve>& rem, const std::vector<SleeveSpec>& pool,
                              bool wantFull, bool pinTop, double minGap, double dia, Piece& out, int& hitI,
                              double climbFloor = 0) {
    std::vector<int> order;
    if (wantFull) fullIdxLargestFirst(order, rem, pool);
    else hafIdxLargestFirst(order, rem, pool);
    const double rots[2] = { 0, 180 };
    const bool clipRow = !wantFull || pinTop;
    for (size_t k = 0; k < order.size(); k++) {
        const int i = order[k];
        const SleeveSpec& spec = pool[rem[i].specIndex];
        bool midSatAabb = false;
        for (int ri = 0; ri < 2; ri++) {
            if (scanMidX(spec, rem[i].specIndex, L, R, band0, band1, rots[ri], true,
                         pinObs, placed, minGap, dia, out, clipRow, climbFloor)) {
                if (!wantFull && hafCenterAabbHitsBodies(out, L, R)) {
                    midSatAabb = true;
                    continue;
                }
                hitI = i;
                return true;
            }
        }
        /* First HAF sat at X-mid but box-hit a body — armhole path, not a smaller center. */
        if (!wantFull && midSatAabb) return false;
    }
    return false;
}

static void collectRowMidX(const BodyRow& row, const std::vector<Piece>& placed, std::vector<int>& idxs) {
    idxs.clear();
    if (row.pieces.size() != 2) return;
    const Piece* L = &row.pieces[0];
    const Piece* R = &row.pieces[1];
    if (L->bbox.x > R->bbox.x) std::swap(L, R);
    for (size_t i = 0; i < placed.size(); i++) {
        const Piece& s = placed[i];
        if (!s.midX || !s.armPair) continue;
        const double cx = (s.bbox.x + s.bbox.r) * 0.5;
        if (cx < L->bbox.r - 0.8 || cx > R->bbox.x + 0.8) continue;
        /* FULL may have climbed into the row above — still this gap. */
        if (s.bbox.b < row.y0 - 1.5 || s.bbox.y > row.y1 + 0.2) continue;
        idxs.push_back((int)i);
    }
    std::sort(idxs.begin(), idxs.end(), [&](int a, int b) { return placed[a].bbox.y < placed[b].bbox.y; });
}

/**
 * Mid-X pieces on this 2-body row: press up, then leftover FULL (Y-stack) then leftover HAF.
 * Large → small. Hole too small for FULL → HAF.
 */
static void compactOneMidXRow(BodyRow& row, std::vector<Piece>& bodies, std::vector<Piece>& placed,
                              std::vector<RemSleeve>& hafRem, std::vector<RemSleeve>& fullRem,
                              const std::vector<SleeveSpec>& pool, double minGap, double dia,
                              bool pinTop = false, double climbFloor = 0) {
    if (row.pieces.size() != 2) return;
    /* Armhole path: seated top/bottom stay pinned — do not climb or restack. */
    if (!row.hafCenter && !row.fullGap) return;
    std::vector<int> mid;
    collectRowMidX(row, placed, mid);
    if (mid.empty()) return;

    for (size_t k = 0; k < mid.size(); k++) {
        if (placed[mid[k]].freezeTop) continue; /* DOC-first mid stays top-aligned. */
        std::vector<const Poly*> blk = refs(bodies);
        for (size_t p = 0; p < placed.size(); p++) {
            if ((int)p == mid[k]) continue;
            blk.push_back(&placed[p]);
        }
        const double floorY = (pinTop || !placed[mid[k]].isFull) ? row.y0 : climbFloor;
        pressPieceUp(placed[mid[k]], blk, minGap, floorY);
    }

    const Piece* L = &row.pieces[0];
    const Piece* R = &row.pieces[1];
    if (L->bbox.x > R->bbox.x) std::swap(L, R);

    auto fillKind = [&](bool wantFull) -> void {
        std::vector<RemSleeve>& rem = wantFull ? fullRem : hafRem;
        bool progressed = true;
        while (progressed && !rem.empty()) {
            progressed = false;
            collectRowMidX(row, placed, mid);
            double yLo = row.y0;
            for (size_t k = 0; k < mid.size(); k++) {
                if (placed[mid[k]].bbox.b + minGap > yLo) yLo = placed[mid[k]].bbox.b + minGap;
            }
            const double pocket = row.y1 - yLo;
            if (pocket <= minGap + 0.05) break;

            std::vector<int> order;
            if (wantFull) fullIdxLargestFirst(order, rem, pool);
            else hafIdxLargestFirst(order, rem, pool);
            for (size_t oi = 0; oi < order.size(); oi++) {
                const int i = order[oi];
                const SleeveSpec& spec = pool[rem[i].specIndex];
                const double h = spec.outline.bbox.h;
                if (h <= 0 || yLo + h > row.y1 + 0.12) continue;
                Piece t;
                bool sat = false;
                for (double y = yLo; y + h <= row.y1 + 0.04 + 1e-9; y += 0.16) {
                    if (sitMidX(spec, rem[i].specIndex, *L, *R, y, 0,
                                row.y0, row.y1, bodies, placed, minGap, dia, t)
                        || sitMidX(spec, rem[i].specIndex, *L, *R, y, 180,
                                   row.y0, row.y1, bodies, placed, minGap, dia, t)) {
                        sat = true;
                        break;
                    }
                }
                if (!sat) continue;
                std::vector<const Poly*> blk = refs2(bodies, placed);
                const double floorY = (pinTop || !wantFull) ? (wantFull ? row.y0 : (yLo - 0.02)) : climbFloor;
                pressPieceUp(t, blk, minGap, floorY);
                t.armPair = true;
                placed.push_back(std::move(t));
                rem.erase(rem.begin() + i);
                progressed = true;
                break;
            }
        }
    };
    fillKind(true);
    fillKind(false);
}

/** Top 180° at one Y: X-mid (AABB-clean) else Body-2 armhole. Largest remaining HAF first. */
static bool trySitTopHafY(const Piece& L, const Piece& R, double y, double band0, double band1,
                          const std::vector<Piece>& pinObs, const std::vector<Piece>& placed,
                          std::vector<RemSleeve>& hafRem, const std::vector<SleeveSpec>& pool,
                          double minGap, double dia, Piece& out, int& topI, int& cap) {
    std::vector<int> order;
    hafIdxLargestFirst(order, hafRem, pool);
    for (size_t k = 0; k < order.size(); k++) {
        const int i = order[k];
        const SleeveSpec& spec = pool[hafRem[i].specIndex];
        const bool midOk = sitMidX(spec, hafRem[i].specIndex, L, R, y, 180, band0, band1,
                                   pinObs, placed, minGap, dia, out)
            && !hafCenterAabbHitsBodies(out, L, R);
        if (midOk
            || sitCenterSlv(spec, hafRem[i].specIndex, L, R, y, 180, band0, band1,
                            pinObs, placed, minGap, dia, out)) {
            topI = i;
            cap = sizeIndex(spec.size);
            return true;
        }
    }
    return false;
}

/** Armhole tuck only — do not walk the 180° into the bottom 0° pocket. */
static bool trySitTopHafWalk(const Piece& L, const Piece& R, double band0, double band1,
                             const std::vector<Piece>& pinObs, const std::vector<Piece>& placed,
                             std::vector<RemSleeve>& hafRem, const std::vector<SleeveSpec>& pool,
                             double minGap, double dia, Piece& out, int& topI, int& cap) {
    const double yMax = band0 + 0.64;
    for (double y = band0 + 0.16; y <= yMax + 1e-9; y += 0.16) {
        if (trySitTopHafY(L, R, y, band0, band1, pinObs, placed, hafRem, pool, minGap, dia, out, topI, cap))
            return true;
    }
    return false;
}

static void pinObsFrom(const std::vector<Piece>& bodies, const BodyRow& row,
                       const std::vector<Piece>& trial, std::vector<Piece>& above,
                       std::vector<Piece>& pinObs, const Piece*& L, const Piece*& R,
                       double& band0, double& band1) {
    above.clear();
    for (size_t b = 0; b < bodies.size(); b++) {
        if (!sameRowPiece(bodies[b], row)) above.push_back(bodies[b]);
    }
    L = &trial[0];
    R = &trial[1];
    if (L->bbox.x > R->bbox.x) std::swap(L, R);
    band0 = std::min(trial[0].bbox.y, trial[1].bbox.y);
    band1 = std::max(trial[0].bbox.b, trial[1].bbox.b);
    pinObs = above;
    pinObs.push_back(trial[0]);
    pinObs.push_back(trial[1]);
}

/**
 * DOC-first mid-X: sit as high as these two bodies allow. If the row above / previous DOC
 * forces a lower sit, drop this whole 2-body row and retry — do not walk the sleeve down
 * the pocket (same idea as armhole top-align). Other 2-body rows: unchanged climb/scan.
 */
static bool tryMidXDocFirst(const Piece*& L, const Piece*& R, double& band0, double& band1,
                            std::vector<Piece>& trial, std::vector<Piece>& above,
                            std::vector<Piece>& pinObs, const BodyRow& row,
                            const std::vector<Piece>& bodies, const std::vector<Piece>& placed,
                            std::vector<RemSleeve>& rem, const std::vector<SleeveSpec>& pool,
                            bool wantFull, bool pinTop, double minGap, double dia,
                            Piece& midP, int& hitI, double climbFloor = 0) {
    hitI = -1;
    if (!pinTop) {
        return trySitMidXLargest(*L, *R, band0, band1, pinObs, placed, rem, pool,
                                 wantFull, false, minGap, dia, midP, hitI, climbFloor);
    }
    std::vector<Piece> rowOnly;
    rowOnly.push_back(*L);
    rowOnly.push_back(*R);
    std::vector<Piece> none;
    Piece ghost;
    int gI = -1;
    const bool ghostOk = trySitMidXLargest(*L, *R, band0, band1, rowOnly, none, rem, pool,
                                           wantFull, true, minGap, dia, ghost, gI);
    hitI = -1;
    const bool realOk = trySitMidXLargest(*L, *R, band0, band1, pinObs, placed, rem, pool,
                                          wantFull, true, minGap, dia, midP, hitI);
    if (realOk && (!ghostOk || midP.bbox.y <= ghost.bbox.y + 0.25))
        return true;
    if (!ghostOk)
        return realOk;

    std::vector<Piece> group = trial;
    group.push_back(ghost);
    const std::vector<const Poly*> blockers = refs2(above, placed);
    const double dy = dyDownToClear(group, blockers, minGap);
    if (dy > 1e-4 && dy < 9.5) {
        for (size_t i = 0; i < trial.size(); i++) movePiece(trial[i], 0, dy);
        pinObsFrom(bodies, row, trial, above, pinObs, L, R, band0, band1);
        hitI = -1;
        if (trySitMidXLargest(*L, *R, band0, band1, pinObs, placed, rem, pool,
                              wantFull, true, minGap, dia, midP, hitI))
            return true;
    }
    return realOk;
}

/**
 * Rule 2 pass 1: FULL at mid-X first (large, one stack along Y — not HAF top+bottom).
 * Else one top HAF (Body-2 armhole). Still fail → Body-1 180→0, then FULL mid-X else HAF mid-X.
 */
static void tryTwoBodyTopSleeve(BodyRow& row, size_t rowIndex, std::vector<BodyRow>& bodyRows,
                                std::vector<Piece>& bodies, std::vector<Piece>& placed,
                                std::vector<RemSleeve>& hafRem, std::vector<RemSleeve>& fullRem,
                                const std::vector<SleeveSpec>& pool,
                                double minGap, double dia) {
    if (row.pieces.size() != 2) return;
    if (rowShiftLocked(row, placed)) return;
    if (hafRem.empty() && fullRem.empty()) return;
    const bool pinTop = docFirstBodyRow(rowIndex);
    const double climbFloor = docFirstRowY0(rowIndex, bodyRows);

    std::vector<Piece> trial = row.pieces;
    if (!forcePinTwoBodies(trial, row.y0, dia, 180)) return;

    std::vector<Piece> above, pinObs;
    const Piece* L = nullptr;
    const Piece* R = nullptr;
    double band0 = 0, band1 = 0;
    pinObsFrom(bodies, row, trial, above, pinObs, L, R, band0, band1);

    auto commitPin = [&](bool flipped) {
        const double oldY1 = row.y1;
        row.pieces = trial;
        row.y0 = rowY0(trial);
        row.y1 = rowY1(trial);
        row.splitPin = true;
        row.pinFlipped = flipped;
        bodyRows[rowIndex] = row;
        writeBackBodies(bodies, trial);
        shiftRowsBelow(bodyRows, bodies, placed, rowIndex, oldY1, row.y1 - oldY1);
        L = &row.pieces[0]; R = &row.pieces[1];
        if (L->bbox.x > R->bbox.x) std::swap(L, R);
        band0 = row.y0;
        band1 = row.y1;
    };

    Piece midP;
    int hitI = -1;
    if (tryMidXDocFirst(L, R, band0, band1, trial, above, pinObs, row, bodies, placed,
                        fullRem, pool, true, pinTop, minGap, dia, midP, hitI, climbFloor)) {
        commitPin(false);
        row.fullGap = true;
        row.hafCap = -1;
        bodyRows[rowIndex] = row;
        midP.armPair = true;
        if (pinTop) midP.freezeTop = true;
        placed.push_back(std::move(midP));
        fullRem.erase(fullRem.begin() + hitI);
        compactOneMidXRow(row, bodies, placed, hafRem, fullRem, pool, minGap, dia, pinTop, climbFloor);
        bodyRows[rowIndex] = row;
        return;
    }
    /* 180° true-gap miss + leftover FULL: flip 0° and retry FULL before any HAF.
     * No FULL in the job → skip, HAF fallback unchanged. */
    if (!fullRem.empty()) {
        if (forcePinTwoBodies(trial, row.y0, dia, 0)) {
            pinObsFrom(bodies, row, trial, above, pinObs, L, R, band0, band1);
            hitI = -1;
            if (tryMidXDocFirst(L, R, band0, band1, trial, above, pinObs, row, bodies, placed,
                                fullRem, pool, true, pinTop, minGap, dia, midP, hitI, climbFloor)) {
                commitPin(true);
                row.fullGap = true;
                row.hafCap = -1;
                bodyRows[rowIndex] = row;
                midP.armPair = true;
                if (pinTop) midP.freezeTop = true;
                placed.push_back(std::move(midP));
                fullRem.erase(fullRem.begin() + hitI);
                compactOneMidXRow(row, bodies, placed, hafRem, fullRem, pool, minGap, dia, pinTop, climbFloor);
                bodyRows[rowIndex] = row;
                return;
            }
            if (!forcePinTwoBodies(trial, row.y0, dia, 180)) return;
            pinObsFrom(bodies, row, trial, above, pinObs, L, R, band0, band1);
        }
    }

    Piece topP;
    int topI = -1, cap = -1;
    bool flipped = false;
    auto seatTop = [&]() -> bool {
        topI = -1;
        cap = -1;
        if (trySitTopHafY(*L, *R, band0, band0, band1, pinObs, placed, hafRem, pool,
                          minGap, dia, topP, topI, cap))
            return true;
        /* Top-align fits this row's bodies, but the row above blocks → drop the
         * whole current row and retry top-align. Do not walk the sleeve down. */
        std::vector<Piece> rowOnly;
        rowOnly.push_back(*L);
        rowOnly.push_back(*R);
        std::vector<Piece> none;
        Piece ghost;
        int gI = -1, gCap = -1;
        if (trySitTopHafY(*L, *R, band0, band0, band1, rowOnly, none, hafRem, pool,
                          minGap, dia, ghost, gI, gCap)) {
            std::vector<Piece> group = trial;
            group.push_back(ghost);
            const std::vector<const Poly*> blockers = refs2(above, placed);
            const double dy = dyDownToClear(group, blockers, minGap);
            if (dy > 1e-4 && dy < 9.5) {
                for (size_t i = 0; i < trial.size(); i++) movePiece(trial[i], 0, dy);
                pinObsFrom(bodies, row, trial, above, pinObs, L, R, band0, band1);
                if (trySitTopHafY(*L, *R, band0, band0, band1, pinObs, placed, hafRem, pool,
                                  minGap, dia, topP, topI, cap))
                    return true;
            }
        }
        /* This row's armhole blocks top-align — short tuck only, not into the pocket. */
        return trySitTopHafWalk(*L, *R, band0, band1, pinObs, placed, hafRem, pool,
                                minGap, dia, topP, topI, cap);
    };
    bool sat = seatTop();
    if (!sat) {
        if (!forcePinTwoBodies(trial, row.y0, dia, 0)) return;
        flipped = true;
        pinObsFrom(bodies, row, trial, above, pinObs, L, R, band0, band1);
        hitI = -1;
        if (tryMidXDocFirst(L, R, band0, band1, trial, above, pinObs, row, bodies, placed,
                            fullRem, pool, true, pinTop, minGap, dia, midP, hitI, climbFloor)) {
            commitPin(true);
            row.fullGap = true;
            row.hafCap = -1;
            bodyRows[rowIndex] = row;
            midP.armPair = true;
            if (pinTop) midP.freezeTop = true;
            placed.push_back(std::move(midP));
            fullRem.erase(fullRem.begin() + hitI);
            compactOneMidXRow(row, bodies, placed, hafRem, fullRem, pool, minGap, dia, pinTop, climbFloor);
            bodyRows[rowIndex] = row;
            return;
        }
        hitI = -1;
        if (tryMidXDocFirst(L, R, band0, band1, trial, above, pinObs, row, bodies, placed,
                            hafRem, pool, false, pinTop, minGap, dia, midP, hitI, climbFloor)) {
            commitPin(true);
            row.fullGap = false;
            row.hafCap = sizeIndex(pool[hafRem[hitI].specIndex].size);
            bodyRows[rowIndex] = row;
            midP.armPair = true;
            if (pinTop) midP.freezeTop = true;
            placed.push_back(std::move(midP));
            hafRem.erase(hafRem.begin() + hitI);
            compactOneMidXRow(row, bodies, placed, hafRem, fullRem, pool, minGap, dia, pinTop, climbFloor);
            bodyRows[rowIndex] = row;
            return;
        }
        sat = seatTop();
        if (!sat) return;
    }

    commitPin(flipped);
    row.hafCap = cap;
    bodyRows[rowIndex] = row;

    if (flipped) {
        if (topI >= 0) {
            topP.armPair = true;
            placed.push_back(std::move(topP));
            hafRem.erase(hafRem.begin() + topI);
        }
        return;
    }

    if (topI < 0) return;
    topP.armPair = true;
    const bool center = topP.midX;
    if (pinTop && center) topP.freezeTop = true;
    placed.push_back(std::move(topP));
    hafRem.erase(hafRem.begin() + topI);
    if (center) {
        row.hafCenter = true;
        bodyRows[rowIndex] = row;
        compactOneMidXRow(row, bodies, placed, hafRem, fullRem, pool, minGap, dia, pinTop, climbFloor);
        bodyRows[rowIndex] = row;
    }
}

/**
 * Bottom 0° — Body-1 armhole, X toward Body-2. Hem-align first, then walk up.
 * Stay where it first sits (no press toward the top). Tight miss → row 180° trick.
 */
static bool trySitBottom0Once(const SleeveSpec& spec, int specIndex,
                              const Piece& L, const Piece& R,
                              double rowY0, double rowY1,
                              const std::vector<Piece>& bodies, const std::vector<Piece>& placed,
                              double minGap, double dia, Piece& out) {
    return scanMidX(spec, specIndex, L, R, rowY0, rowY1, 0, false,
                    bodies, placed, minGap, dia, out)
        || scanCenterSlv(spec, specIndex, L, R, rowY0, rowY1, 0, false,
                         bodies, placed, minGap, dia, out, true);
}

static void rotatePiece180Around(Piece& p, double cx, double cy) {
    for (size_t i = 0; i < p.points.size(); i++) {
        p.points[i].x = 2.0 * cx - p.points[i].x;
        p.points[i].y = 2.0 * cy - p.points[i].y;
    }
    p.bbox = bboxOf(p.points);
    p.rotation += 180.0;
    if (p.rotation >= 360.0 - 1e-9) p.rotation -= 360.0;
}

static int rowTopArmHafIndex(const BodyRow& row, const Piece& R, const std::vector<Piece>& placed) {
    int best = -1;
    for (size_t i = 0; i < placed.size(); i++) {
        const Piece& s = placed[i];
        if (!s.armPair || s.isFull) continue;
        const double cy = (s.bbox.y + s.bbox.b) * 0.5;
        if (cy < row.y0 - 0.2 || cy > row.y1 + 0.2) continue;
        if (s.bbox.x > R.bbox.x + 0.5) continue;
        if (best < 0 || s.bbox.y < placed[best].bbox.y) best = (int)i;
    }
    return best;
}

/**
 * Tight pocket: one sleeve sat (top 180°), bottom 0° did not.
 * Remember the top sleeve's X/Y, rotate the whole row 180°, that sleeve is now
 * the bottom 0°. Place a new 180° HAF (same size, else smaller) at the remembered
 * mark — the fabric spot the first sleeve just left. True-gap checked.
 */
static bool applyRow180Trick(BodyRow& row, size_t rowIndex, std::vector<BodyRow>& bodyRows,
                             std::vector<Piece>& bodies, std::vector<Piece>& placed,
                             std::vector<RemSleeve>& hafRem, const std::vector<SleeveSpec>& pool,
                             double minGap, double dia) {
    if (row.pieces.size() != 2 || row.pinFlipped || hafRem.empty() || row.hafCap < 0)
        return false;
    const Piece* L = &row.pieces[0];
    const Piece* R = &row.pieces[1];
    if (L->bbox.x > R->bbox.x) std::swap(L, R);
    const int topIdx = rowTopArmHafIndex(row, *R, placed);
    if (topIdx < 0) return false;

    std::vector<int> order;
    hafIdxLargestFirst(order, hafRem, pool);
    int pick = -1;
    for (size_t k = 0; k < order.size(); k++) {
        if (sizeIndex(pool[hafRem[order[k]].specIndex].size) <= row.hafCap) {
            pick = order[k];
            break;
        }
    }
    if (pick < 0) return false;

    const double markX = placed[topIdx].bbox.x;
    const double markY = placed[topIdx].bbox.y;
    const std::vector<Piece> snapRow = row.pieces;
    const Piece snapTop = placed[topIdx];
    const double snapY0 = row.y0, snapY1 = row.y1;

    /* Rotate around the two bodies only — including the top sleeve in the
     * center pulls cy up/down and drives this row into the neighbor row. */
    double x0 = row.pieces[0].bbox.x, y0 = row.pieces[0].bbox.y;
    double x1 = row.pieces[0].bbox.r, y1 = row.pieces[0].bbox.b;
    for (size_t i = 1; i < row.pieces.size(); i++) {
        if (row.pieces[i].bbox.x < x0) x0 = row.pieces[i].bbox.x;
        if (row.pieces[i].bbox.y < y0) y0 = row.pieces[i].bbox.y;
        if (row.pieces[i].bbox.r > x1) x1 = row.pieces[i].bbox.r;
        if (row.pieces[i].bbox.b > y1) y1 = row.pieces[i].bbox.b;
    }
    const double cx = (x0 + x1) * 0.5;
    const double cy = (y0 + y1) * 0.5;

    const double oldY1 = row.y1;
    rotatePiece180Around(row.pieces[0], cx, cy);
    rotatePiece180Around(row.pieces[1], cx, cy);
    rotatePiece180Around(placed[topIdx], cx, cy);

    double minX = std::min(row.pieces[0].bbox.x, row.pieces[1].bbox.x);
    minX = std::min(minX, placed[topIdx].bbox.x);
    double maxX = std::max(row.pieces[0].bbox.r, row.pieces[1].bbox.r);
    maxX = std::max(maxX, placed[topIdx].bbox.r);
    double dx = 0;
    if (minX < -0.02) dx = -minX;
    if (maxX + dx > dia + 0.02) dx = dia - maxX;
    if (std::fabs(dx) > 1e-4) {
        movePiece(row.pieces[0], dx, 0);
        movePiece(row.pieces[1], dx, 0);
        movePiece(placed[topIdx], dx, 0);
    }

    row.y0 = rowY0(row.pieces);
    row.y1 = rowY1(row.pieces);
    const double appliedDy = row.y1 - oldY1;
    bodyRows[rowIndex] = row;
    writeBackBodies(bodies, row.pieces);
    shiftRowsBelow(bodyRows, bodies, placed, rowIndex, oldY1, appliedDy);

    const SleeveSpec& spec = pool[hafRem[pick].specIndex];
    auto obs = refs2(bodies, placed);
    Piece neu = placePiece(spec.outline, markX, markY, 180, spec.flip);
    stampSleeve(neu, spec, hafRem[pick].specIndex, 180);
    bool sat = canSit(neu, obs, minGap, dia);
    if (!sat) {
        L = &row.pieces[0];
        R = &row.pieces[1];
        if (L->bbox.x > R->bbox.x) std::swap(L, R);
        sat = sitCenterSlv(spec, hafRem[pick].specIndex, *L, *R, markY, 180,
                           row.y0, row.y1, bodies, placed, minGap, dia, neu)
              || scanCenterSlv(spec, hafRem[pick].specIndex, *L, *R, row.y0, row.y1, 180, true,
                               bodies, placed, minGap, dia, neu);
    }
    if (!sat) {
        row.pieces = snapRow;
        row.y0 = snapY0;
        row.y1 = snapY1;
        placed[topIdx] = snapTop;
        bodyRows[rowIndex] = row;
        writeBackBodies(bodies, row.pieces);
        shiftRowsBelow(bodyRows, bodies, placed, rowIndex, oldY1, -appliedDy);
        return false;
    }
    neu.armPair = true;
    placed.push_back(std::move(neu));
    hafRem.erase(hafRem.begin() + pick);
    unpinOverlayRows(bodyRows, placed);
    separateOverlappingBodyRows(bodyRows, bodies, placed, minGap);
    row = bodyRows[rowIndex];
#if PF_DEV_TRIALS
    PF_GEOM(placed.back(), 180, true, "ok", true);
#endif
    return true;
}

/**
 * Rule 2 pass 2a — bottom 0° hem-first then walk up (same size first, else smaller).
 * If top sat and bottom did not, rotate the row 180° and sit a new 180° at the mark.
 */
static void fillTwoBodyBottom(BodyRow& row, size_t rowIndex, std::vector<BodyRow>& bodyRows,
                              std::vector<Piece>& bodies, std::vector<Piece>& placed,
                              std::vector<RemSleeve>& hafRem, const std::vector<SleeveSpec>& pool,
                              double minGap, double dia) {
    if (row.pieces.size() != 2 || !row.splitPin || row.pinFlipped || row.fullGap
        || row.hafCenter || row.hafCap < 0 || hafRem.empty())
        return;
    const Piece* L = &row.pieces[0];
    const Piece* R = &row.pieces[1];
    if (L->bbox.x > R->bbox.x) std::swap(L, R);

    std::vector<int> order;
    hafIdxLargestFirst(order, hafRem, pool);
    for (size_t k = 0; k < order.size(); k++) {
        const int botI = order[k];
        const SleeveSpec& spec = pool[hafRem[botI].specIndex];
        if (sizeIndex(spec.size) > row.hafCap) continue;
        Piece botP;
        if (!trySitBottom0Once(spec, hafRem[botI].specIndex, *L, *R,
                               row.y0, row.y1, bodies, placed, minGap, dia, botP)) {
#if PF_DEV_TRIALS
            Piece ghost = placePiece(spec.outline, std::max(0.0, L->bbox.r - spec.outline.bbox.w * 0.22),
                                     row.y1 - spec.outline.bbox.h, 0, spec.flip);
            stampSleeve(ghost, spec, hafRem[botI].specIndex, 0);
            PF_GEOM(ghost, 0, false, "bot-0", true);
#endif
            continue;
        }
        botP.armPair = true;
        placed.push_back(std::move(botP));
        hafRem.erase(hafRem.begin() + botI);
        return;
    }
    applyRow180Trick(row, rowIndex, bodyRows, bodies, placed, hafRem, pool, minGap, dia);
}

static bool rowArmHafPair(const BodyRow& row, const Piece& R, const std::vector<Piece>& placed,
                          const Piece*& topSl, const Piece*& botSl) {
    topSl = nullptr;
    botSl = nullptr;
    for (size_t i = 0; i < placed.size(); i++) {
        const Piece& s = placed[i];
        if (!s.armPair || s.isFull) continue;
        const double cy = (s.bbox.y + s.bbox.b) * 0.5;
        if (cy < row.y0 - 0.2 || cy > row.y1 + 0.2) continue;
        if (s.bbox.x > R.bbox.x + 0.5) continue;
        if (!topSl || s.bbox.y < topSl->bbox.y) topSl = &s;
        if (!botSl || s.bbox.b > botSl->bbox.b) botSl = &s;
    }
    return topSl && botSl && topSl != botSl;
}

/**
 * Mid HAF sits only in the vertical pocket between the two arm HAFs.
 * True-gap (AABB pocket may be 0). Does not scan the whole body row.
 */
static bool trySitMidInPocket(const SleeveSpec& spec, int specIndex,
                              const Piece& L, const Piece& R,
                              const Piece& topSl, const Piece& botSl,
                              double rowY0, double rowY1,
                              const std::vector<Piece>& bodies, const std::vector<Piece>& placed,
                              double minGap, double dia, Piece& out) {
    const double h = spec.outline.bbox.h;
    if (h <= 0) return false;
    const double cyTop = (topSl.bbox.y + topSl.bbox.b) * 0.5;
    const double cyBot = (botSl.bbox.y + botSl.bbox.b) * 0.5;
    const double lo = std::min(cyTop, cyBot);
    const double hi = std::max(cyTop, cyBot);
    const double midY = (cyTop + cyBot) * 0.5 - h * 0.5;

    auto inPocket = [&](const Piece& p) -> bool {
        const double cy = (p.bbox.y + p.bbox.b) * 0.5;
        if (cy < lo - 0.8 || cy > hi + 0.8) return false;
        if (pairTooClose(p, topSl, minGap) || pairTooClose(p, botSl, minGap)) return false;
        return true;
    };

    const double rots[2] = { 0, 180 };
    for (int ri = 0; ri < 2; ri++) {
        if (sitMidX(spec, specIndex, L, R, midY, rots[ri],
                    rowY0, rowY1, bodies, placed, minGap, dia, out)
            && inPocket(out))
            return true;
    }
    for (int ri = 0; ri < 2; ri++) {
        if (sitCenterSlv(spec, specIndex, L, R, midY, rots[ri],
                         rowY0, rowY1, bodies, placed, minGap, dia, out)
            && inPocket(out))
            return true;
    }

    const double yLo = std::max(rowY0 - 0.08, lo - h * 0.5 - 0.5);
    const double yHi = std::min(rowY1 + 0.12 - h, hi - h * 0.5 + 0.5);
    if (yHi < yLo - 1e-4) return false;
    for (int ri = 0; ri < 2; ri++) {
        for (double y = yLo; y <= yHi + 1e-6; y += 0.12) {
            if (std::fabs(y - midY) < 0.06) continue;
            if (sitCenterSlv(spec, specIndex, L, R, y, rots[ri],
                             rowY0, rowY1, bodies, placed, minGap, dia, out)
                && inPocket(out))
                return true;
        }
    }
    return false;
}

/**
 * Rule 2 pass 2b: after top + bottom both sat, mid of the vertical pocket.
 * FULL first (large → small), else HAF large → small. 0° then 180°.
 */
static bool fillTwoBodyMid(BodyRow& row, int rowIndex, std::vector<Piece>& bodies,
                           std::vector<Piece>& placed,
                           std::vector<RemSleeve>& hafRem, std::vector<RemSleeve>& fullRem,
                           const std::vector<SleeveSpec>& pool,
                           double minGap, double dia) {
#if !PF_DEV_TRIALS
    (void)rowIndex;
#endif
    if (row.pieces.size() != 2 || !row.splitPin || row.pinFlipped || row.fullGap || row.hafCenter)
        return false;
    if (hafRem.empty() && fullRem.empty()) return false;
    const Piece* L = &row.pieces[0];
    const Piece* R = &row.pieces[1];
    if (L->bbox.x > R->bbox.x) std::swap(L, R);
    const Piece* topSl = nullptr;
    const Piece* botSl = nullptr;
    if (!rowArmHafPair(row, *R, placed, topSl, botSl)) return false;

    const double cyTop = (topSl->bbox.y + topSl->bbox.b) * 0.5;
    const double cyBot = (botSl->bbox.y + botSl->bbox.b) * 0.5;

    auto tryList = [&](std::vector<RemSleeve>& rem, bool wantFull) -> bool {
        (void)wantFull;
        std::vector<int> order;
        if (wantFull) fullIdxLargestFirst(order, rem, pool);
        else hafIdxLargestFirst(order, rem, pool);
        for (size_t k = 0; k < order.size(); k++) {
            const int midI = order[k];
            const SleeveSpec& spec = pool[rem[midI].specIndex];
#if PF_DEV_TRIALS
            PF_CTX("haf-2body-mid", rowIndex, spec.size, spec.label);
#else
            (void)wantFull;
#endif
            Piece midP;
            if (trySitMidInPocket(spec, rem[midI].specIndex, *L, *R, *topSl, *botSl,
                                  row.y0, row.y1, bodies, placed, minGap, dia, midP)) {
                midP.armPair = true;
                placed.push_back(std::move(midP));
                rem.erase(rem.begin() + midI);
                return true;
            }
#if PF_DEV_TRIALS
            {
                const double h = spec.outline.bbox.h;
                const double midY = (cyTop + cyBot) * 0.5 - h * 0.5;
                Piece ghost = placePiece(spec.outline, std::max(0.0, R->bbox.x - spec.outline.bbox.w),
                                         midY, 0, spec.flip);
                stampSleeve(ghost, spec, rem[midI].specIndex, 0);
                PF_GEOM(ghost, 0, false, "mid-gap", true);
            }
#endif
        }
        return false;
    };
    if (tryList(fullRem, true) || tryList(hafRem, false)) {
        if (!rowHasOverlay(row, placed))
            row.layoutPin = true;
        return true;
    }
    return false;
}

/**
 * Mid-X rows: press up, leftover FULL then leftover HAF (large → small).
 */
static void compactMidXAndFill(std::vector<BodyRow>& bodyRows, std::vector<Piece>& bodies,
                               std::vector<Piece>& placed, std::vector<RemSleeve>& remaining,
                               std::vector<RemSleeve>& fullRem, const std::vector<SleeveSpec>& pool,
                               double minGap, double dia) {
    for (size_t ri = 0; ri < bodyRows.size(); ri++) {
        compactOneMidXRow(bodyRows[ri], bodies, placed, remaining, fullRem, pool, minGap, dia,
                          docFirstBodyRow(ri), docFirstRowY0(ri, bodyRows));
    }
}

/**
 * Rule 6: sit one HAF inside a clipped band. Must finish at or above yHi (no row overflow).
 * nestLeft from xStart toward xFloor — first sit hugs the left of the leftover.
 */
static bool trySitHafInBand(const SleeveSpec& spec, int specIndex,
                            double xStart, double xFloor, double yLo, double yHi,
                            double clipX0, double clipX1,
                            const std::vector<Piece>& bodies, const std::vector<Piece>& placed,
                            double minGap, double dia, Piece& out) {
    const double h = spec.outline.bbox.h;
    if (h <= 0 || yHi - yLo + 0.08 < h) return false;
    auto blockers = refs2(bodies, placed);
    const double rots[2] = { 0, 180 };
    for (int ri = 0; ri < 2; ri++) {
        for (double y = yLo; y + h <= yHi + 0.04 + 1e-9; y += 0.16) {
            Piece t;
            if (!tryAddSleeve(spec, specIndex, xStart, y, xFloor, blockers, minGap, dia,
                              rots[ri], 0.14, t))
                continue;
            if (t.bbox.b > yHi + 0.04 || t.bbox.y < yLo - 0.10) continue;
            if (t.bbox.x < clipX0 - 0.40 || t.bbox.r > clipX1 + 0.40) continue;
            out = std::move(t);
            return true;
        }
    }
    return false;
}

/**
 * Rule 6: leftover HAF only — not FULL, not 2-body rows (Rule 2 owns those).
 * 1-body: fill right of the body to DIA, stay inside the row hem.
 * 3+ body: under every body shorter than the row hem, stay inside the row hem.
 */
static void fillRule6Row(const BodyRow& row, int rowIndex,
                         std::vector<RemSleeve>& hafRem, const std::vector<SleeveSpec>& pool,
                         const std::vector<Piece>& bodies, std::vector<Piece>& placed,
                         double minGap, double dia) {
#if !PF_DEV_TRIALS
    (void)rowIndex;
#endif
    if (hafRem.empty() || row.pieces.size() == 2) return;

    auto placeOne = [&](double xStart, double xFloor, double yLo, double yHi,
                        double clipX0, double clipX1, const char* pass) -> bool {
#if !PF_DEV_TRIALS
        (void)pass;
#endif
        std::vector<int> order;
        hafIdxLargestFirst(order, hafRem, pool);
        for (size_t k = 0; k < order.size(); k++) {
            const int i = order[k];
            const SleeveSpec& spec = pool[hafRem[i].specIndex];
#if PF_DEV_TRIALS
            PF_CTX(pass, rowIndex, spec.size, spec.label);
#endif
            Piece t;
            if (!trySitHafInBand(spec, hafRem[i].specIndex, xStart, xFloor, yLo, yHi,
                                 clipX0, clipX1, bodies, placed, minGap, dia, t)) {
#if PF_DEV_TRIALS
                Piece ghost = placePiece(spec.outline, std::max(0.0, xFloor),
                                         yLo, 0, spec.flip);
                stampSleeve(ghost, spec, hafRem[i].specIndex, 0);
                PF_GEOM(ghost, 0, false, pass, true);
#endif
                continue;
            }
            placed.push_back(std::move(t));
            hafRem.erase(hafRem.begin() + i);
            return true;
        }
        return false;
    };

    if (row.pieces.size() == 1) {
        const Piece& body = row.pieces[0];
        const double tuck = std::min(2.4, std::max(0.8, body.bbox.w * 0.08));
        const double xFloor = std::max(0.0, body.bbox.r - tuck);
        const double clip0 = body.bbox.r - tuck;
        bool progressed = true;
        while (progressed && !hafRem.empty()) {
            progressed = false;
            const double xStart = std::max(0.0, dia - 0.5);
            if (placeOne(xStart, xFloor, row.y0, row.y1, clip0, dia, "haf-rule6-1"))
                progressed = true;
        }
        return;
    }

    if (row.pieces.size() < 3) return;
    std::vector<const Piece*> col;
    for (size_t i = 0; i < row.pieces.size(); i++) col.push_back(&row.pieces[i]);
    std::sort(col.begin(), col.end(), [](const Piece* a, const Piece* b) {
        return a->bbox.x < b->bbox.x;
    });
    for (size_t bi = 0; bi < col.size() && !hafRem.empty(); bi++) {
        const Piece& body = *col[bi];
        /* Any leftover shorter than the row hem — even a small step. Skip only if
         * no remaining HAF can fit the pocket height. */
        const double pocket = row.y1 - body.bbox.b;
        if (pocket <= minGap + 0.05) continue;
        bool tallEnough = false;
        for (size_t hi = 0; hi < hafRem.size(); hi++) {
            const int si = hafRem[hi].specIndex;
            if (si < 0 || si >= (int)pool.size() || pool[si].isFull) continue;
            if (pool[si].outline.bbox.h <= pocket + 0.08) { tallEnough = true; break; }
        }
        if (!tallEnough) continue;
        const double yLo = body.bbox.b + minGap;
        const double yHi = row.y1;
        const double tuck = 0.45;
        const double xFloor = std::max(0.0, body.bbox.x - tuck);
        const double clip0 = body.bbox.x - 0.40;
        const double clip1 = body.bbox.r + 0.40;
        bool progressed = true;
        while (progressed && !hafRem.empty()) {
            progressed = false;
            const double xStart = std::min(std::max(0.0, dia - 0.5), body.bbox.r);
            if (placeOne(xStart, xFloor, yLo, yHi, clip0, clip1, "haf-rule6-step"))
                progressed = true;
        }
    }
}

static void placeSleeves(std::vector<SleeveSpec>& pool, std::vector<BodyRow>& bodyRows,
                         std::vector<Piece>& bodies,
                         double dia, double minGap, std::vector<Piece>& placed, std::vector<RemSleeve>& overflow) {
#if PF_DEV_TRIALS
    if (gDev.on) devBind(&bodyRows, &placed, gSnapRpd, gSnapGap);
#endif
    std::vector<RemSleeve> remaining;
    remaining.reserve(pool.size());
    std::vector<RemSleeve> fullRem;
    for (size_t i = 0; i < pool.size(); i++) {
        if (pool[i].isFull) fullRem.push_back({ (int)i });
        else remaining.push_back({ (int)i });
    }
    std::sort(remaining.begin(), remaining.end(), [&](const RemSleeve& a, const RemSleeve& b) {
        return pool[b.specIndex].outline.bbox.w < pool[a.specIndex].outline.bbox.w;
    });

    /* Rule 2: FULL mid-X first (Y-stack), else HAF top then bottom (before the next row's top). */
    for (size_t ri = 0; ri < bodyRows.size(); ri++) {
        if (bodyRows[ri].pieces.size() != 2) continue;
        if (remaining.empty() && fullRem.empty()) break;
        PF_CTX("haf-2body-top", (int)ri, "", "");
        tryTwoBodyTopSleeve(bodyRows[ri], ri, bodyRows, bodies, placed, remaining, fullRem, pool, minGap, dia);
#if PF_DEV_TRIALS
        if (gDev.on) devSnap("sit", "haf-2body-top");
#endif
        PF_CTX("haf-2body-bot", (int)ri, "", "");
        fillTwoBodyBottom(bodyRows[ri], ri, bodyRows, bodies, placed, remaining, pool, minGap, dia);
#if PF_DEV_TRIALS
        if (gDev.on) devSnap("sit", "haf-2body-bot");
#endif
    }
    unpinOverlayRows(bodyRows, placed);
    separateOverlappingBodyRows(bodyRows, bodies, placed, minGap);
    for (size_t ri = 0; ri < bodyRows.size(); ri++) {
        if (bodyRows[ri].pieces.size() != 2) continue;
        if (remaining.empty() && fullRem.empty()) break;
        PF_CTX("haf-2body-mid", (int)ri, "", "");
        const bool midTried = fillTwoBodyMid(bodyRows[ri], (int)ri, bodies, placed,
                                            remaining, fullRem, pool, minGap, dia);
#if PF_DEV_TRIALS
        if (gDev.on && midTried) devSnap("sit", "haf-2body-mid");
#endif
    }
    compactMidXAndFill(bodyRows, bodies, placed, remaining, fullRem, pool, minGap, dia);

    /* Rule 6: 1-body right HAF + 3-body under-short steps. HAF only. 2-body stays Rule 2. */
    for (size_t ri = 0; ri < bodyRows.size() && !remaining.empty(); ri++) {
        if (bodyRows[ri].pieces.size() == 2) continue;
        PF_CTX("haf-rule6", (int)ri, "", "");
        fillRule6Row(bodyRows[ri], (int)ri, remaining, pool, bodies, placed, minGap, dia);
#if PF_DEV_TRIALS
        if (gDev.on) devSnap("sit", "haf-rule6");
#endif
    }

    double bodyBot = 0;
    for (size_t b = 0; b < bodies.size(); b++) {
        if (bodies[b].bbox.b > bodyBot) bodyBot = bodies[b].bbox.b;
    }

    /* Rule 3: leftover HAF under all bodies — 180 L→R + 0 R→L, interlock 3+3 / 4+4… / 3+2 / 3+1 */
    if (!remaining.empty()) {
        PF_CTX("haf-block", -1, "", "");
        packBottomSleeveBlocks(remaining, pool, bodyBot + minGap, bodies, placed, minGap, dia);
#if PF_DEV_TRIALS
        if (gDev.on) devSnap("sit", "haf-block");
#endif
    }

    overflow = remaining;
}

static int markOverlaps(std::vector<Piece*>& pieces) {
    for (size_t i = 0; i < pieces.size(); i++) pieces[i]->overlap = false;
    for (size_t i = 0; i < pieces.size(); i++) {
        for (size_t j = i + 1; j < pieces.size(); j++) {
            if (!aabbNear(pieces[i]->bbox, pieces[j]->bbox, 0.04)) continue;
            Poly A, B;
            A.points = pieces[i]->points; A.bbox = pieces[i]->bbox;
            B.points = pieces[j]->points; B.bbox = pieces[j]->bbox;
            const bool hit = closerThan(A, B, 1e-4);
            if (hit) {
                pieces[i]->overlap = true;
                pieces[j]->overlap = true;
            }
        }
    }
    int n = 0;
    for (size_t i = 0; i < pieces.size(); i++) if (pieces[i]->overlap) n++;
    return n;
}

static const double DOC_MAX_H = 190.0;

static void pageExtents(const Page& pg, double& top, double& bot) {
    top = 1e300; bot = -1e300;
    bool any = false;
    auto acc = [&](const Piece& it) {
        any = true;
        if (it.bbox.y < top) top = it.bbox.y;
        if (it.bbox.b > bot) bot = it.bbox.b;
    };
    for (size_t i = 0; i < pg.bodies.size(); i++) acc(pg.bodies[i]);
    for (size_t i = 0; i < pg.sleeves.size(); i++) acc(pg.sleeves[i]);
    if (!any) { top = 0; bot = 0; }
}

static bool pageCanTakeBlock(const Page& pg, double blockH, double minGap) {
    double top, bot;
    pageExtents(pg, top, bot);
    const double newH = (pg.bodies.empty() && pg.sleeves.empty())
        ? blockH
        : (bot + minGap + blockH - top);
    return newH <= DOC_MAX_H + 1e-6;
}

static void finalizePageBounds(std::vector<Page>& pages) {
    for (size_t p = 0; p < pages.size(); p++) {
        Page& pg = pages[p];
        double top = 1e300, bot = -1e300, left = 1e300, right = -1e300;
        auto acc = [&](const Piece& it) {
            if (it.bbox.y < top) top = it.bbox.y;
            if (it.bbox.b > bot) bot = it.bbox.b;
            if (it.bbox.x < left) left = it.bbox.x;
            if (it.bbox.r > right) right = it.bbox.r;
        };
        for (size_t i = 0; i < pg.bodies.size(); i++) acc(pg.bodies[i]);
        for (size_t i = 0; i < pg.sleeves.size(); i++) acc(pg.sleeves[i]);
        if (pg.bodies.empty() && pg.sleeves.empty()) {
            top = pg.y0; bot = pg.y1; left = 0; right = 0;
        }
        pg.contentY0 = top;
        pg.contentY1 = bot;
        pg.height = std::max(0.0, bot - top);
        pg.width = std::max(0.0, right - left);
    }
}

struct SleeveBlockGrp {
    int id = 0;
    bool isFull = false;
    std::vector<int> idxs;
};

static void collectSleeveBlocks(const std::vector<Piece>& sleeves,
                                std::vector<SleeveBlockGrp>& haf, std::vector<SleeveBlockGrp>& full) {
    std::map<int, SleeveBlockGrp> hafM, fullM;
    for (size_t i = 0; i < sleeves.size(); i++) {
        if (sleeves[i].sleeveBlock <= 0) continue;
        auto& m = sleeves[i].isFull ? fullM : hafM;
        SleeveBlockGrp& g = m[sleeves[i].sleeveBlock];
        g.id = sleeves[i].sleeveBlock;
        g.isFull = sleeves[i].isFull;
        g.idxs.push_back((int)i);
    }
    for (auto& kv : hafM) haf.push_back(kv.second);
    for (auto& kv : fullM) full.push_back(kv.second);
}

static void attachBlockToPage(Page& pg, std::vector<Piece>& sleeves, const std::vector<int>& idxs,
                              double minGap) {
    double by0 = 1e300, by1 = -1e300;
    for (size_t i = 0; i < idxs.size(); i++) {
        const BBox& b = sleeves[idxs[i]].bbox;
        if (b.y < by0) by0 = b.y;
        if (b.b > by1) by1 = b.b;
    }
    double top, bot;
    pageExtents(pg, top, bot);
    const bool empty = pg.bodies.empty() && pg.sleeves.empty();
    const double target = empty ? 0.0 : (bot + minGap);
    const double dy = target - by0;
    for (size_t i = 0; i < idxs.size(); i++) {
        if (std::fabs(dy) > 1e-9) movePiece(sleeves[idxs[i]], 0, dy);
        pg.sleeves.push_back(sleeves[idxs[i]]);
    }
}

static int lastBodyPageIndex(const std::vector<Page>& pages) {
    int lastBody = (int)pages.size() - 1;
    for (int p = (int)pages.size() - 1; p >= 0; p--) {
        if (!pages[p].bodies.empty()) { lastBody = p; break; }
    }
    return lastBody;
}

static double grpHeight(const std::vector<Piece>& sleeves, const SleeveBlockGrp& g) {
    double y0 = 1e300, y1 = -1e300;
    for (size_t i = 0; i < g.idxs.size(); i++) {
        const BBox& b = sleeves[g.idxs[i]].bbox;
        if (b.y < y0) y0 = b.y;
        if (b.b > y1) y1 = b.b;
    }
    return std::max(0.0, y1 - y0);
}

/** True leftover HAF block: 3+3 / 4+4… or 3+2 / 3+1. */
static bool hafGrpIsInterlock(const std::vector<Piece>& sleeves, const SleeveBlockGrp& g) {
    int n180 = 0, n0 = 0;
    for (size_t i = 0; i < g.idxs.size(); i++) {
        const Piece& p = sleeves[g.idxs[i]];
        if (p.isFull) return false;
        if (std::fabs(p.rotation - 180) < 1) n180++;
        else n0++;
    }
    return (n180 >= 3 && n0 == n180) || (n180 == 3 && (n0 == 1 || n0 == 2));
}

static int findPageForHafBlock(std::vector<Page>& pages, int preferred, double blockH, double minGap) {
    const int start = std::max(0, preferred);
    for (int p = start; p < (int)pages.size(); p++) {
        if (pageCanTakeBlock(pages[p], blockH, minGap)) return p;
    }
    for (int p = 0; p < start && p < (int)pages.size(); p++) {
        if (pageCanTakeBlock(pages[p], blockH, minGap)) return p;
    }
    Page extra;
    extra.index = (int)pages.size() + 1;
    extra.rowFrom = 0;
    extra.rowTo = 0;
    extra.y0 = 0;
    extra.y1 = 0;
    pages.push_back(extra);
    return (int)pages.size() - 1;
}

static std::vector<Page> slicePages(const std::vector<BodyRow>& bodyRows, std::vector<Piece>& sleeves,
                                    int rowsPerDoc, double minGap) {
    int n = std::max(1, rowsPerDoc);
    std::vector<Page> pages;
    for (size_t i = 0; i < bodyRows.size(); i += (size_t)n) {
        Page pg;
        pg.index = (int)pages.size() + 1;
        pg.rowFrom = (int)i + 1;
        double y0 = 1e300, y1 = -1e300;
        size_t end = std::min(i + (size_t)n, bodyRows.size());
        for (size_t j = i; j < end; j++) {
            if (bodyRows[j].y0 < y0) y0 = bodyRows[j].y0;
            if (bodyRows[j].y1 > y1) y1 = bodyRows[j].y1;
            for (size_t k = 0; k < bodyRows[j].pieces.size(); k++) pg.bodies.push_back(bodyRows[j].pieces[k]);
        }
        if (!(y0 < 1e299)) { y0 = 0; y1 = 0; }
        pg.rowTo = (int)i + (int)(end - i);
        pg.y0 = y0; pg.y1 = y1;
        pages.push_back(pg);
    }
    if (pages.empty()) {
        Page pg;
        pg.index = 1;
        pages.push_back(pg);
    }

    double bodyBot = -1e300;
    for (size_t i = 0; i < bodyRows.size(); i++) {
        if (bodyRows[i].y1 > bodyBot) bodyBot = bodyRows[i].y1;
    }

    /* Side / arm / pocket sleeves stay on the body row that owns their Y.
     * Untagged leftover HAF below the bodies is not a block — last body DOC later. */
    for (size_t i = 0; i < sleeves.size(); i++) {
        Piece& sl = sleeves[i];
        if (sl.sleeveBlock) continue;
        if (!sl.isFull && sl.bbox.y >= bodyBot - 0.15) continue;
        double cy = (sl.bbox.y + sl.bbox.b) * 0.5;
        int best = 0;
        double bestOverlap = -1;
        for (size_t p = 0; p < pages.size(); p++) {
            double overlap = std::min(pages[p].y1, sl.bbox.b) - std::max(pages[p].y0, sl.bbox.y);
            if (overlap > bestOverlap) {
                bestOverlap = overlap;
                best = (int)p;
            }
            if (cy >= pages[p].y0 && cy <= pages[p].y1) {
                best = (int)p;
                bestOverlap = 1e9;
            }
        }
        pages[best].sleeves.push_back(sl);
    }

    std::vector<SleeveBlockGrp> haf, full;
    collectSleeveBlocks(sleeves, haf, full);

    int lastBody = lastBodyPageIndex(pages);

    /* Complete interlocking leftover (3+3 / 4+4 / … / 3+2 / 3+1) : block 1 → DOC 1…  Never a single row. */
    int completeI = 0;
    std::vector<int> incompleteIdxs;
    for (size_t b = 0; b < haf.size(); b++) {
        if (hafGrpIsInterlock(sleeves, haf[b])) {
            const double blockH = grpHeight(sleeves, haf[b]);
            const int dest = findPageForHafBlock(pages, completeI, blockH, minGap);
            attachBlockToPage(pages[dest], sleeves, haf[b].idxs, minGap);
            completeI += 1;
        } else {
            for (size_t i = 0; i < haf[b].idxs.size(); i++) incompleteIdxs.push_back(haf[b].idxs[i]);
        }
    }
    for (size_t i = 0; i < sleeves.size(); i++) {
        const Piece& sl = sleeves[i];
        if (sl.sleeveBlock || sl.isFull) continue;
        if (sl.bbox.y >= bodyBot - 0.15) incompleteIdxs.push_back((int)i);
    }
    if (!incompleteIdxs.empty()) {
        attachBlockToPage(pages[lastBody], sleeves, incompleteIdxs, minGap);
    }

    /* FULL leftover rows stay on the last body DOC. One dy for every FULL row so nest Y (push-up interlock) is kept. */
    if (!full.empty()) {
        lastBody = lastBodyPageIndex(pages);
        double by0 = 1e300;
        for (size_t i = 0; i < full[0].idxs.size(); i++) {
            if (sleeves[full[0].idxs[i]].bbox.y < by0) by0 = sleeves[full[0].idxs[i]].bbox.y;
        }
        double top, bot;
        pageExtents(pages[lastBody], top, bot);
        const bool empty = pages[lastBody].bodies.empty() && pages[lastBody].sleeves.empty();
        const double dy = (empty ? 0.0 : (bot + minGap)) - by0;
        for (size_t b = 0; b < full.size(); b++) {
            for (size_t i = 0; i < full[b].idxs.size(); i++) {
                if (std::fabs(dy) > 1e-9) movePiece(sleeves[full[b].idxs[i]], 0, dy);
                pages[lastBody].sleeves.push_back(sleeves[full[b].idxs[i]]);
            }
        }
    }

    finalizePageBounds(pages);
    return pages;
}

#if PF_DEV_TRIALS
static DevSnapPiece slimSnapPiece(const Piece& src) {
    DevSnapPiece d;
    d.kind = src.kind;
    d.size = src.size;
    d.label = src.label;
    d.rot = src.rotation;
    d.x = src.bbox.x;
    d.y = src.bbox.y;
    d.w = src.bbox.w;
    d.h = src.bbox.h;
    d.overlap = src.overlap;
    const size_t n = src.points.size();
    if (!n) return d;
    const int step = n > 14 ? (int)(n / 14) : 1;
    for (size_t k = 0; k < n; k += (size_t)step) d.pts.push_back(src.points[k]);
    if (d.pts.empty() || d.pts.back().x != src.points.back().x || d.pts.back().y != src.points.back().y)
        d.pts.push_back(src.points.back());
    return d;
}

void devSnap(const char* kind, const char* reason) {
    if (!gDev.on) return;
    const bool midPass = reason && std::string(reason) == "haf-2body-mid";
    if (gDev.items.size() >= (size_t)DevLog::MAX &&
        (!midPass || gDev.items.size() >= (size_t)DevLog::MAX + 24))
        return;
    const bool overlay = kind && std::string(kind) == "overlay";
    if (overlay) {
        if (gOverlaySnaps >= 12) return;
        gOverlaySnaps += 1;
    }
    DevTrial t;
    t.i = (int)gDev.items.size();
    t.pass = gDev.pass;
    t.kind = kind ? kind : "sit";
    t.reason = reason ? reason : "";
    t.size = gDev.size;
    t.label = gDev.label;
    t.row = gDev.row;
    t.ok = (t.kind == "sit" || t.kind == "commit");
    t.overlay = overlay;
    t.tries.swap(gDev.pending);
    if (gSnapRows) {
        std::vector<Piece> slv;
        if (gSnapSleeves) slv = *gSnapSleeves;
        std::vector<Page> pages = slicePages(*gSnapRows, slv, gSnapRpd, gSnapGap);
        t.pages.reserve(pages.size());
        for (size_t p = 0; p < pages.size(); p++) {
            DevSnapPage pg;
            pg.index = pages[p].index;
            pg.rowFrom = pages[p].rowFrom;
            pg.rowTo = pages[p].rowTo;
            pg.height = pages[p].height;
            pg.contentY0 = pages[p].contentY0;
            for (size_t b = 0; b < pages[p].bodies.size(); b++)
                pg.bodies.push_back(slimSnapPiece(pages[p].bodies[b]));
            for (size_t s = 0; s < pages[p].sleeves.size(); s++)
                pg.sleeves.push_back(slimSnapPiece(pages[p].sleeves[s]));
            t.pages.push_back(std::move(pg));
        }
    }
    gDev.items.push_back(std::move(t));
}
#endif

static std::string asciiUpper(std::string k) {
    for (size_t i = 0; i < k.size(); i++) {
        if (k[i] >= 'a' && k[i] <= 'z') k[i] = char(k[i] - 32);
    }
    return k;
}

static bool keyIsCust(const std::string& k) {
    const std::string u = asciiUpper(k);
    return u.size() >= 5 && u.compare(0, 5, "CUST-") == 0;
}

static std::vector<JobRow> parseJob(const Json& list, const Json& chart, int& skippedFull, int& skipped) {
    std::vector<JobRow> rows;
    skippedFull = 0; skipped = 0;
    if (!list.isArr()) return rows;
    for (size_t i = 0; i < list.a.size(); i++) {
        const Json& it = list.a[i];
        std::string size = normalizeSize(it.strVal("SIZE"));
        std::string slv = normalizeSlv(it.strVal("SLV"));
        if (size.empty() || slv.empty()) { skipped++; continue; }
        if (slv != "HAF" && slv != "FULL") { skipped++; continue; }
        double w, h;
        partDim(chart, size, "FONT_BACK", w, h);
        if (w <= 0) { skipped++; continue; }
        JobRow r;
        r.NAME = it.strVal("NAME");
        r.NUMBER = it.strVal("NUMBER");
        r.SIZE = size;
        r.SLV = slv;
        r.COMMENTS = it.strVal("COMMENTS");
        r.jobIndex = (int)rows.size();
        if (it.isObj()) {
            for (auto kv = it.o.begin(); kv != it.o.end(); ++kv) {
                if (!keyIsCust(kv->first)) continue;
                r.extra[asciiUpper(kv->first)] = kv->second.type == Json::STR
                    ? kv->second.s
                    : it.strVal(kv->first);
            }
        }
        rows.push_back(r);
    }
    return rows;
}

static std::vector<Group> groupCopies(const std::vector<JobRow>& rows, const Json& chart) {
    std::map<std::string, Group> map;
    std::vector<std::string> order;
    for (size_t i = 0; i < rows.size(); i++) {
        const std::string& s = rows[i].SIZE;
        if (!map.count(s)) {
            Group g;
            g.SIZE = s;
            partDim(chart, s, "FONT_BACK", g.width, g.height);
            map[s] = g;
            order.push_back(s);
        }
        map[s].copies.push_back(rows[i]);
    }
    std::sort(order.begin(), order.end(), [&](const std::string& a, const std::string& b) {
        double dw = map[b].width - map[a].width;
        if (std::fabs(dw) > 1e-6) return dw > 0;
        return sizeIndex(a) < sizeIndex(b);
    });
    std::vector<Group> out;
    for (size_t i = 0; i < order.size(); i++) out.push_back(map[order[i]]);
    return out;
}

static std::vector<BodyItem> buildBodyItems(const std::vector<Group>& groups, const Outline& fontM, const Outline& backM, const Json& chart) {
    std::vector<BodyItem> items;
    for (size_t g = 0; g < groups.size(); g++) {
        const Group& grp = groups[g];
        double tw, th;
        partDim(chart, grp.SIZE, "FONT_BACK", tw, th);
        if (tw <= 0) continue;
        Outline fontOl = gradeOutline(fontM, tw, th);
        Outline backOl = gradeOutline(backM, tw, th);
        if (fontOl.points.empty() || backOl.points.empty()) continue;
        for (size_t i = 0; i < grp.copies.size(); i++) {
            BodyItem f;
            f.outline = fontOl; f.flip = false; f.kind = "FRONT"; f.size = grp.SIZE;
            f.copyIndex = grp.copies[i].jobIndex; f.label = grp.SIZE + " FRONT";
            f.pairId = grp.SIZE + ":" + std::to_string(i); f.role = "FRONT"; f.w = fontOl.bbox.w;
            items.push_back(f);
            BodyItem b;
            b.outline = backOl; b.flip = false; b.kind = "BACK"; b.size = grp.SIZE;
            b.copyIndex = grp.copies[i].jobIndex; b.label = grp.SIZE + " BACK";
            b.pairId = grp.SIZE + ":" + std::to_string(i); b.role = "BACK"; b.w = backOl.bbox.w;
            items.push_back(b);
        }
    }
    std::sort(items.begin(), items.end(), [](const BodyItem& a, const BodyItem& b) { return a.w > b.w; });
    return items;
}

SimResult simulate(const Json& input) {
    auto t0 = std::chrono::steady_clock::now();
#if PF_DEV_TRIALS
    gDev.reset();
    gSnapRows = nullptr;
    gSnapSleeves = nullptr;
    gOverlaySnaps = 0;
    if (input.has("devTrials")) {
        const Json& dv = input.get("devTrials");
        if (dv.type == Json::BOOL) gDev.on = dv.b;
        else if (dv.type == Json::NUM) gDev.on = dv.n != 0;
    }
    PF_CTX("bodies", -1, "", "");
#endif
    SimResult r;
    r.dia = input.number("dia", 63);
    if (!(r.dia > 0)) r.dia = 63;
    r.minGap = input.number("minGap", 0.05);
    if (!std::isfinite(r.minGap) || r.minGap < 0) r.minGap = 0.05;
    r.rowsPerDoc = (int)input.number("rowsPerDoc", 4);
    if (r.rowsPerDoc < 1) r.rowsPerDoc = 4;
    gRowsPerDoc = r.rowsPerDoc;
#if PF_DEV_TRIALS
    gSnapRpd = r.rowsPerDoc;
    gSnapGap = r.minGap;
#endif
    r.sleeveKey = input.strVal("sleeveKey", "short_slv_without_rib");

    const Json& chart = input.get("chart");
    r.jobRows = parseJob(input.get("job"), chart, r.skippedFull, r.skipped);
    auto groups = groupCopies(r.jobRows, chart);

    const Json& masters = input.get("masters");
    if (!masters.has("font") || !masters.has("back")) {
        r.ok = false;
        r.error = "Load FRONT and BACK SVG first.";
        r.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return r;
    }
    Outline fontM = readOutline(masters.get("font"));
    Outline backM = readOutline(masters.get("back"));
    bool hasSleeve = masters.has("sleeve") && masters.get("sleeve").has("points");
    bool hasLong = masters.has("sleeveLong") && masters.get("sleeveLong").has("points");
    Outline slvM, slvLong;
    if (hasSleeve) slvM = readOutline(masters.get("sleeve"));
    if (hasLong) slvLong = readOutline(masters.get("sleeveLong"));
    if (fontM.points.empty() || backM.points.empty()) {
        r.ok = false;
        r.error = "FRONT/BACK outlines are empty.";
        r.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return r;
    }
    if ((!hasSleeve || slvM.points.empty()) && (!hasLong || slvLong.points.empty())) {
        r.warnings.push_back("No sleeve SVG — body rows only.");
    }

    auto bodyItems = buildBodyItems(groups, fontM, backM, chart);
    PackedBodies packed = packBodies(bodyItems, r.minGap, r.dia);
#if PF_DEV_TRIALS
    if (gDev.on) {
        PF_CTX("bodies", -1, "", "");
        devSnap("sit", "bodies-done");
    }
#endif

    const std::string rib = (r.sleeveKey.find("without") != std::string::npos) ? "without_rib" : "with_rib";
    const std::string hafKey = "short_slv_" + rib;
    const std::string fullKey = "long_slv_" + rib;

    std::vector<SleeveSpec> sleevePool;
    int skippedHafSlv = 0, skippedFullSlv = 0;
    for (size_t i = 0; i < r.jobRows.size(); i++) {
        const bool isFull = (r.jobRows[i].SLV == "FULL");
        const Outline& src = isFull ? slvLong : slvM;
        if (src.points.empty()) {
            if (isFull) skippedFullSlv += 1;
            else skippedHafSlv += 1;
            continue;
        }
        double tw, th;
        partDim(chart, r.jobRows[i].SIZE, isFull ? fullKey : hafKey, tw, th);
        if (tw <= 0) continue;
        Outline ol = gradeOutline(src, tw, th);
        if (ol.points.empty()) continue;
        SleeveSpec a;
        a.outline = ol; a.flip = false; a.isFull = isFull;
        a.size = r.jobRows[i].SIZE; a.copyIndex = (int)i;
        a.label = r.jobRows[i].SIZE + (isFull ? " FULL" : " SLV");
        sleevePool.push_back(a);
        SleeveSpec b = a;
        b.flip = true;
        b.label = a.label + "'";
        sleevePool.push_back(b);
    }
    if (skippedHafSlv) r.warnings.push_back("No short sleeve SVG — HAF sleeves skipped.");
    if (skippedFullSlv) r.warnings.push_back("No long sleeve SVG — FULL sleeves skipped.");

    std::vector<Piece> slvPlaced;
#if PF_DEV_TRIALS
    if (gDev.on) devBind(&packed.rows, &slvPlaced, r.rowsPerDoc, r.minGap);
#endif
    std::vector<RemSleeve> overflow;
    if (!sleevePool.empty()) {
        placeSleeves(sleevePool, packed.rows, packed.obstacles, r.dia, r.minGap, slvPlaced, overflow);
        std::vector<RemSleeve> fullRem;
        for (size_t i = 0; i < sleevePool.size(); i++) {
            if (sleevePool[i].isFull) fullRem.push_back({ (int)i });
        }
        if (!fullRem.empty()) {
            /* Rule 4: FULL already tried in the 0° pocket (Rule 2). Rest sit below everything. */
            std::vector<char> used(sleevePool.size(), 0);
            for (size_t i = 0; i < slvPlaced.size(); i++) {
                if (slvPlaced[i].specIndex >= 0 && slvPlaced[i].specIndex < (int)sleevePool.size())
                    used[slvPlaced[i].specIndex] = 1;
            }
            std::vector<RemSleeve> leftoverFull;
            for (size_t i = 0; i < fullRem.size(); i++) {
                if (fullRem[i].specIndex >= 0 && !used[fullRem[i].specIndex])
                    leftoverFull.push_back(fullRem[i]);
            }
            sortFullSmallFirst(leftoverFull, sleevePool);
            double bot = 0;
            for (size_t b = 0; b < packed.obstacles.size(); b++) {
                if (packed.obstacles[b].bbox.b > bot) bot = packed.obstacles[b].bbox.b;
            }
            for (size_t s = 0; s < slvPlaced.size(); s++) {
                if (slvPlaced[s].bbox.b > bot) bot = slvPlaced[s].bbox.b;
            }
            PF_CTX("full-row", -1, "", "");
            packFullSleeveRows(leftoverFull, sleevePool, bot + r.minGap, packed.obstacles, slvPlaced, r.minGap, r.dia);
#if PF_DEV_TRIALS
            if (gDev.on) devSnap("sit", "full-row");
#endif
            for (size_t i = 0; i < leftoverFull.size(); i++) overflow.push_back(leftoverFull[i]);
        }
    }
    r.overflowSleeves = (int)overflow.size();
    if (r.overflowSleeves) {
        r.warnings.push_back(std::to_string(r.overflowSleeves) + " sleeve(s) could not sit at this DIA.");
        PF_DECIDE("overflow", "unsit");
    }

    r.pages = slicePages(packed.rows, slvPlaced, r.rowsPerDoc, r.minGap);
    /* Mark on the page copies — bodyRows.pieces never received the pre-slice flags. */
    r.overlapCount = 0;
    for (size_t p = 0; p < r.pages.size(); p++) {
        std::vector<Piece*> pageAll;
        for (size_t i = 0; i < r.pages[p].bodies.size(); i++) pageAll.push_back(&r.pages[p].bodies[i]);
        for (size_t i = 0; i < r.pages[p].sleeves.size(); i++) pageAll.push_back(&r.pages[p].sleeves[i]);
        r.overlapCount += markOverlaps(pageAll);
    }
    if (r.overlapCount) r.warnings.push_back(std::to_string(r.overlapCount) + " piece(s) overlay — shown in red.");
    r.bodyRowCount = (int)packed.rows.size();
    for (size_t p = 0; p < r.pages.size(); p++) {
        r.fabricInches += r.pages[p].height;
        if (r.pages[p].height > DOC_MAX_H + 0.05) {
            r.warnings.push_back("DOC " + std::to_string(r.pages[p].index)
                + " is " + std::to_string((int)(r.pages[p].height + 0.5))
                + " in (limit " + std::to_string((int)DOC_MAX_H) + " in).");
        }
    }
    r.fabricMeters = r.fabricInches * 0.0254;
    r.ok = true;
    r.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

static void writePiece(JsonOut& o, const Piece& p) {
    o.raw("{");
    o.key("kind"); o.str(p.kind); o.comma();
    o.key("size"); o.str(p.size); o.comma();
    o.key("label"); o.str(p.label); o.comma();
    o.key("rotation"); o.num(p.rotation); o.comma();
    o.key("flipX"); o.boolean(p.flipX); o.comma();
    o.key("overlap"); o.boolean(p.overlap); o.comma();
    o.key("sleeveBlock"); o.num((double)p.sleeveBlock); o.comma();
    o.key("copyIndex"); o.num((double)p.copyIndex); o.comma();
    o.key("bbox"); o.raw("{");
    o.key("x"); o.num(p.bbox.x); o.comma();
    o.key("y"); o.num(p.bbox.y); o.comma();
    o.key("w"); o.num(p.bbox.w); o.comma();
    o.key("h"); o.num(p.bbox.h); o.comma();
    o.key("r"); o.num(p.bbox.r); o.comma();
    o.key("b"); o.num(p.bbox.b);
    o.raw("},");
    o.key("points"); o.raw("[");
    for (size_t i = 0; i < p.points.size(); i++) {
        if (i) o.comma();
        o.raw("{\"x\":"); o.num(p.points[i].x); o.raw(",\"y\":"); o.num(p.points[i].y); o.raw("}");
    }
    o.raw("]}");
}

/** Automation layer names — right-side map (FRONT / SHORT-SLV-1-WITH-RIB …). */
static std::string layerNameOf(const Piece& p, const std::string& sleeveKey) {
    if (p.role == "FRONT" || p.kind == "FRONT") return "FRONT";
    if (p.role == "BACK" || p.kind == "BACK") return "BACK";
    const bool without = sleeveKey.find("without") != std::string::npos;
    const char* rib = without ? "WITHOUT-RIB" : "WITH-RIB";
    std::string s = p.isFull ? "LONG-SLV-" : "SHORT-SLV-";
    s += p.flipX ? "2-" : "1-";
    s += rib;
    return s;
}

static void writeExportItem(JsonOut& o, const Piece& p, const std::vector<JobRow>& jobs,
                            const std::string& sleeveKey, double originY) {
    const JobRow* row = nullptr;
    if (p.copyIndex >= 0 && p.copyIndex < (int)jobs.size()) row = &jobs[(size_t)p.copyIndex];
    o.raw("{");
    o.key("name"); o.str(layerNameOf(p, sleeveKey)); o.comma();
    o.key("NAME"); o.str(row ? row->NAME : ""); o.comma();
    o.key("NUMBER"); o.str(row ? row->NUMBER : ""); o.comma();
    o.key("SIZE"); o.str(row ? row->SIZE : p.size); o.comma();
    o.key("SLV"); o.str(row ? row->SLV : ""); o.comma();
    o.key("COMMENTS"); o.str(row ? row->COMMENTS : ""); o.comma();
    if (row) {
        for (auto kv = row->extra.begin(); kv != row->extra.end(); ++kv) {
            o.raw("\"");
            o.raw(jsonEscape(kv->first));
            o.raw("\":");
            o.str(kv->second);
            o.comma();
        }
    }
    o.key("xInches"); o.num(p.bbox.x); o.comma();
    o.key("yInches"); o.num(p.bbox.y - originY); o.comma();
    o.key("rotation"); o.num(p.rotation); o.comma();
    o.key("widthInches"); o.num(p.bbox.w); o.comma();
    o.key("heightInches"); o.num(p.bbox.h);
    o.raw("}");
}

static void writeDocs(JsonOut& o, const SimResult& r) {
    o.key("docs"); o.raw("[");
    for (size_t p = 0; p < r.pages.size(); p++) {
        if (p) o.comma();
        const Page& pg = r.pages[p];
        const double originY = std::isfinite(pg.contentY0) ? pg.contentY0 : pg.y0;
        o.raw("{");
        o.key("index"); o.num((double)pg.index); o.comma();
        o.key("width"); o.num(r.dia); o.comma();
        o.key("height"); o.num(pg.height); o.comma();
        o.key("items"); o.raw("[");
        bool any = false;
        for (size_t i = 0; i < pg.bodies.size(); i++) {
            if (any) o.comma();
            writeExportItem(o, pg.bodies[i], r.jobRows, r.sleeveKey, originY);
            any = true;
        }
        for (size_t i = 0; i < pg.sleeves.size(); i++) {
            if (any) o.comma();
            writeExportItem(o, pg.sleeves[i], r.jobRows, r.sleeveKey, originY);
            any = true;
        }
        o.raw("]}");
    }
    o.raw("]");
}

static std::string resultToJson(const SimResult& r) {
    JsonOut o;
    o.raw("{");
    o.key("ok"); o.boolean(r.ok); o.comma();
    o.key("error"); o.str(r.error); o.comma();
    o.key("dia"); o.num(r.dia); o.comma();
    o.key("minGap"); o.num(r.minGap); o.comma();
    o.key("rowsPerDoc"); o.num((double)r.rowsPerDoc); o.comma();
    o.key("sleeveKey"); o.str(r.sleeveKey); o.comma();
    o.key("bodyRowCount"); o.num((double)r.bodyRowCount); o.comma();
    o.key("overflowSleeves"); o.num((double)r.overflowSleeves); o.comma();
    o.key("overlapCount"); o.num((double)r.overlapCount); o.comma();
    o.key("fabricInches"); o.num(r.fabricInches); o.comma();
    o.key("fabricMeters"); o.num(r.fabricMeters); o.comma();
    o.key("elapsedMs"); o.num(r.elapsedMs); o.comma();
    o.key("engine"); o.str("cpp"); o.comma();
    o.key("job"); o.raw("{");
    o.key("skippedFull"); o.num((double)r.skippedFull); o.comma();
    o.key("skipped"); o.num((double)r.skipped); o.comma();
    o.key("rows"); o.raw("[");
    for (size_t i = 0; i < r.jobRows.size(); i++) {
        if (i) o.comma();
        o.raw("{");
        o.key("NAME"); o.str(r.jobRows[i].NAME); o.comma();
        o.key("NUMBER"); o.str(r.jobRows[i].NUMBER); o.comma();
        o.key("SIZE"); o.str(r.jobRows[i].SIZE); o.comma();
        o.key("SLV"); o.str(r.jobRows[i].SLV);
        o.raw("}");
    }
    o.raw("]},");
    o.key("warnings"); o.raw("[");
    for (size_t i = 0; i < r.warnings.size(); i++) {
        if (i) o.comma();
        o.str(r.warnings[i]);
    }
    o.raw("],");
    o.key("pages"); o.raw("[");
    for (size_t p = 0; p < r.pages.size(); p++) {
        if (p) o.comma();
        const Page& pg = r.pages[p];
        o.raw("{");
        o.key("index"); o.num((double)pg.index); o.comma();
        o.key("rowFrom"); o.num((double)pg.rowFrom); o.comma();
        o.key("rowTo"); o.num((double)pg.rowTo); o.comma();
        o.key("y0"); o.num(pg.y0); o.comma();
        o.key("y1"); o.num(pg.y1); o.comma();
        o.key("contentY0"); o.num(pg.contentY0); o.comma();
        o.key("contentY1"); o.num(pg.contentY1); o.comma();
        o.key("height"); o.num(pg.height); o.comma();
        o.key("width"); o.num(pg.width); o.comma();
        o.key("bodies"); o.raw("[");
        for (size_t i = 0; i < pg.bodies.size(); i++) {
            if (i) o.comma();
            writePiece(o, pg.bodies[i]);
        }
        o.raw("],");
        o.key("sleeves"); o.raw("[");
        for (size_t i = 0; i < pg.sleeves.size(); i++) {
            if (i) o.comma();
            writePiece(o, pg.sleeves[i]);
        }
        o.raw("]}");
    }
    o.raw("],");
    writeDocs(o, r);
#if PF_DEV_TRIALS
    o.comma();
    gDev.write(o);
#endif
    o.raw("}");
    return o.str();
}

} // namespace pf

static std::string readAllStdin() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    std::ostringstream os;
    os << std::cin.rdbuf();
    return os.str();
}

int main() {
    try {
        std::string src = readAllStdin();
        if (src.empty()) {
            std::cout << "{\"ok\":false,\"error\":\"Empty input\",\"pages\":[],\"job\":{\"rows\":[]},\"elapsedMs\":0}";
            return 0;
        }
        pf::Json input = pf::parseJson(src);
        pf::SimResult r = pf::simulate(input);
        std::cout << pf::resultToJson(r);
        return r.ok ? 0 : 2;
    } catch (const std::exception& e) {
        pf::JsonOut o;
        o.raw("{\"ok\":false,\"error\":");
        o.str(e.what());
        o.raw(",\"pages\":[],\"job\":{\"rows\":[]},\"elapsedMs\":0}");
        std::cout << o.str();
        return 1;
    }
}
