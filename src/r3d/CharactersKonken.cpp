// Characters module: Konken "son kalan" (Rules::lastStanding). A regular whose total burned ("yandı") gets up from the
// table and watches the rest of the match standing a step behind his chair, hands behind his back; the table plays on
// with the ones left. He is a Watcher body (Cast::updateWatcher: the walk out, the standing idle, reactions) in his own
// clothes (his PersonMeshes); his face parts (eyes, lids, brows, lips, mustache, Nuri's spectacles) are carried on the
// standing head as they sat on the seated one, so he still looks like himself from the player's chair.
//
// Also here: Characters::floorPeople, the people standing or walking on the floor (the cat walks round them).
#include "r3d/CharactersState.h"

namespace r3d {
namespace chr {

namespace {

// Where each seat stands once he has left (world, our okey table; clear of the bystanders' spots behind Kel Mahmut,
// the special days' standing men and the background tables' chairs).
const Vector3 kOutSpot[4] = {{0.f, 0.f, 0.f}, {1.52f, 0.f, 0.42f}, {-1.4f, 0.f, -1.02f}, {-1.56f, 0.f, -0.18f}};

Matrix* faceMats(Opponent& o, int i) {
    Matrix* m[10] = {&o.eyeW[0], &o.eyeW[1], &o.lidW[0], &o.lidW[1], &o.browW[0], &o.browW[1], &o.mouthW, &o.lipW, &o.stacheW[0], &o.stacheW[1]};
    return m[i];
}

}  // namespace

void Cast::updateSeatOut(Opponent& o, float dt) {
    SeatOut& so = seatOut[(size_t)o.seat];
    updateWatcher(so.w, dt);
    if (so.w.state == -1) {  // (updateWatcher ends a walk *out*; ours only walks in to his spot)
        so.w.state = 1;
        so.w.wait = 1.f;
    }
    // the seated rig's head is the standing one now: headPosition, bubbles and the others' glances follow him
    o.headW = so.w.headW;
    o.torsoW = so.w.torsoW;
    for (int i = 0; i < 10; ++i) *faceMats(o, i) = mul(so.rel[i], so.w.headW);
}

void Cast::submitSeatOut(Renderer& r, const Opponent& o) {
    const SeatOut& so = seatOut[(size_t)o.seat];
    const Watcher& w = so.w;
    const PersonMeshes& pm = *o.pm;
    for (int i = 0; i < 2; ++i) {
        r.submit(&M.walkThigh[i], &pm.cloth, w.thighW[i], CastShadow);
        r.submit(&M.walkShin[i], &pm.cloth, w.shinW[i], CastShadow);
    }
    r.submit(&pm.torso, &pm.cloth, w.torsoW, CastShadow);
    r.submit(&pm.head, &pm.headMat, w.headW, CastShadow);
    for (int a = 0; a < 2; ++a) {
        r.submit(&pm.upper[a], &pm.cloth, w.upperW[a], 0);
        r.submit(&pm.fore[a], &pm.cloth, w.foreW[a], 0);
        r.submit(&M.hand[a][(int)w.pose[a]], &pm.skin, w.handW[a], 0);
    }
    for (int i = 0; i < 2; ++i) {
        r.submit(&M.eye[o.irisMesh], &M.eyeMat, o.eyeW[i], 0);
        r.submit(&M.lidUpper, &pm.skin, o.lidW[i], 0);
        r.submit(&pm.brow[i], &pm.hairMat, o.browW[i], 0);
    }
    r.submit(&M.lowerLip[o.lipVariant], &M.lipMat, o.lipW, 0);
    if (pm.stacheWing[0].vertexCount > 0) {
        r.submit(&pm.stacheWing[0], &pm.hairMat, o.stacheW[0], 0);
        r.submit(&pm.stacheWing[1], &pm.hairMat, o.stacheW[1], 0);
    } else {
        r.submit(&pm.stache, &pm.hairMat, o.headW, 0);
    }
    if (o.kind == 2) {
        r.submit(&M.spectacles, &M.frameMat, o.headW, 0);
        r.submit(&M.lenses, &M.lensMat, o.headW, Transparent | DoubleSided);
    }
    submitCount += 20;
}

}  // namespace chr

void Characters::setSeatOut(int seat, bool out, bool instant) {
    Impl& m = *impl_;
    if (!m.ready || seat < 1 || seat > 3) return;
    chr::SeatOut& so = m.seatOut[(size_t)seat];
    if (out == so.on) return;
    chr::Opponent& o = m.opp[seat];
    if (!out) {  // back to his chair (a new match): the seated rig takes over from where it was
        so.on = false;
        o.gaze = o.gazeGoal = m.tableFocus;
        return;
    }
    // his face as it sits on his head now (relative), then the standing body in his clothes
    const Matrix inv = MatrixInvert(o.headW);
    for (int i = 0; i < 10; ++i) so.rel[i] = chr::mul(*chr::faceMats(o, i), inv);
    chr::Watcher& w = so.w;
    w = chr::Watcher{};
    w.L = o.L;
    w.L.hipPivot = {0.f, 0.94f, 0.f};
    w.pm = o.pm;
    w.variant = o.kind;
    const Vector3 chair = w3d::seatPos(seat), dir = w3d::SEAT_DIR[seat];
    w.path = {Vector3Add(chair, Vector3Scale(dir, 0.3f)), Vector3Add(chair, Vector3Scale(dir, 0.62f)), chr::kOutSpot[seat]};
    w.pos = w.path[0];
    w.pathI = 1;
    w.yaw = std::atan2(-dir.x, -dir.z) + chr::PI_F;  // facing away from the table: he steps back from it
    w.state = 0;
    w.came = true;
    w.gaze = m.tableFocus;
    if (instant) {
        w.pos = chr::kOutSpot[seat];
        w.pathI = w.path.size();
        w.state = 1;
        w.wait = 2.f;
        const Vector3 d = Vector3Subtract(m.tableFocus, w.pos);
        w.yaw = std::atan2(-d.x, -d.z);
    }
    // his glass stays on the table, on its saucer
    chr::TeaGlass& g = m.glass[(size_t)seat];
    if (g.holder == seat) {
        g.holder = -1;
        g.blend = 1.f;
        g.world = chr::mul(chr::RY(g.yaw), chr::T(g.rest));
    }
    so.on = true;
    m.updateSeatOut(o, 0.f);
}

bool Characters::seatOut(int seat) const { return seat >= 1 && seat <= 3 && impl_->seatOut[(size_t)seat].on; }

void Characters::floorPeople(std::vector<Vector3>& out) const {
    const Impl& m = *impl_;
    out.clear();
    if (!m.ready) return;
    auto add = [&](Vector3 p, float r) { out.push_back({p.x, r, p.z}); };
    for (const chr::Watcher& w : m.watchers)
        if (w.state != -1) add(w.pos, 0.32f);
    for (int s = 1; s <= 3; ++s)
        if (m.seatOut[(size_t)s].on) add(m.seatOut[(size_t)s].w.pos, 0.32f);
    m.specialFloorPeople(out);  // (ozelgun's standing men)
    add(m.boy.pos, 0.3f);       // the çaycı, wherever he is
}

}  // namespace r3d
