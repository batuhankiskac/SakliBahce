// Table3D: every tile the players touch, in 3D. State model (ported from the proven 2D TableView): every
// frame each of the 106 tiles gets a TARGET (container + pose) derived from the game state; its visual
// tweens toward it, and when the container changes the tile flies there (arc, slight wobble, landing rock).
// That keeps animations right for every event, for bots and for autoplay. The human plays by mouse ray
// picking against the tiles, the istaka's row plane and the felt.
#include "r3d/Table3DInternal.h"
#include "r3d/Table3DTest.h"

#include <algorithm>
#include <cmath>

namespace r3d {

using namespace t3d;
using okey::Game;
using okey::Meld;
using okey::MeldKind;

namespace {

float lerpf(float a, float b, float t) { return a + (b - a) * t; }
float v2len(Vector2 a) { return std::sqrt(a.x * a.x + a.y * a.y); }

Pose lerpPose(const Pose& a, const Pose& b, float t) {
    Pose p;
    p.pos = Vector3Lerp(a.pos, b.pos, t);
    p.rot = QuaternionSlerp(a.rot, b.rot, t);
    p.scale = lerpf(a.scale, b.scale, t);
    return p;
}

float quatAngle(Quaternion a, Quaternion b) {
    const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2.f * std::acos(std::min(1.f, d));
}

// rotate a pose about a vertical axis through `c`
Pose yawAbout(const Pose& p, Vector3 c, float ang) {
    if (std::fabs(ang) < 1e-5f) return p;
    const Quaternion q = QuaternionFromAxisAngle({0, 1, 0}, ang);
    Pose o = p;
    o.pos = Vector3Add(c, Vector3RotateByQuaternion(Vector3Subtract(p.pos, c), q));
    o.rot = QuaternionMultiply(q, p.rot);
    return o;
}

} // namespace

// ================================================================ basics
TableState::TableState() {
    slots.fill(-1);
    for (int id = 0; id < NUM_TILES; ++id) heapCell[id] = id;
    dealDelay.fill(0.f);
    privTile.fill(true);
}

void TableState::sound(ui::Sfx s) const {
    if (sfx && *sfx) (*sfx)(s);
}

void TableState::pushToast(const std::string& key, const std::string& text, Color c, float dur) {
    if (text.empty()) return;
    Toast t;
    if (!key.empty()) {
        for (size_t i = 0; i < toasts.size(); ++i) {
            if (toasts[i].key == key) {
                t.age = std::min(toasts[i].age, 0.25f);
                toasts.erase(toasts.begin() + (long)i);
                break;
            }
        }
    }
    t.key = key;
    t.text = text;
    t.color = c;
    t.dur = dur;
    toasts.push_back(std::move(t));
    while (toasts.size() > 3) toasts.erase(toasts.begin());
}

void TableState::error(const std::string& text) {
    pushToast("err", text, ui::pal::Bad, 3.0f);
    sound(ui::Sfx::Error);
}

int TableState::slotOfTile(int id) const {
    for (int i = 0; i < SLOTS; ++i)
        if (slots[i] == id) return i;
    return -1;
}

bool TableState::inHumanHand(int id) const {
    const std::vector<int>& h = game->player(human).hand;
    return std::find(h.begin(), h.end(), id) != h.end();
}

// ================================================================ racks & pile bookkeeping
void TableState::reconcileRack() {
    if (!game) return;
    const std::vector<int>& hand = game->player(human).hand;
    std::array<bool, NUM_TILES> inHand{};
    for (int id : hand)
        if (okey::isValidTile(id)) inHand[id] = true;
    std::array<bool, NUM_TILES> inSlots{};
    for (int& id : slots) {
        if (id < 0) continue;
        if (!okey::isValidTile(id) || !inHand[id] || inSlots[id]) {
            if (id == selected) selected = -1;
            id = -1;
            continue;
        }
        inSlots[id] = true;
    }
    for (int id : hand) {
        if (!okey::isValidTile(id) || inSlots[id]) continue;
        int slot = -1;
        if (place.active && place.slot >= 0 && place.slot < SLOTS) {
            if (slots[place.slot] < 0) {
                slot = place.slot;
            } else {
                const int row = place.slot / COLS, col = place.slot % COLS;
                if (insertBefore(slots, row, col + (place.after ? 1 : 0), id)) slot = slotOfTile(id);
            }
        }
        if (slot < 0) slot = chooseFreeSlot(slots, -1);
        if (slot < 0) continue; // rack full (cannot happen with <= 23 tiles)
        slots[slot] = id;
        inSlots[id] = true;
        place = Place{};
    }
    for (int seat = 0; seat < 4; ++seat) {
        if (seat == human) continue;
        std::vector<int>& ord = botOrder[seat];
        const std::vector<int>& h = game->player(seat).hand;
        std::array<bool, NUM_TILES> held{};
        for (int id : h)
            if (okey::isValidTile(id)) held[id] = true;
        ord.erase(std::remove_if(ord.begin(), ord.end(), [&](int id) { return !held[id]; }), ord.end());
        std::array<bool, NUM_TILES> have{};
        for (int id : ord) have[id] = true;
        for (int id : h)
            if (okey::isValidTile(id) && !have[id]) {
                // a tile a bot receives goes to a pseudo-random place in its rack (it sorts its own hand)
                const size_t at = ord.empty() ? 0 : (size_t)(hashU((uint32_t)id * 31u + (uint32_t)ord.size()) % (ord.size() + 1));
                ord.insert(ord.begin() + (long)at, id);
                have[id] = true;
            }
    }
}

void TableState::reconcilePile() {
    if (!game) return;
    // tiles that are in the pile = not anywhere else
    std::array<bool, NUM_TILES> elsewhere{};
    auto mark = [&](int id) {
        if (okey::isValidTile(id)) elsewhere[id] = true;
    };
    mark(ok().indicatorId);
    for (const Meld& m : game->table())
        for (const okey::PlacedTile& t : m.tiles) mark(t.id);
    for (int s = 0; s < 4; ++s) {
        for (int id : game->player(s).discards) mark(id);
        for (int id : game->player(s).hand) mark(id);
    }
    // a tile that left the pile swaps visuals with the top tile first (all backs look alike), so what
    // flies is always the top of the stack
    for (int j = (int)pileOrder.size() - 1; j >= 0; --j) {
        const int id = pileOrder[j];
        if (!elsewhere[id]) continue;
        const int top = (int)pileOrder.size() - 1;
        if (j != top) {
            const int u = pileOrder[top];
            std::swap(vis[id], vis[u]);
            pileOrder[j] = u;
        }
        pileOrder.pop_back();
    }
    std::array<bool, NUM_TILES> listed{};
    for (int id : pileOrder) listed[id] = true;
    for (int id = 0; id < NUM_TILES; ++id)
        if (!elsewhere[id] && !listed[id]) pileOrder.insert(pileOrder.begin(), id);
}

bool TableState::arrangedSlots(bool pairs, Slots& out) const {
    const std::vector<int>& hand = game->player(human).hand;
    if (hand.empty()) return false;
    const okey::RackArrangement a = pairs ? okey::arrangePairs(hand, ok()) : okey::arrangeSeries(hand, ok());
    std::array<int, NUM_TILES> need{};
    for (int id : hand)
        if (okey::isValidTile(id)) need[id] = 1;
    auto take = [&need](int id) {
        if (!okey::isValidTile(id) || need[id] != 1) return false;
        need[id] = 2;
        return true;
    };
    std::vector<std::vector<int>> melds;
    for (const std::vector<int>& m : a.melds) {
        std::vector<int> mm;
        for (int id : m)
            if (take(id)) mm.push_back(id);
        if (!mm.empty()) melds.push_back(std::move(mm));
    }
    std::vector<int> left;
    for (int id : a.leftovers)
        if (take(id)) left.push_back(id);
    for (int id : hand)
        if (take(id)) left.push_back(id);
    return layoutArrangement(melds, left, out);
}

void TableState::arrange(bool pairs) {
    if (!game) return;
    Slots s;
    if (!arrangedSlots(pairs, s)) return;
    int k = 0;
    for (int i = 0; i < SLOTS; ++i) {
        const int id = s[i];
        if (id < 0 || slotOfTile(id) == i) continue;
        TileVis& v = vis[id];
        v.forceHop = true;
        v.ft = -0.014f * (float)k++; // staggered start (see advanceVisuals)
    }
    slots = s;
    if (selected >= 0 && slotOfTile(selected) < 0) selected = -1;
}

// ================================================================ "Yapay Zeka" mode
// The layout the AI is going for: an opened hand keeps its kind; before opening, pairs once there are nearly
// enough of them and no series opening in sight. Once in pairs it stays there while they hold up, so the
// istaka does not flip between the two layouts with every draw. (The bot's own plan is private; this mirrors
// its opening rules closely enough for the watcher to follow.)
bool TableState::aiWantsPairs() {
    const okey::PlayerInfo& me = game->player(human);
    if (me.opened) return aiPairs = me.openedWithPairs;
    const int needSeries = game->seriesOpenNeed(), needPairs = game->pairsOpenNeed();  // (katlamalı aware)
    const int pc = okey::solvePairs(me.hand, ok()).value;
    if (pc < needPairs - 1) return aiPairs = false; // (skips the series solve in the usual case)
    const int sv = okey::solveSeries(me.hand, ok()).value;
    if (aiPairs) return aiPairs = sv < needSeries;
    return aiPairs = (sv < needSeries * 7 / 10) || (pc >= needPairs && sv < needSeries);
}

// Re-arranges the AI's istaka once it may (never mid-deal; a drawn tile flies straight to its group).
void TableState::updateAiArrange() {
    if (!aiArrangePending) return;
    if (!aiMode || !playing()) {
        aiArrangePending = false;
        return;
    }
    if (dealing()) return;
    if (!game->player(human).hand.empty()) arrange(aiWantsPairs());
    aiArrangePending = false;
}

void TableState::setAiMode(bool on) {
    if (on == aiMode) return;
    aiMode = on;
    aiToggleRequested = false;
    aiArrangePending = false;
    if (on) {
        // whatever the hand was doing is dropped: a held tile hops back, a pending question is withdrawn
        if (press.dragging) {
            const int dragged = draggedTileId();
            if (dragged >= 0) vis[dragged].forceHop = true;
        }
        press = Press{};
        selected = -1;
        confirm = Confirm{};
        queued = Btn::None;
        place = Place{};
        aiPairs = false;
        aiArrangePending = started();
    }
}

// ================================================================ hand start: gather, shuffle, deal
void TableState::rebuildForHand() {
    if (!started()) return;
    slots.fill(-1);
    for (auto& o : botOrder) o.clear();
    pileOrder.clear();
    selected = -1;
    press = Press{};
    place = Place{};
    confirm = Confirm{};
    newTile = -1;
    meldBorn.clear();
    meldPulse.clear();
    pileWarn = 0;
    revealed = false;
    revealAt = -1.f;
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(),
                                [](const Toast& t) { return t.key != "hand" && t.key != "match"; }),
                 toasts.end());
    Slots arranged;
    aiPairs = false;
    aiArrangePending = false;
    if (arrangedSlots(aiMode && aiWantsPairs(), arranged)) slots = arranged;
    reconcileRack();
    reconcilePile();

    // a fresh heap layout per hand: cell index = distance rank from the centre
    {
        const uint32_t seed = hashU(0xDEA10000u + (uint32_t)game->handIndex() * 977u + (uint32_t)ok().indicatorId * 131u);
        std::array<int, NUM_TILES> ids{};
        for (int i = 0; i < NUM_TILES; ++i) ids[i] = i;
        for (int i = NUM_TILES - 1; i > 0; --i) std::swap(ids[i], ids[hashU(seed + (uint32_t)i * 13u) % (uint32_t)(i + 1)]);
        for (int i = 0; i < NUM_TILES; ++i) heapCell[ids[i]] = i;
    }
    publicTile.fill(false);

    // deal order: gösterge first, the pile stacks, then round by round from the starter
    dealDelay.fill(0.3f);
    {
        const int ind = ok().indicatorId;
        if (okey::isValidTile(ind)) dealDelay[ind] = 0.02f;
        for (int i = 0; i < (int)pileOrder.size(); ++i) dealDelay[pileOrder[i]] = 0.05f + 0.012f * (float)i;
        std::array<std::vector<int>, 4> ord;
        for (int seat = 0; seat < 4; ++seat) {
            if (seat == human) {
                for (int i = 0; i < SLOTS; ++i)
                    if (slots[i] >= 0) ord[seat].push_back(slots[i]);
            } else {
                ord[seat] = botOrder[seat];
            }
        }
        int k = 0;
        for (int round = 0; round < 23; ++round)
            for (int i = 0; i < 4; ++i) {
                const int seat = (game->starter() + i) % 4;
                if (round < (int)ord[seat].size()) dealDelay[ord[seat][round]] = 0.32f + 0.0125f * (float)k++;
            }
    }
    bool anyVisible = false;
    for (const TileVis& v : vis)
        if (v.init && v.cont != C_HEAP) anyVisible = true;
    phase = anyVisible ? Phase::Gather : Phase::Shuffle;
    phaseT = 0.f;
    dealPending = true;
    boxes.clear();
    built = true;
    builtHand = game->handIndex();
    builtInd = ok().indicatorId;
    builtTurn = game->turnNumber();
    needRebuild = false;
}

