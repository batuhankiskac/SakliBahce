// Characters module: tavla at its own table (w3d::tavlaFrame). The opponent gets up from the okey table and sits
// across the player there, the two tea glasses go along (the çaycı serves them there), Kel Mahmut's ash goes into the
// tavla table's ashtray, and the bystanders of a long match stand behind the tavla opponent.
#include "r3d/CharactersState.h"

namespace r3d {
namespace chr {

Vector3 Cast::ashtrayFor(const Opponent& o) const {
    return tavlaSeat > 0 && o.seat == tavlaSeat ? w3d::tavlaToWorld(w3d::TAVLA_ASHTRAY_LOCAL) : w3d::ASHTRAY_POS;
}

namespace {

void seat(Opponent& o, Vector3 pos, float yawDeg) {
    o.pos = pos;
    o.yawDeg = yawDeg;
    o.root = trsYaw(o.pos, o.yawDeg, 1.f);
    o.rootInv = MatrixInvert(o.root);
}

void putGlass(TeaGlass& g, Vector3 saucer) {
    g.holder = -1;
    g.blend = 1.f;
    g.saucer = saucer;
    g.rest = Vector3Add(saucer, {0, SAUCER_TOP, 0});
    g.world = mul(RY(g.yaw), T(g.rest));
}

} // namespace

} // namespace chr

void Characters::setTavlaTable(bool on, int seat) {
    Impl& m = *impl_;
    if (!m.ready) return;
    const int want = on ? std::clamp(seat, 1, 3) : 0;
    if (want == m.tavlaSeat) return;
    // the one who played tavla goes back to the okey table
    if (m.tavlaSeat > 0) {
        chr::Opponent& o = m.opp[m.tavlaSeat];
        chr::seat(o, w3d::seatPos(o.seat), w3d::seatYawDeg(o.seat));
        chr::putGlass(m.glass[(size_t)o.seat], m.glassHome[(size_t)o.seat]);
        chr::putGlass(m.glass[0], m.glassHome[0]);
        m.tableFocus = {0.f, w3d::TABLE_Y, 0.f};
        o.gaze = o.gazeGoal = m.tableFocus;
    }
    m.tavlaSeat = want;
    if (want > 0) {
        chr::Opponent& o = m.opp[want];
        chr::seat(o, w3d::tavlaToWorld({0.f, 0.f, -w3d::TAVLA_SEAT_DIST}), w3d::TAVLA_YAW_DEG + 180.f);
        chr::putGlass(m.glass[(size_t)want], w3d::tavlaToWorld(w3d::TAVLA_GLASS_LOCAL[1]));
        chr::putGlass(m.glass[0], w3d::tavlaToWorld(w3d::TAVLA_GLASS_LOCAL[0]));
        m.tableFocus = w3d::tavlaToWorld({0.f, w3d::TABLE_Y, 0.f});
        o.gaze = o.gazeGoal = m.tableFocus;
    }
    // (a bystander already standing at the old table goes home)
    for (chr::Watcher& w : m.watchers) {
        w.state = -1;
        w.came = false;
    }
}

int Characters::tavlaSeat() const { return impl_->tavlaSeat; }

} // namespace r3d