bool TableState::dealing() const {
    return phase != Phase::Idle || dealPending || now < dealEndsAt;
}

// ================================================================ melds on the felt
void TableState::layoutMelds() {
    const std::vector<Meld>& table = game->table();
    boxes.assign(table.size(), MeldBox{});
    if (meldBorn.size() < table.size()) meldBorn.resize(table.size(), -100.f);
    if (meldPulse.size() < table.size()) meldPulse.resize(table.size(), -100.f);
    for (int owner = 0; owner < 4; ++owner) {
        std::vector<int> list;
        for (int i = 0; i < (int)table.size(); ++i)
            if (table[i].owner == owner) list.push_back(i);
        if (list.empty()) continue;
        w3d::RectXZ z = w3d::MELD_ZONE[owner];
        if (owner == human && human == 0) z.z1 = std::min(z.z1, HUMAN_MELD_NEAR); // the istaka hides the felt behind it
        if (owner == 2 && human == 0) z.z1 -= FAR_MELD_CLEAR;
        const float zw = z.x1 - z.x0, zd = z.z1 - z.z0;
        // far melds are seen at a grazing angle: they may lie a little larger while the zone has room
        const int rel = (owner - human + 4) % 4;
        float s = rel == 0 ? 1.f : (rel == 2 ? MELD_SCALE_FAR : MELD_SCALE_SIDE);
        std::vector<std::vector<int>> rows;
        float tw = TW, th = TH, gapT = 0, gapM = 0, gapR = 0;
        for (;; s -= 0.03f) {
            tw = TW * s;
            th = TH * s;
            gapT = 0.0009f * s;
            gapM = 0.011f * s;
            gapR = 0.006f * s;
            rows.assign(1, {});
            float x = 0.f;
            bool fits = true;
            for (int idx : list) {
                const float mw = (float)table[idx].size() * (tw + gapT) - gapT;
                if (mw > zw) fits = false;
                if (!rows.back().empty() && x + gapM + mw > zw) {
                    rows.push_back({});
                    x = 0.f;
                }
                x += (rows.back().empty() ? 0.f : gapM) + mw;
                rows.back().push_back(idx);
            }
            const float totalD = (float)rows.size() * th + (float)(rows.size() - 1) * gapR;
            if (totalD > zd) fits = false;
            if (fits || s <= 0.5f) break;
        }
        const float totalD = (float)rows.size() * th + (float)(rows.size() - 1) * gapR;
        // Readable from the human seat: every block starts on the edge of its zone nearest to the human and
        // grows away (the human's own rows start next to the istaka; the far player's just behind the pile,
        // where they are seen at ~24 deg from ~1.1 m instead of ~20 deg from 1.3 m; the side players' hug
        // their own edge and end short of the human's zone). A new row never moves the ones before it.
        float zc = z.z1 - th * 0.5f;
        const float dir = -1.f;
        if (owner == 1 || owner == 3) zc = std::min(std::max(SIDE_MELD_NEAR, z.z0 + totalD), z.z1) - th * 0.5f;
        for (const std::vector<int>& row : rows) {
            float rw = 0.f;
            for (size_t k = 0; k < row.size(); ++k)
                rw += (k ? gapM : 0.f) + (float)table[row[k]].size() * (tw + gapT) - gapT;
            float x = z.x0 + (zw - rw) * 0.5f;
            if (owner == 1) x = z.x1 - rw;
            else if (owner == 3) x = z.x0;
            for (int idx : row) {
                MeldBox& b = boxes[idx];
                const int n = table[idx].size();
                const float mw = (float)n * (tw + gapT) - gapT;
                b.x0 = x;
                b.x1 = x + mw;
                b.z0 = zc - th * 0.5f;
                b.z1 = zc + th * 0.5f;
                b.scale = s;
                b.pos.clear();
                for (int k = 0; k < n; ++k)
                    b.pos.push_back({x + tw * 0.5f + (float)k * (tw + gapT), w3d::TABLE_Y + TT * s * 0.5f, zc});
                x += mw + gapM;
            }
            zc += dir * (th + gapR);
        }
    }
}

// ================================================================ targets
Pose TableState::restPose(int id) const {
    const int slot = slotOfTile(id);
    if (slot < 0) return tgt[id].pose;
    return rackPose(human, true, slot / COLS, slotRight(slot % COLS));
}

void TableState::computeTargets() {
    std::array<bool, NUM_TILES> done{};
    auto set = [&](int id, int cont, const Pose& p, bool hidden = false) {
        if (!okey::isValidTile(id) || done[id]) return;
        done[id] = true;
        Target& t = tgt[id];
        t.cont = cont;
        t.pose = p;
        t.hidden = hidden;
    };
    if (!started() || phase == Phase::Gather || phase == Phase::Shuffle) {
        float swirl = 0.f;
        if (phase == Phase::Shuffle) {
            const float u = std::clamp(phaseT / 1.05f, 0.f, 1.f);
            swirl = 0.42f * std::sin(u * PI * 3.f) * std::sin(u * PI) + 0.25f * std::sin(u * PI * 1.5f) * (1.f - u);
        }
        for (int id = 0; id < NUM_TILES; ++id) set(id, C_HEAP, heapPose(heapCell[id], swirl));
        if (!started() || phase == Phase::Shuffle) privTile.fill(true); // shuffled face down: nobody knows
        return;
    }
    set(ok().indicatorId, C_IND, indicatorPose());
    const std::vector<Meld>& table = game->table();
    for (int i = 0; i < (int)table.size() && i < (int)boxes.size(); ++i) {
        const Meld& m = table[i];
        for (int k = 0; k < m.size(); ++k) {
            Pose p;
            p.pos = k < (int)boxes[i].pos.size() ? boxes[i].pos[k] : w3d::PILE_POS;
            p.rot = flatFaceUp(hashS((uint32_t)m.tiles[k].id * 613u + (uint32_t)i) * 1.2f);
            p.scale = boxes[i].scale;
            set(m.tiles[k].id, C_MELD + i, p);
        }
    }
    for (int seat = 0; seat < 4; ++seat) {
        const std::vector<int>& d = game->player(seat).discards;
        const int n = (int)d.size();
        for (int k = 0; k < n; ++k) {
            const int depth = n - 1 - k;
            set(d[k], C_DISC + seat, discardPose(seat, depth, n, d[k]), depth >= DISC_LAYERS);
        }
    }
    // the human's istaka (hover / selection lift the tile up the plank and toward the eye)
    {
        Vector3 c, n, r, up;
        rackRowFrame(human, true, 0, c, n, r, up);
        for (int i = 0; i < SLOTS; ++i) {
            const int id = slots[i];
            if (id < 0) continue;
            Pose p = rackPose(human, true, i / COLS, slotRight(i % COLS));
            float lift = 0.f;
            if (id == selected) lift = 1.f;
            else if (id == hoverTile && press.kind == Press::None) lift = 0.75f;
            if (lift > 0.f) p.pos = Vector3Add(p.pos, Vector3Add(Vector3Scale(up, 0.0105f * lift), Vector3Scale(n, 0.004f * lift)));
            set(id, C_HAND + human, p);
        }
    }
    // bots: two rows, centred; turned around when the hand is over
    for (int seat = 0; seat < 4; ++seat) {
        if (seat == human) continue;
        const std::vector<int>& ord = botOrder[seat];
        const int n = (int)ord.size();
        const int top = n / 2, front = n - top;
        float ang = 0.f;
        if (revealed) {
            const float u = std::clamp((now - revealAt - 0.18f * (float)((seat - human + 4) % 4 - 1)) / 0.85f, 0.f, 1.f);
            ang = ui::easeInOutCubic(u) * PI;
        }
        const Vector3 rc = w3d::seatLocal(seat, 0.f, w3d::RACK_DIST, w3d::TABLE_Y);
        for (int i = 0; i < n; ++i) {
            const bool isFront = i < front;
            const int k = isFront ? i : i - front;
            const int m = isFront ? front : top;
            const float right = ((float)k - (float)(m - 1) * 0.5f) * PITCH;
            set(ord[i], C_HAND + seat, yawAbout(rackPose(seat, false, isFront ? 1 : 0, right), rc, ang));
        }
    }
    for (int i = 0; i < (int)pileOrder.size(); ++i) set(pileOrder[i], C_PILE, pilePose(i));
    for (int id = 0; id < NUM_TILES; ++id)
        if (!done[id]) set(id, C_HEAP, heapPose(heapCell[id], 0.f));
    // which faces may be drawn: never a tile only its (bot) owner has seen, never the pile
    for (int id = 0; id < NUM_TILES; ++id) {
        const int c = tgt[id].cont;
        if (c == C_IND || c >= C_DISC) publicTile[id] = true;
        const bool botHand = c >= C_HAND && c < C_HAND + 4 && c != C_HAND + human;
        if (c == C_PILE) privTile[id] = !publicTile[id];
        else if (botHand) privTile[id] = !publicTile[id] && !revealed;
        else if (c != C_HEAP) privTile[id] = false;
    }
}

// ================================================================ flights
namespace {
bool isRack(int cont) { return cont >= C_HAND && cont < C_HAND + 4; }
// Tiles leave and enter an istaka from above (never through its plank), a little on the owner's side.
Vector3 rackApproach(int cont) {
    const int seat = cont - C_HAND;
    return {w3d::SEAT_DIR[seat].x * 0.022f, 0.13f, w3d::SEAT_DIR[seat].z * 0.022f};
}
Vector3 bezier(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    const float u = 1.f - t;
    const float a = u * u * u, b = 3.f * u * u * t, c = 3.f * u * t * t, d = t * t * t;
    return {a * p0.x + b * p1.x + c * p2.x + d * p3.x, a * p0.y + b * p1.y + c * p2.y + d * p3.y,
            a * p0.z + b * p1.z + c * p2.z + d * p3.z};
}
} // namespace

void TableState::startFlight(int id, int fromCont, int toCont, const Pose& to, float delay, bool hop) {
    TileVis& v = vis[id];
    v.flying = true;
    v.from = v.pose;
    v.ft = -delay;
    const float dist = Vector3Distance(to.pos, v.pose.pos);
    const float turn = quatAngle(to.rot, v.pose.rot);
    const Vector3 nTo = Vector3RotateByQuaternion({0, 0, 1}, to.rot);
    const Vector3 nFrom = Vector3RotateByQuaternion({0, 0, 1}, v.pose.rot);
    // a tile held in front of the istaka just slides into its slot
    const bool inFront = isRack(toCont) && Vector3DotProduct(Vector3Subtract(v.pose.pos, to.pos), nTo) > 0.003f && dist < 0.16f;
    if (hop && (v.atSlot || inFront)) {
        v.fdur = 0.19f + 0.12f * std::min(1.f, dist / 0.3f);
        v.arc = std::min(0.02f, 0.006f + dist * 0.06f);
        const Vector3 lift{0.f, v.arc, 0.f};
        v.c1 = Vector3Add(v.pose.pos, v.atSlot ? Vector3Add(Vector3Scale(nFrom, 0.012f), lift) : Vector3Scale(nTo, 0.004f));
        v.c2off = Vector3Add(Vector3Scale(nTo, 0.012f), lift);
    } else {
        v.fdur = 0.36f + 0.14f * std::min(1.f, dist / 0.8f) + 0.04f * std::min(1.f, turn / PI);
        v.arc = std::clamp(dist * 0.16f, 0.028f, 0.11f);
        const float h = v.arc / 0.75f; // a cubic with both controls raised by h peaks at 0.75 h
        const bool outOfRack = v.atSlot && isRack(fromCont);
        v.c1 = Vector3Add(v.pose.pos, outOfRack ? rackApproach(fromCont) : Vector3{0.f, v.pose.pos.y > w3d::TABLE_Y + 0.03f ? h * 0.35f : h, 0.f});
        v.c2off = isRack(toCont) ? rackApproach(toCont) : Vector3{0.f, h, 0.f};
        if (outOfRack || isRack(toCont)) v.fdur += 0.06f;
    }
    ++v.serial;
    const uint32_t hs = hashU((uint32_t)id * 7919u + (uint32_t)v.serial * 104729u);
    const float a = hashF(hs) * 2.f * PI;
    v.wobAxis = {std::cos(a), 0.f, std::sin(a)};
    v.wobAng = hashS(hs + 1) * (hop ? 3.f : 9.f) * DEG2RAD;
    v.flightStart = (float)(++flightCounter);
    v.atSlot = false;
}

void TableState::advanceVisuals(float dt) {
    const float sdt = dt * speed;
    const int dragged = draggedTileId();
    for (int id = 0; id < NUM_TILES; ++id) {
        TileVis& v = vis[id];
        const Target& t = tgt[id];
        if (!v.init) {
            v.init = true;
            v.cont = t.cont;
            v.pose = t.pose;
        }
        if (id == dragged) {
            v.atSlot = false;
            continue; // follows the mouse (updateDragPose)
        }
        if (t.cont != v.cont) {
            float delay = v.flying && v.ft < 0 ? -v.ft : 0.f;
            if (phase == Phase::Gather) delay = hashF((uint32_t)id * 331u) * 0.22f;
            else if (dealPending && v.cont == C_HEAP) delay = dealDelay[id];
            else if (delay == 0.f) delay = botLead(v.cont, t.cont);
            startFlight(id, v.cont, t.cont, t.pose, delay, false);
            v.clickOnLand = t.cont == C_HAND + human && !dealPending;
            v.cont = t.cont;
        } else if (v.forceHop) {
            const float delay = v.ft < 0 ? -v.ft : 0.f;
            startFlight(id, v.cont, t.cont, t.pose, delay, true);
            v.clickOnLand = false;
        }
        v.forceHop = false;
        if (v.flying) {
            v.ft += sdt;
            if (v.ft < 0.f) continue;
            const float u = ui::clamp01(v.ft / v.fdur);
            const float e = 0.5f * ui::easeInOutCubic(u) + 0.5f * ui::easeOutCubic(u);
            v.pose.pos = bezier(v.from.pos, v.c1, Vector3Add(t.pose.pos, v.c2off), t.pose.pos, e);
            const float er = smooth01(0.02f, 0.92f, u);
            const Quaternion wob = QuaternionFromAxisAngle(v.wobAxis, v.wobAng * std::sin(PI * e));
            v.pose.rot = QuaternionNormalize(QuaternionMultiply(wob, QuaternionSlerp(v.from.rot, t.pose.rot, er)));
            v.pose.scale = lerpf(v.from.scale, t.pose.scale, e);
            if (u >= 1.f) {
                v.flying = false;
                v.pose = t.pose;
                v.landT = 0.f;
                const bool onFelt = t.cont == C_PILE || t.cont >= C_DISC || t.cont == C_HEAP;
                v.landAmp = (onFelt ? 3.2f : 1.6f) * (v.arc > 0.025f ? 1.f : 0.5f);
                if (v.clickOnLand && now - lastLandClick > 0.08f) {
                    sound(ui::Sfx::TileClick);
                    lastLandClick = now;
                }
                v.clickOnLand = false;
            }
        } else {
            const float k = 1.f - std::exp(-22.f * sdt);
            v.pose = lerpPose(v.pose, t.pose, k);
            v.landT += sdt;
        }
        v.atSlot = !v.flying && isRack(v.cont) && Vector3Distance(v.pose.pos, t.pose.pos) < 0.02f;
        const bool hot = id == selected || (id == hoverTile && press.kind == Press::None);
        v.glow = lerpf(v.glow, hot ? 1.f : 0.f, 1.f - std::exp(-16.f * dt));
    }
}

// Tiles an opponent moves wait for its hand (w3d::BOT_*_LEAD): taken ones lift off when the fingers reach them,
// given ones leave the rack once picked.
float TableState::botLead(int fromCont, int toCont) const {
    auto botRack = [&](int c) { return isRack(c) && c - C_HAND != human; };
    const bool fromTable = fromCont == C_PILE || (fromCont >= C_DISC && fromCont < C_MELD);
    if (botRack(toCont)) return fromTable ? w3d::BOT_TAKE_LEAD : fromCont >= C_MELD ? w3d::BOT_SWAPBACK_LEAD : 0.f;
    if (botRack(fromCont) && toCont >= C_MELD) return layingMelds ? w3d::BOT_MELD_LEAD : w3d::BOT_GIVE_LEAD;
    if (botRack(fromCont) && toCont >= C_DISC) return w3d::BOT_GIVE_LEAD;
    return 0.f;
}

bool TableState::anyFlying() const {
    for (const TileVis& v : vis)
        if (v.flying) return true;
    return false;
}

int TableState::draggedTileId() const {
    if (!press.dragging) return -1;
    if (press.kind == Press::RackTile || press.kind == Press::Left) return press.tile;
    if (press.kind == Press::Pile) return pileOrder.empty() ? -1 : pileOrder.back();
    return -1;
}

// ================================================================ hints
void TableState::computeHints() {
    islek.assign(NUM_TILES, 0);
    if (playing() && !game->table().empty()) {
        for (int id : slots)
            if (id >= 0 && !ok().isJoker(id) && game->isPlayableOnTable(id)) islek[id] = 1;
    }
    groups = parseGroups(slots);
    groupKind.assign(groups.size(), 0);
    groupValue.assign(groups.size(), 0);
    seriesGroups.clear();
    pairGroups.clear();
    for (size_t i = 0; i < groups.size(); ++i) {
        const RackGroup& g = groups[i];
        Meld m;
        if (g.len >= 3) {
            if (okey::makeMeld(g.ids, ok(), m, false)) {
                groupKind[i] = 1;
                groupValue[i] = m.value();
                seriesGroups.push_back(g.ids);
            }
        } else if (g.len == 2) {
            if (okey::makePair(g.ids, ok(), m)) {
                groupKind[i] = 2;
                pairGroups.push_back(g.ids);
            }
        }
    }
    const okey::PlayerInfo& me = game->player(human);
    if (!me.opened) {
        seriesCheck = game->checkOpen(human, seriesGroups);
        pairCheck = game->checkOpen(human, pairGroups);
    } else {
        seriesCheck = game->checkLay(human, seriesGroups);
        pairCheck = game->checkLay(human, pairGroups);
    }
    if (seriesGroups.empty()) seriesCheck.valid = false;
    if (pairGroups.empty()) pairCheck.valid = false;
}

// ================================================================ picking
Ray TableState::mouseRay(Vector2 m) const {
    const ui::Viewport& vp = ui::currentViewport();
    const float sc = vp.scale > 0 ? vp.scale : 1.f;
    const Vector2 s{m.x * sc + vp.offset.x, m.y * sc + vp.offset.y};
    const int w = R ? R->renderWidth() : (int)ui::VW, h = R ? R->renderHeight() : (int)ui::VH;
    return GetScreenToWorldRayEx(s, cam, w, h);
}

int TableState::pickRackTile(const Ray& ray) const {
    int best = -1;
    float bestD = 1e30f;
    const Vector3 half{PITCH * 0.5f, TH * 0.5f + 0.0015f, TT * 0.5f + 0.002f};
    for (int i = 0; i < SLOTS; ++i) {
        const int id = slots[i];
        if (id < 0 || tgt[id].cont != C_HAND + human || vis[id].flying) continue;
        // the resting box, stretched upward so a lifted (hovered) tile stays hovered
        Pose p = rackPose(human, true, i / COLS, slotRight(i % COLS));
        const Vector3 up = poseAxis(p, 1);
        p.pos = Vector3Add(p.pos, Vector3Scale(up, 0.005f));
        const float d = rayPoseBox(ray, p, {half.x, half.y + 0.005f, half.z});
        if (d >= 0.f && d < bestD) {
            bestD = d;
            best = id;
        }
    }
    return best;
}

bool TableState::pickPile(const Ray& ray) const {
    if (pileOrder.empty()) return false;
    const Pose p = pilePose((int)pileOrder.size() - 1);
    // the whole top of the pile is a comfortable target
    Pose area = p;
    area.pos = {w3d::PILE_POS.x, p.pos.y, w3d::PILE_POS.z};
    area.rot = QuaternionIdentity();
    return rayPoseBox(ray, area, {TW + 0.006f, (p.pos.y - w3d::TABLE_Y) + 0.008f, TH + 0.006f}) >= 0.f;
}

bool TableState::pickLeft(const Ray& ray) const {
    const int top = game->topDiscard(leftSeat());
    if (top < 0) return false;
    Pose p = vis[top].pose;
    return rayPoseBox(ray, p, {TW * 0.5f + 0.008f, TH * 0.5f + 0.008f, TT * 0.5f + 0.006f}) >= 0.f;
}

void TableState::computeAim(Vector2 m) {
    aim = Aim{};
    if (!camValid || !game) return;
    const Ray ray = mouseRay(m);
    aim.valid = true;
    // the istaka: both rows lie in one plane (same lean), so a single plane test finds row and column
    {
        Vector3 c1, n, r, up;
        rackRowFrame(human, true, 1, c1, n, r, up);
        Vector3 hit;
        if (rayPlane(ray, c1, n, hit)) {
            const Vector3 d = Vector3Subtract(hit, c1);
            const float dx = Vector3DotProduct(d, r), dy = Vector3DotProduct(d, up);
            const float rowSplit = TH * 0.5f + (RACK_ROW_GAP * 0.5f);
            if (std::fabs(dx) <= w3d::RACK_LEN * 0.5f + 0.01f && dy >= -TH * 0.5f - 0.03f &&
                dy <= TH * 1.5f + 2.f * (RACK_ROW_GAP * 0.5f) + 0.035f) {
                aim.overRack = true;
                aim.rackRow = dy > rowSplit ? 0 : 1;
                aim.rackRight = dx;
                const float fx = (dx + (float)COLS * PITCH * 0.5f) / PITCH;
                const int col = std::clamp((int)std::floor(fx), 0, COLS - 1);
                aim.after = (fx - (float)col) > 0.5f;
                aim.slot = aim.rackRow * COLS + col;
                Vector3 rc, rn, rr, ru;
                rackRowFrame(human, true, aim.rackRow, rc, rn, rr, ru);
                aim.dragPoint = Vector3Add(Vector3Add(rc, Vector3Scale(rr, std::clamp(dx, -w3d::RACK_LEN * 0.5f, w3d::RACK_LEN * 0.5f))),
                                           Vector3Add(Vector3Scale(rn, 0.017f), Vector3Scale(ru, 0.006f)));
                aim.feltPoint = {aim.dragPoint.x, w3d::TABLE_Y, aim.dragPoint.z};
            }
        }
    }
    // the felt under the pointer (hover / click) or under the held tile (drag)
    Vector3 tableHit;
    const bool tableOk = rayPlane(ray, {0.f, w3d::TABLE_Y + TT * 0.5f, 0.f}, {0, 1, 0}, tableHit);
    if (!aim.overRack) {
        Vector3 hit;
        if (!rayPlane(ray, {0.f, w3d::TABLE_Y + DRAG_LIFT_Y, 0.f}, {0, 1, 0}, hit)) {
            // looking above the horizon: keep the tile on a sphere in front of the eye
            hit = Vector3Add(ray.position, Vector3Scale(ray.direction, 0.6f));
        }
        const float lim = w3d::FELT_HALF + w3d::RIM_W + 0.1f;
        hit.x = std::clamp(hit.x, -lim, lim);
        hit.z = std::clamp(hit.z, -lim, lim);
        aim.dragPoint = hit;
        aim.feltPoint = {hit.x, w3d::TABLE_Y, hit.z};
    }
    const Vector3 probe = press.dragging ? aim.feltPoint : (tableOk ? tableHit : aim.feltPoint);
    if (!aim.overRack || !press.dragging) {
        const Vector3 dp = w3d::DISCARD_POS[human];
        const float ddx = probe.x - dp.x, ddz = probe.z - dp.z;
        aim.overDiscard = !aim.overRack && ddx * ddx + ddz * ddz < 0.068f * 0.068f;
        if (!aim.overDiscard && !aim.overRack) {
            const int top = game->topDiscard(human);
            if (top >= 0 && rayPoseBox(ray, vis[top].pose, {TW * 0.5f + 0.01f, TH * 0.5f + 0.01f, TT * 0.5f + 0.02f}) >= 0.f)
                aim.overDiscard = true;
        }
        const std::vector<Meld>& table = game->table();
        for (int i = 0; i < (int)boxes.size() && i < (int)table.size() && !aim.overRack; ++i) {
            const MeldBox& b = boxes[i];
            const float mg = 0.012f;
            if (probe.x < b.x0 - mg || probe.x > b.x1 + mg || probe.z < b.z0 - mg || probe.z > b.z1 + mg) continue;
            aim.meld = i;
            aim.meldFront = probe.x < (b.x0 + b.x1) * 0.5f;
            const float hw = TW * b.scale * 0.5f, hh = TH * b.scale * 0.5f;
            for (int k = 0; k < table[i].size() && k < (int)b.pos.size(); ++k) {
                if (!table[i].tiles[k].joker) continue;
                if (std::fabs(probe.x - b.pos[k].x) <= hw + 0.002f && std::fabs(probe.z - b.pos[k].z) <= hh + 0.004f)
                    aim.meldJoker = k;
            }
            break;
        }
        for (int s = 0; s < 4 && !aim.overRack; ++s) {
            const Vector3 d = w3d::DISCARD_POS[s];
            const float dx = probe.x - d.x, dz = probe.z - d.z;
            if (dx * dx + dz * dz < 0.042f * 0.042f && !game->player(s).discards.empty()) aim.discardSeat = s;
        }
    }
    aim.overPile = pickPile(ray);
    aim.overLeft = pickLeft(ray);
}

// ================================================================ legality preview
bool TableState::isleLegal(int tile, int meld, int jokerIdx, bool* swap) const {
    if (swap) *swap = false;
    return game->canWorkTable(human) && isleFits(tile, meld, jokerIdx, swap);
}

bool TableState::isleFits(int tile, int meld, int jokerIdx, bool* swap) const {
    if (swap) *swap = false;
    if (meld < 0 || meld >= (int)game->table().size()) return false;
    const Meld& md = game->table()[meld];
    Meld out;
    if (jokerIdx >= 0 && !ok().isJoker(tile)) {
        int freed = -1;
        if (okey::trySwapJoker(md, tile, ok(), out, freed)) {
            if (swap) *swap = true;
            return true;
        }
    }
    return okey::tryAddTile(md, tile, ok(), okey::AddSide::Auto, out);
}

bool TableState::isleFront(int tile, int meld, bool wantFront) const {
    if (meld < 0 || meld >= (int)game->table().size()) return wantFront;
    const Meld& md = game->table()[meld];
    Meld out;
    if (okey::tryAddTile(md, tile, ok(), wantFront ? okey::AddSide::Front : okey::AddSide::Back, out)) return wantFront;
    return !wantFront; // the engine falls back to the other end (attemptIsle)
}

// ================================================================ actions
void TableState::attemptDraw(bool fromLeft, int slot, bool after) {
    if (!canAct()) return;
    place = Place{};
    place.active = slot >= 0;
    place.slot = slot;
    place.after = after;
    const okey::ActionResult r = fromLeft ? game->takeFromLeft(human) : game->drawFromPile(human);
    if (!r.ok) {
        place = Place{};
        error(r.error);
        return;
    }
    reconcilePile();
    reconcileRack();
    place = Place{};
    pendingSync = true;
}

void TableState::attemptDiscard(int tile, bool confirmed) {
    if (!canAct() || tile < 0) return;
    const okey::PlayerInfo& me = game->player(human);
    if (game->stage() == okey::TurnStage::Play && game->pendingLeftTile() < 0 && !confirmed) {
        const bool finishing = me.hand.size() == 1;
        const bool joker = ok().isJoker(tile);
        const okey::RulesConfig& rc = game->rules();
        const bool jokerPen = joker && !finishing && rc.penaltyJokerDiscard;
        const bool islekPen = !joker && !finishing && rc.penaltyPlayableDiscard && game->isPlayableOnTable(tile);
        if (jokerPen || islekPen) {
            confirm = Confirm{};
            confirm.active = true;
            confirm.tile = tile;
            const std::string pen = std::to_string(rc.penalty);
            confirm.text = std::string(jokerPen ? "Bu taş okey!" : "Bu taş işlek!") + " Atarsan " + pen +
                           " ceza yersin. Yine de atılsın mı?";
            sound(ui::Sfx::Error);
            return;
        }
    }
    const okey::ActionResult r = game->discard(human, tile);
    if (!r.ok) {
        error(r.error);
        return;
    }
    if (selected == tile) selected = -1;
    pendingSync = true;
}

void TableState::attemptIsle(int tile, int meld, int jokerIdx, bool front) {
    if (!canAct() || meld < 0 || meld >= (int)game->table().size()) return;
    std::string firstErr;
    if (jokerIdx >= 0 && !ok().isJoker(tile)) {
        const okey::ActionResult r = game->swapJoker(human, tile, meld);
        if (r.ok) {
            pendingSync = true;
            if (selected == tile) selected = -1;
            return;
        }
        firstErr = r.error;
    }
    okey::ActionResult r = game->addToMeld(human, tile, meld, front ? okey::AddSide::Front : okey::AddSide::Back);
    if (!r.ok) {
        const okey::ActionResult r2 = game->addToMeld(human, tile, meld, okey::AddSide::Auto);
        if (r2.ok) r = r2;
    }
    if (r.ok) {
        pendingSync = true;
        if (selected == tile) selected = -1;
        return;
    }
    error(firstErr.empty() ? r.error : firstErr);
}

void TableState::doOpen() {
    if (!canAct()) return;
    if (game->stage() != okey::TurnStage::Play) {
        error("Önce taş çekmelisin");
        return;
    }
    const bool opened = game->player(human).opened;
    okey::ActionResult r;
    if (seriesCheck.valid) {
        r = opened ? game->layMelds(human, seriesGroups) : game->openHand(human, seriesGroups);
    } else if (pairCheck.valid) {
        r = opened ? game->layMelds(human, pairGroups) : game->openHand(human, pairGroups);
    } else if (seriesGroups.empty() && pairGroups.empty()) {
        r = okey::ActionResult::fail(opened ? "Istakanda açılacak per yok"
                                            : "Istakanda geçerli per yok — taşları yan yana diz");
    } else {
        const bool preferPairs = seriesGroups.empty() ||
                                 (!opened && pairGroups.size() * 3 > seriesGroups.size() * 5) ||
                                 (opened && game->player(human).openedWithPairs);
        const okey::OpenCheck& c = preferPairs && !pairGroups.empty() ? pairCheck : seriesCheck;
        r = okey::ActionResult::fail(c.error.empty() ? "Açılamaz" : c.error);
    }
    if (!r.ok) {
        error(r.error);
        return;
    }
    pendingSync = true;
}

void TableState::doGiveBack() {
    if (!canAct() || game->pendingLeftTile() < 0) return;
    const okey::ActionResult r = game->returnLeftTile(human);
    if (!r.ok) error(r.error);
    else pendingSync = true;
}

void TableState::notYourTurn() {
    if (!humanInput || !playing() || dealing() || myTurn()) return;
    // no error beep: clicking around while the others play is fine, it just does nothing
    pushToast("turn", "Sıra " + nameLocative(game->player(game->current()).name), ui::pal::TextLight, 1.8f);
}

void TableState::runButton(Btn b) {
    switch (b) {
    case Btn::Open: doOpen(); break;
    case Btn::GiveBack: doGiveBack(); break;
    case Btn::Series:
        if (rackInteractive()) {
            arrange(false);
            sound(ui::Sfx::TileClick);
        }
        break;
    case Btn::Pairs:
        if (rackInteractive()) {
            arrange(true);
            sound(ui::Sfx::TileClick);
        }
        break;
    case Btn::Menu: menuRequested = true; break;
    case Btn::AiToggle: aiToggleRequested = true; break;
    case Btn::ConfirmYes: {
        const int t = confirm.tile;
        confirm = Confirm{};
        attemptDiscard(t, true);
        break;
    }
    case Btn::ConfirmNo: confirm = Confirm{}; break;
    case Btn::None: break;
    }
}

void TableState::queue(Btn b) {
    sound(ui::Sfx::Button);
    if (b == Btn::Menu || b == Btn::AiToggle) {
        runButton(b); // App's business (pause menu / Yapay Zeka mode): no need to wait for the table's step
        return;
    }
    queued = b;
}

// ================================================================ input
void TableState::handleInput(float dt) {
    const Vector2 m = in.mouse;
    const Vector2 dm{m.x - lastMouse.x, m.y - lastMouse.y};
    const float kv = 1.f - std::exp(-18.f * dt);
    dragVel = {lerpf(dragVel.x, dm.x / std::max(dt, 1e-3f), kv), lerpf(dragVel.y, dm.y / std::max(dt, 1e-3f), kv)};
    lastMouse = m;
    hudHover = overHud(m);
    computeAim(m);

    hoverTile = -1;
    hoverPile = hoverLeft = false;
    if (confirm.active) {
        press = Press{};
        return;
    }
    if (humanInput) {
        if (in.keySeries) runButton(Btn::Series);
        if (in.keyPairs) runButton(Btn::Pairs);
        if (in.keyOpen) runButton(Btn::Open);
    }
    const Ray ray = mouseRay(m);
    if (press.kind == Press::None && !hudHover && camValid) {
        if (rackInteractive()) hoverTile = pickRackTile(ray);
        if (hoverTile < 0) {
            if (canDrawNow() && aim.overPile) hoverPile = true;
            else if (canTakeLeftNow() && aim.overLeft) hoverLeft = true;
        }
    }

    // hover inspection of a meld or a discard pile (drawn by the HUD after a short dwell); an opponent's meld
    // also while a rack tile is carried over it, so a far meld can be read before the tile is let go
    {
        int kind = 0, idx = -1;
        if (press.kind == Press::None && !hudHover && hoverTile < 0 && !hoverPile && !hoverLeft && !aim.overRack) {
            if (aim.meld >= 0) {
                kind = 1;
                idx = aim.meld;
            } else if (aim.discardSeat >= 0) {
                kind = 2;
                idx = aim.discardSeat;
            }
        } else if (press.dragging && press.kind == Press::RackTile && !aim.overRack && aim.meld >= 0 &&
                   aim.meld < (int)game->table().size() && game->table()[aim.meld].owner != human) {
            kind = 1;
            idx = aim.meld;
        }
        if (kind == peekKind && idx == peekIdx) {
            peekT += dt;
        } else {
            peekKind = kind;
            peekIdx = idx;
            peekT = 0.f;
        }
    }

    if (in.pressed && !hudHover) {
        press = Press{};
        press.at = m;
        if (hoverTile >= 0) {
            press.kind = Press::RackTile;
            press.slot = slotOfTile(hoverTile);
            press.tile = hoverTile;
        } else if (hoverPile) {
            press.kind = Press::Pile;
        } else if (hoverLeft) {
            press.kind = Press::Left;
            press.tile = game->topDiscard(leftSeat());
        } else {
            press.kind = Press::Empty;
        }
    }

    if (press.kind != Press::None && in.down && !press.dragging) {
        if (v2len({m.x - press.at.x, m.y - press.at.y}) > 6.f &&
            (press.kind == Press::RackTile || press.kind == Press::Pile || press.kind == Press::Left)) {
            press.dragging = true;
            dragPoseValid = false;
            if (press.kind == Press::RackTile && selected == press.tile) selected = -1;
            sound(ui::Sfx::TileClick);
        }
    }
    // the dragged tile may vanish (autoplay, hand end): cancel
    if (press.dragging && press.kind == Press::RackTile && slotOfTile(press.tile) < 0) press = Press{};
    if (press.dragging && press.kind == Press::Left &&
        (!canTakeLeftNow() || game->topDiscard(leftSeat()) != press.tile))
        press = Press{};
    if (press.dragging && press.kind == Press::Pile && !canDrawNow()) press = Press{};

    // a release we never saw (focus loss, overlay): drop the press without acting
    if (!in.down && !in.released && !in.pressed && press.kind != Press::None) {
        const int dragged = draggedTileId();
        if (dragged >= 0) vis[dragged].forceHop = true;
        press = Press{};
    }
    if (in.released && press.kind != Press::None) {
        const Press p = press;
        const int dragged = draggedTileId();
        press = Press{};
        if (p.dragging) {
            if (dragged >= 0) vis[dragged].forceHop = true; // whatever happens, it settles from where it is
            onDrop(p);
        } else {
            onClick(p);
        }
    }
}

void TableState::updateDragPose(float dt) {
    const int id = draggedTileId();
    if (id < 0 || !aim.valid) return;
    TileVis& v = vis[id];
    Pose want;
    want.pos = aim.dragPoint;
    if (aim.overRack) {
        want.rot = rackPose(human, true, aim.rackRow, 0.f).rot;
    } else {
        // held above the felt, face tilted toward the eye so it stays readable
        Vector3 toCam = Vector3Subtract(cam.position, want.pos);
        toCam.y = 0.f;
        toCam = Vector3Length(toCam) > 1e-4f ? Vector3Normalize(toCam) : Vector3{0, 0, 1};
        const float tilt = 38.f * DEG2RAD;
        const Vector3 n = Vector3Normalize(Vector3Add(Vector3Scale({0, 1, 0}, std::cos(tilt)), Vector3Scale(toCam, std::sin(tilt))));
        const Vector3 x = Vector3Normalize(Vector3CrossProduct({0, 1, 0}, toCam));
        const Vector3 y = Vector3CrossProduct(n, x);
        want.rot = quatFromAxes(x, y, n);
        if (press.kind == Press::Pile) want.rot = QuaternionMultiply(want.rot, QuaternionFromAxisAngle({0, 1, 0}, PI));
    }
    // sway with the hand's motion
    const float roll = std::clamp(-dragVel.x * 0.0009f, -0.16f, 0.16f);
    const float pitch = std::clamp(dragVel.y * 0.0006f, -0.12f, 0.12f);
    want.rot = QuaternionMultiply(want.rot, QuaternionMultiply(QuaternionFromAxisAngle({0, 0, 1}, roll),
                                                               QuaternionFromAxisAngle({1, 0, 0}, pitch)));
    if (!dragPoseValid) {
        dragPoseValid = true;
        v.flying = false;
    }
    const float k = 1.f - std::exp(-30.f * dt);
    v.pose = lerpPose(v.pose, want, k);
    v.pose.scale = lerpf(v.pose.scale, 1.f, k);
    v.landT = 10.f;
}

void TableState::onDrop(const Press& p) {
    if (p.kind == Press::RackTile) {
        const int id = p.tile;
        const int from = slotOfTile(id);
        if (from < 0) return;
        if (aim.overRack && aim.slot >= 0) {
            moveTile(slots, from, aim.slot, aim.after);
            sound(ui::Sfx::TileClick);
            return;
        }
        if (aim.overDiscard && canAct()) {
            attemptDiscard(id, false);
            return;
        }
        if (aim.meld >= 0 && canAct()) {
            attemptIsle(id, aim.meld, aim.meldJoker, aim.meldFront);
            return;
        }
        if (aim.overDiscard || aim.meld >= 0) notYourTurn();
        return; // back to its slot
    }
    if (p.kind == Press::Pile) {
        if (aim.overRack && aim.slot >= 0) attemptDraw(false, aim.slot, aim.after);
        else if (aim.overPile || aim.feltPoint.z > w3d::PILE_POS.z) attemptDraw(false, -1, false);
        return;
    }
    if (p.kind == Press::Left) {
        if (aim.overRack && aim.slot >= 0) attemptDraw(true, aim.slot, aim.after);
        else if (aim.overLeft) attemptDraw(true, -1, false);
    }
}

void TableState::onClick(const Press& p) {
    if (p.kind == Press::RackTile) {
        const int id = p.tile;
        const bool dbl = id == lastClickTile && now - lastClickTime < 0.4f;
        if (dbl) {
            lastClickTile = -1;
            if (canAct() && game->stage() == okey::TurnStage::Play) {
                attemptDiscard(id, false);
                return;
            }
            if (canAct()) { // a double-click discard before drawing
                error("Önce taş çekmelisin");
                return;
            }
            if (!myTurn()) notYourTurn();
        } else {
            lastClickTile = id;
            lastClickTime = now;
        }
        selected = (selected == id) ? -1 : id;
        sound(ui::Sfx::TileClick);
        return;
    }
    if (p.kind == Press::Pile) {
        attemptDraw(false, -1, false);
        return;
    }
    if (p.kind == Press::Left) {
        attemptDraw(true, -1, false);
        return;
    }
    // the pile or the left pile clicked out of turn (they only take a press on our turn)
    if (!myTurn() && (aim.overPile || aim.overLeft)) notYourTurn();
    if (selected < 0) return;
    if (aim.overRack && aim.slot >= 0 && rackInteractive()) {
        const int from = slotOfTile(selected);
        if (from >= 0 && slots[aim.slot] < 0) {
            moveTile(slots, from, aim.slot, aim.after);
            sound(ui::Sfx::TileClick);
        }
        selected = -1;
        return;
    }
    if (aim.overDiscard && canAct()) {
        attemptDiscard(selected, false);
        return;
    }
    if (aim.meld >= 0 && canAct()) {
        attemptIsle(selected, aim.meld, aim.meldJoker, aim.meldFront);
        return;
    }
    if (aim.overDiscard || aim.meld >= 0) notYourTurn();
    selected = -1;
}

// ================================================================ frame
void TableState::step(float dt, bool hi) {
    now += dt;
    humanInput = hi;
    if (!humanInput) {
        // a screen is up (or autoplay): its clicks are not ours. A press still in progress is dropped
        // below as "a release we never saw" and a held tile hops back to its slot.
        in.pressed = in.down = in.released = false;
        in.keySeries = in.keyPairs = in.keyOpen = false;
    }
    if (!game) return;
    if (!started()) {
        computeTargets();
        advanceVisuals(dt);
        in = Input{};
        return;
    }
    if (needRebuild || !built || builtHand != game->handIndex() || builtInd != ok().indicatorId) rebuildForHand();

    // phases of a new hand
    if (phase != Phase::Idle) {
        phaseT += dt * speed;
        if (phase == Phase::Gather && ((phaseT > 0.25f && !anyFlying()) || phaseT > 1.6f)) {
            phase = Phase::Shuffle;
            phaseT = 0.f;
        } else if (phase == Phase::Shuffle && phaseT > 1.05f) {
            phase = Phase::Idle;
            phaseT = 0.f;
        }
    }

    reconcilePile();
    reconcileRack();
    if (queued != Btn::None) {
        const Btn b = queued;
        queued = Btn::None;
        runButton(b);
    }
    handleInput(dt);
    reconcilePile();
    reconcileRack();
    updateAiArrange();
    computeHints();
    layoutMelds();
    computeTargets();
    advanceVisuals(dt);
    updateDragPose(dt);
    if (dealPending && phase == Phase::Idle) {
        dealPending = false;
        float last = 0.f;
        for (float d : dealDelay) last = std::max(last, d);
        dealEndsAt = now + (last + 0.65f) / std::max(0.25f, speed);
    }
    pendingSync = false;

    // the hand is over: turn the bots' istakas around once everything has landed
    const bool over = game->handState() == okey::HandState::HandOver || game->handState() == okey::HandState::MatchOver;
    if (over && !revealed && !anyFlying() && !dealing()) {
        revealed = true;
        revealAt = now + 0.35f;
    }

    for (Toast& t : toasts) t.age += dt;
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [](const Toast& t) { return t.age > t.dur; }),
                 toasts.end());
    if (confirm.active) confirm.t += dt;

    if (playing() && !dealing()) {
        const int n = game->pileCount();
        if (n <= 3 && pileWarn < 2) {
            pileWarn = 2;
            pushToast("pile", n == 0 ? "Ortada taş kalmadı!" : "Son " + std::to_string(n) + " taş! Taşlar bitiyor…",
                      ui::pal::Bad, 3.2f);
        } else if (n <= 8 && pileWarn < 1) {
            pileWarn = 1;
            pushToast("pile", "Ortada " + std::to_string(n) + " taş kaldı", ui::pal::Highlight, 3.0f);
        }
    }
    in = Input{};
}

// ================================================================ events
void TableState::onEvent(const okey::GameEvent& e) {
    using okey::EvType;
    const bool isHuman = e.player == human;
    const Color light = ui::pal::TextLight, gold = ui::pal::Highlight, bad = ui::pal::Bad;
    switch (e.type) {
    case EvType::MatchStart: pushToast("hand", e.text, gold, 2.4f); break;
    case EvType::HandStart:
        if (!(built && builtHand == game->handIndex() && builtInd == ok().indicatorId && builtTurn == game->turnNumber()))
            needRebuild = true;
        pushToast("hand", e.text, gold, 3.4f);
        break;
    case EvType::TurnStart: break;
    case EvType::DrawPile:
        if (isHuman) {
            newTile = e.tile;
            newTileTime = now;
        }
        break;
    case EvType::TakeLeft:
        if (isHuman) {
            newTile = e.tile;
            newTileTime = now;
        }
        pushToast(isHuman ? "hmove" : "move", e.text, light, 2.2f);
        break;
    case EvType::ReturnLeft: break; // the Penalty right after it tells the whole story ("... geri verdi: 101 ceza")
    case EvType::Open:
    case EvType::LayMelds:
        if ((int)meldBorn.size() < e.meld + e.count) meldBorn.resize(e.meld + e.count, -100.f);
        for (int i = e.meld; i < e.meld + e.count; ++i)
            if (i >= 0) meldBorn[i] = now;
        pushToast("", e.text, e.type == EvType::Open ? gold : light, 3.2f);
        break;
    case EvType::AddToMeld:
    case EvType::SwapJoker:
        if (e.meld >= 0) {
            if ((int)meldPulse.size() <= e.meld) meldPulse.resize(e.meld + 1, -100.f);
            meldPulse[e.meld] = now;
        }
        pushToast(isHuman ? "hmove" : "move", e.text, e.type == EvType::SwapJoker ? gold : light, 2.4f);
        break;
    case EvType::Discard:
        if (isHuman && selected == e.tile) selected = -1;
        break;
    case EvType::Penalty: pushToast("", e.text, bad, 3.4f); break;
    case EvType::HandEnd: pushToast("hand", e.text, gold, 4.5f); break;
    case EvType::MatchEnd: pushToast("match", e.text, gold, 5.0f); break;
    }
    if (e.type != EvType::TurnStart) layingMelds = e.type == EvType::Open || e.type == EvType::LayMelds;
    // the AI's istaka stays tidy: re-arranged whenever tiles come in or leave (no holes, groups as it plans them)
    if (aiMode && isHuman) {
        switch (e.type) {
        case EvType::DrawPile:
        case EvType::TakeLeft:
        case EvType::ReturnLeft:
        case EvType::SwapJoker:
        case EvType::Open:
        case EvType::LayMelds:
        case EvType::AddToMeld:
        case EvType::Discard: aiArrangePending = true; break;
        default: break;
        }
    }
    pendingSync = true;
}

// ================================================================ public API
struct Table3D::Impl : TableState {};

namespace {
std::vector<std::pair<const Table3D*, TableState*>>& registry() {
    static std::vector<std::pair<const Table3D*, TableState*>> r;
    return r;
}
} // namespace

Table3D::Table3D() : impl_(new Impl) {
    impl_->sfx = &playSfx;
    registry().push_back({this, impl_});
}

Table3D::~Table3D() {
    auto& r = registry();
    r.erase(std::remove_if(r.begin(), r.end(), [this](const auto& p) { return p.first == this; }), r.end());
    delete impl_;
}

bool Table3D::init(Renderer& r, okey::Game* game, int humanSeat) {
    impl_->R = &r;
    impl_->game = game;
    impl_->human = (humanSeat >= 0 && humanSeat < 4) ? humanSeat : 0;
    impl_->built = false;
    impl_->toasts.clear();
    impl_->buildGfx(r);
    return impl_->gfxReady;
}

void Table3D::shutdown(Renderer& r) {
    impl_->freeGfx(r);
    impl_->game = nullptr;
    impl_->built = false;
}

void Table3D::onHandStart() {
    if (!impl_->game) return;
    impl_->rebuildForHand();
}

void Table3D::onEvent(const okey::GameEvent& e) {
    if (!impl_->game) return;
    impl_->onEvent(e);
}

void Table3D::update(float dt, const Camera3D& cam, Vector2 mouse, bool humanInput) {
    TableState& s = *impl_;
    s.cam = cam;
    s.camValid = true;
    s.in.mouse = mouse;
    s.in.pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    s.in.down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    s.in.released = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    s.in.keySeries = IsKeyPressed(KEY_S);
    s.in.keyPairs = IsKeyPressed(KEY_C);
    s.in.keyOpen = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
    s.step(dt, humanInput);
    s.in.mouse = mouse; // the HUD draws with this frame's mouse
}

void Table3D::submit(Renderer& r) {
    if (!impl_->gfxReady) return;
    impl_->submitAll(r);
}

void Table3D::drawHUD(const Renderer& r) { impl_->drawHUD(r); }

bool Table3D::isAnimating() const {
    const TableState& s = *impl_;
    if (s.pendingSync || s.needRebuild || s.dealing() || s.anyFlying() || s.aiArrangePending) return true;
    // a finished hand: the bots' istakas are still turning around to show their tiles
    const bool over = s.game && (s.game->handState() == okey::HandState::HandOver ||
                                 s.game->handState() == okey::HandState::MatchOver);
    return over && s.built && (!s.revealed || s.now < s.revealAt + REVEAL_SECONDS);
}

void Table3D::toast(const std::string& text, Color c, float seconds) { impl_->pushToast("", text, c, seconds); }

bool Table3D::consumeMenuRequest() {
    const bool r = impl_->menuRequested;
    impl_->menuRequested = false;
    return r;
}

void Table3D::setAnimationSpeed(float speed) { impl_->speed = std::clamp(speed, 0.25f, 4.f); }
void Table3D::setHints(bool on) { impl_->hints = on; }

void Table3D::setHeadAnchors(const std::array<Vector3, 4>& heads) {
    impl_->heads = heads;
    impl_->headsSet = true;
}

void Table3D::setAiMode(bool on) { impl_->setAiMode(on); }

bool Table3D::consumeAiToggleRequest() {
    const bool r = impl_->aiToggleRequested;
    impl_->aiToggleRequested = false;
    return r;
}

bool Table3D::mouseBusy() const {
    const TableState& s = *impl_;
    return s.confirm.active || s.press.kind == Press::RackTile || s.press.kind == Press::Pile ||
           s.press.kind == Press::Left || s.hoverTile >= 0 || s.hoverPile || s.hoverLeft || s.hudHover;
}

// ================================================================ harness hooks
namespace table3dtest {

namespace {
TableState* stateOf(const Table3D& t) {
    for (auto& p : registry())
        if (p.first == &t) return p.second;
    return nullptr;
}
Slots toSlots(const std::vector<int>& v) {
    Slots s;
    s.fill(-1);
    for (int i = 0; i < SLOTS && i < (int)v.size(); ++i) s[i] = v[i];
    return s;
}
} // namespace

void input(Table3D& t, float dt, Vector2 mouse, int button, int key, bool humanInput, const Camera3D& cam) {
    TableState* s = stateOf(t);
    if (!s) return;
    s->cam = cam;
    s->camValid = true;
    s->in = Input{};
    s->in.mouse = mouse;
    s->in.pressed = button == 1;
    s->in.down = button == 1 || button == 2;
    s->in.released = button == 3;
    s->in.keySeries = key == 'S';
    s->in.keyPairs = key == 'C';
    s->in.keyOpen = key == '\n';
    s->step(dt, humanInput);
    s->in.mouse = mouse;
}

void settle(Table3D& t, const Camera3D& cam, bool humanInput) {
    TableState* s = stateOf(t);
    if (!s) return;
    s->cam = cam;
    s->camValid = true;
    const Vector2 m = s->in.mouse;
    for (int i = 0; i < 900 && t.isAnimating(); ++i) {
        s->in = Input{};
        s->in.mouse = m;
        s->step(1.f / 60.f, humanInput);
    }
    for (int i = 0; i < 45; ++i) {
        s->in = Input{};
        s->in.mouse = m;
        s->step(1.f / 60.f, humanInput);
    }
}

std::vector<int> rackSlots(Table3D& t) {
    TableState* s = stateOf(t);
    if (!s) return {};
    return std::vector<int>(s->slots.begin(), s->slots.end());
}

void setRackSlots(Table3D& t, const std::vector<int>& slots) {
    TableState* s = stateOf(t);
    if (!s) return;
    s->slots = toSlots(slots);
    s->reconcileRack();
}

void pressButton(Table3D& t, int which) {
    TableState* s = stateOf(t);
    if (!s) return;
    const Btn map[] = {Btn::None, Btn::Open,       Btn::GiveBack,  Btn::Series,  Btn::Pairs,
                       Btn::Menu, Btn::ConfirmYes, Btn::ConfirmNo, Btn::AiToggle};
    if (which >= 1 && which <= 8) s->runButton(map[which]);
}

int draggedTile(Table3D& t) {
    TableState* s = stateOf(t);
    return s ? s->draggedTileId() : -1;
}
bool confirmActive(Table3D& t) {
    TableState* s = stateOf(t);
    return s && s->confirm.active;
}
int selectedTile(Table3D& t) {
    TableState* s = stateOf(t);
    return s ? s->selected : -1;
}
int hoverTile(Table3D& t) {
    TableState* s = stateOf(t);
    return s ? s->hoverTile : -1;
}
int rackTileUnderRay(Table3D& t, const Ray& ray) {
    TableState* s = stateOf(t);
    return s ? s->pickRackTile(ray) : -1;
}
Vector3 tileFaceCenter(Table3D& t, int id) {
    TableState* s = stateOf(t);
    if (!s || !okey::isValidTile(id)) return {0, 0, 0};
    return poseToWorld(s->vis[id].pose, {0, 0, TT * 0.5f});
}
Vector3 tileCenter(Table3D& t, int id) {
    TableState* s = stateOf(t);
    if (!s || !okey::isValidTile(id)) return {0, 0, 0};
    return s->vis[id].pose.pos;
}
Vector3 slotFaceCenter(Table3D& t, int slot) {
    TableState* s = stateOf(t);
    if (!s || slot < 0 || slot >= SLOTS) return {0, 0, 0};
    return poseToWorld(rackPose(s->human, true, slot / COLS, slotRight(slot % COLS)), {0, 0, TT * 0.5f});
}
Vector3 meldPoint(Table3D& t, int meld, float along) {
    TableState* s = stateOf(t);
    if (!s || meld < 0 || meld >= (int)s->boxes.size()) return {0, 0, 0};
    const MeldBox& b = s->boxes[meld];
    return {lerpf(b.x0, b.x1, along), w3d::TABLE_Y + TT * b.scale, (b.z0 + b.z1) * 0.5f};
}
Vector3 pileTopPoint(Table3D& t) {
    TableState* s = stateOf(t);
    if (!s || s->pileOrder.empty()) return w3d::PILE_POS;
    return poseToWorld(pilePose((int)s->pileOrder.size() - 1), {0, 0, -TT * 0.5f});
}
Vector3 leftTopPoint(Table3D& t) {
    TableState* s = stateOf(t);
    if (!s || !s->game) return w3d::DISCARD_POS[3];
    const int top = s->game->topDiscard(s->leftSeat());
    if (top < 0) return w3d::DISCARD_POS[s->leftSeat()];
    return poseToWorld(s->vis[top].pose, {0, 0, TT * 0.5f});
}
Vector3 discardPoint(Table3D& t) {
    TableState* s = stateOf(t);
    return w3d::DISCARD_POS[s ? s->human : 0];
}

std::string validate(Table3D& t, bool atRest) {
    TableState* s = stateOf(t);
    if (!s || !s->game || !s->started()) return "";
    const Game& g = *s->game;
    std::array<int, NUM_TILES> want;
    want.fill(C_PILE);
    std::array<int, NUM_TILES> seen{};
    auto mark = [&](int id, int cont) {
        if (!okey::isValidTile(id)) return;
        want[id] = cont;
        ++seen[id];
    };
    mark(g.okey().indicatorId, C_IND);
    for (int i = 0; i < (int)g.table().size(); ++i)
        for (const okey::PlacedTile& pt : g.table()[i].tiles) mark(pt.id, C_MELD + i);
    for (int seat = 0; seat < 4; ++seat) {
        for (int id : g.player(seat).discards) mark(id, C_DISC + seat);
        for (int id : g.player(seat).hand) mark(id, C_HAND + seat);
    }
    int pileN = 0;
    for (int id = 0; id < NUM_TILES; ++id) {
        if (seen[id] > 1) return "tile " + std::to_string(id) + " in two places in the game";
        if (seen[id] == 0) ++pileN;
        if (s->phase == Phase::Idle && s->tgt[id].cont != want[id])
            return "tile " + std::to_string(id) + " target " + std::to_string(s->tgt[id].cont) + " != " + std::to_string(want[id]);
        const Pose& p = s->vis[id].pose;
        if (!(std::isfinite(p.pos.x) && std::isfinite(p.pos.y) && std::isfinite(p.pos.z) && std::isfinite(p.rot.w)))
            return "tile " + std::to_string(id) + " has a non-finite pose";
        if (std::fabs(p.pos.x) > 1.2f || std::fabs(p.pos.z) > 1.2f || p.pos.y < w3d::TABLE_Y - 0.01f || p.pos.y > 1.6f)
            return "tile " + std::to_string(id) + " off the table";
    }
    if (pileN != g.pileCount()) return "pile count mismatch";
    if ((int)s->pileOrder.size() != g.pileCount()) return "pile order size mismatch";
    std::array<int, NUM_TILES> inRack{};
    for (int id : s->slots)
        if (id >= 0) ++inRack[id];
    for (int id = 0; id < NUM_TILES; ++id) {
        const bool held = want[id] == C_HAND + s->human;
        if (inRack[id] != (held ? 1 : 0)) return "rack/hand mismatch for tile " + std::to_string(id);
    }
    for (int seat = 0; seat < 4; ++seat) {
        if (seat == s->human) continue;
        if (s->botOrder[seat].size() != g.player(seat).hand.size()) return "bot order size mismatch";
    }
    if (atRest) {
        if (s->anyFlying()) return "tiles still flying after settling";
        for (int id = 0; id < NUM_TILES; ++id) {
            const float d = Vector3Distance(s->vis[id].pose.pos, s->tgt[id].pose.pos);
            if (d > 0.002f) return "tile " + std::to_string(id) + " resting " + std::to_string(d) + " m from its target";
        }
    }
    return "";
}

std::string lastToast(Table3D& t) {
    TableState* s = stateOf(t);
    if (!s || s->toasts.empty()) return "";
    return s->toasts.back().text;
}

int flyingCount(Table3D& t) {
    TableState* s = stateOf(t);
    if (!s) return 0;
    int n = 0;
    for (const TileVis& v : s->vis) n += v.flying ? 1 : 0;
    return n;
}

int submitCount(Table3D& t) {
    TableState* s = stateOf(t);
    return s ? s->lastSubmits : 0;
}

std::vector<std::vector<int>> parseGroups(const std::vector<int>& slots) {
    std::vector<std::vector<int>> out;
    for (const RackGroup& g : t3d::parseGroups(toSlots(slots))) out.push_back(g.ids);
    return out;
}

bool layoutArrangement(const std::vector<std::vector<int>>& melds, const std::vector<int>& leftovers, std::vector<int>& slots) {
    Slots s;
    const bool ok = t3d::layoutArrangement(melds, leftovers, s);
    slots.assign(s.begin(), s.end());
    return ok;
}

int chooseFreeSlot(const std::vector<int>& slots, int pref) { return t3d::chooseFreeSlot(toSlots(slots), pref); }

bool moveTile(std::vector<int>& slots, int from, int to, bool after) {
    Slots s = toSlots(slots);
    const bool ok = t3d::moveTile(s, from, to, after);
    slots.assign(s.begin(), s.end());
    return ok;
}

} // namespace table3dtest

} // namespace r3d
