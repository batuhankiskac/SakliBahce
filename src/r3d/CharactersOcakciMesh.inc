// The ocakçı's body (see r3d/CharactersOcakci.h), modelled with the same SDF helpers as the regulars. Included by
// CharactersMesh.cpp at r3d::chr scope (after its builders) — ocakçı owner. Not a header: compiled only there.

namespace {

// An older man who has stood at the ocak for thirty years: weathered skin, grey hair going thin on top, a thick
// salt-and-pepper mustache, white shirt with the sleeves rolled up, a navy bib apron, a little belly.
PersonLook ocakciLookImpl() {
    PersonLook L;
    L.kind = 4;
    L.skin = rgb(196, 136, 100);
    L.lip = rgb(158, 88, 78);
    L.hair = rgb(156, 150, 144);
    L.browCol = rgb(112, 106, 100);
    L.iris = rgb(64, 42, 26);
    L.sleeve = rgb(226, 222, 210);
    L.cuff = rgb(226, 222, 210);
    L.bareForearm = true;
    L.trousers = rgb(44, 40, 38);
    L.shoes = rgb(28, 22, 20);
    L.headSpec = 0.30f;
    L.shoulderW = 0.192f;
    L.armR = 0.053f;
    L.upperArm = 0.29f;
    L.foreArm = 0.27f;
    L.handScale = 1.05f;
    L.spineLen = 0.50f;
    L.shoulderY = 0.375f;
    L.headZ = -0.018f;  // a little stooped
    L.hipPivot = {0.f, 0.94f, 0.f};
    return L;
}

HeadOut buildHeadOcakci(const PersonLook& L) {
    HeadOut h;
    Sdf& s = h.s;
    baseHead(s, h.g, L, 1.03f, 0.65f, 1.12f, 0.005f, rgb(204, 120, 94));
    // short grey hair around the back and the sides (a horseshoe), thin on the crown (painted)
    s.ellipsoid({0, 0.103f, 0.026f}, {0.0815f, 0.058f, 0.0855f}, L.hair, 0.010f);
    s.ellipsoid({0, 0.0485f, -0.0925f}, {0.017f, 0.0045f, 0.007f}, L.lip, 0.004f);  // upper lip
    // a heavy salt-and-pepper mustache
    h.setStache(rgb(122, 114, 106), 0.0104f, 0.035f, 0.008f, 0.0570f);
    h.g.headTop = 0.214f;
    PersonLook Lc = L;
    s.paint = [Lc](Vector3 p, Vector3 n, Color c) {
        Color out = skinPaint(Lc, p, n, c, 0.45f);
        const float wh = likeC(c, Lc.hair, 80.f);
        if (wh > 0.f) {
            const float k = 0.84f + 0.3f * noise3(Vector3Scale(p, 700.f), 5u);
            const float edge = smooth01((0.150f - p.y) / 0.02f);  // thinner toward the top edge
            out = mixC(out, mixC(Lc.skin, scaleC(Lc.hair, k), 0.5f + 0.45f * edge), wh);
        }
        // thin grey hair combed back over the crown: the scalp shows through
        const float crown = smooth01((p.y - 0.165f) / 0.02f) * smooth01((p.z + 0.035f) / 0.03f) * likeC(c, Lc.skin);
        if (crown > 0.f) {
            const float comb = 0.5f + 0.5f * std::sin(p.x * 900.f + p.z * 120.f);
            out = mixC(out, scaleC(Lc.hair, 0.9f + 0.12f * comb), 0.42f * crown);
        }
        // grey stubble on the jaw and cheeks
        if (p.y < 0.075f && p.z < -0.02f && std::fabs(p.x) > 0.016f)
            out = mixC(out, rgb(138, 122, 112), 0.20f * smooth01((0.075f - p.y) / 0.025f) * likeC(c, Lc.skin));
        return out;
    };
    return h;
}

TorsoOut torsoOcakci(const PersonLook& L) {
    TorsoOut t;
    Sdf& s = t.body;
    const Color shirt = L.sleeve, apron = rgb(46, 58, 84), skin = L.skin, trousers = L.trousers;
    const TorsoSpec ts{1.02f, 0.55f, 0.012f, 0.050f};
    torsoBase(s, L, ts, shirt, shirt, skin);
    const Neckline nl = findNeckline(s, neckAxis(L, ts));
    addCollar(t.collar, s, nl, false, 0.080f, 0.046f);  // open collar, no tie
    t.collar.paint = [shirt](Vector3 p, Vector3, Color) { return fabric(shirt, p, 0.05f, 91u); };
    const float sy = L.shoulderY;
    s.paint = [=](Vector3 p, Vector3, Color) {
        Color k = fabric(shirt, p, 0.05f, 92u);
        // the open collar: a V of skin
        k = mixC(k, scaleC(skin, 0.95f), vOpening(p, sy - 0.035f, sy + 0.05f, 0.040f, 0.011f));
        // the apron's bib, narrowing toward the chest, its straps up over the shoulders, the ties round the waist
        const float yTop = sy - 0.05f;
        const float half = lerpf(0.150f, 0.088f, clampf((p.y - 0.06f) / (yTop - 0.06f), 0.f, 1.f));
        const float front = ramp(-p.z - 0.015f, 0.03f);
        const float bib = ramp(half - std::fabs(p.x), 0.010f) * ramp(yTop - p.y, 0.010f) * front;
        const float strapX = lerpf(0.082f, 0.056f, clampf((p.y - yTop) / 0.09f, 0.f, 1.f));
        const float strap = ramp(0.010f - std::fabs(std::fabs(p.x) - strapX), 0.006f) * ramp(p.y - yTop + 0.012f, 0.01f) *
                            ramp(sy + 0.07f - p.y, 0.01f);
        const float tie = ramp(0.010f - std::fabs(p.y - 0.078f), 0.006f);
        Color ap = fabric(apron, p, 0.08f, 93u);
        ap = scaleC(ap, 1.f - 0.20f * piping(bib));                            // the stitched hem
        const float pocket = ramp(0.050f - std::fabs(p.x), 0.006f) * ramp(0.012f - std::fabs(p.y - 0.17f) + 0.035f, 0.006f);
        ap = scaleC(ap, 1.f - 0.10f * pocket * front);                         // a pocket on the bib
        k = mixC(k, ap, std::max(bib, std::max(strap, tie)));
        // below the waist: the apron's skirt in front (its own mesh carries it down the legs), trousers elsewhere
        const float below = ramp(0.062f - p.y, 0.012f);
        k = mixC(k, mixC(fabric(trousers, p, 0.06f, 94u), ap, front), below);
        return mixC(k, skin, neckSkin(p, nl.baseAt(p)));
    };
    return t;
}

// The apron's skirt: a curved sheet in front of the standing legs, from the waist to below the knees, with a few soft
// folds. Character-local (the standing root): hips at y 0.94, forward = -Z.
Mesh buildApronSkirtMesh() {
    MeshBuilder b;
    const int NA = 14, NY = 7;
    const float y0 = 0.985f, y1 = 0.40f;
    const Color base = rgb(46, 58, 84);
    std::vector<int> idx((NA + 1) * (NY + 1));
    for (int j = 0; j <= NY; ++j) {
        const float v = (float)j / NY;
        const float y = lerpf(y0, y1, v);
        const float R = lerpf(0.158f, 0.178f, v);            // a little flare toward the hem
        const float span = lerpf(1.25f, 1.32f, v);           // half angle around the front (radians)
        for (int i = 0; i <= NA; ++i) {
            const float u = (float)i / NA;
            const float a = lerpf(-span, span, u);
            const float fold = 1.f + 0.035f * v * std::sin(a * 7.f + 0.6f);
            const float r = R * fold;
            Vector3 p{r * std::sin(a), y, 0.035f - r * std::cos(a)};
            Vector3 n = vnorm({std::sin(a), 0.f, -std::cos(a)});
            float k = 0.92f + 0.10f * noise1(u * 9.f + v * 3.f, 95u) - 0.08f * v * (0.5f + 0.5f * std::sin(a * 7.f + 0.6f));
            if (j == NY) k *= 0.8f;  // hem
            idx[(size_t)(j * (NA + 1) + i)] = b.vertex(p, n, {u, v}, scaleC(base, k));
        }
    }
    for (int j = 0; j < NY; ++j)
        for (int i = 0; i < NA; ++i) {
            const int a = idx[(size_t)(j * (NA + 1) + i)], c = idx[(size_t)(j * (NA + 1) + i + 1)];
            const int d = idx[(size_t)((j + 1) * (NA + 1) + i)], e = idx[(size_t)((j + 1) * (NA + 1) + i + 1)];
            b.quad(a, c, e, d);  // counter-clockwise seen from the front (outside)
        }
    return b.build(true);
}

}  // namespace

PersonLook lookOcakci() { return ocakciLookImpl(); }

void buildOcakciPerson(PersonMeshes& pm, Mesh& apronSkirt, Renderer& r) {
    const PersonLook L = ocakciLookImpl();
    MeshJobs J;
    HeadOut h = buildHeadOcakci(L);
    pm.face = h.g;
    pm.hasFace = true;
    queueSdf(J, h.s, 0.0031f, 11000, &pm.head);
    if (h.stache)
        queueSdf(J, mustacheSdf(h.stacheCol, h.thick, h.span, h.droop, h.y, 204u), 0.0011f, 1800, &pm.stache);
    queueTorso(J, [L]() { return torsoOcakci(L); }, kCellTorso, 22000, &pm.torso);
    queueSdf(J, browSdf(L.browCol, 0.0054f, 1.1f, 104u), 0.0011f, 500, &pm.brow[0], &pm.brow[1]);
    J.run();
    Mesh up = buildUpperArm(L, false);
    pm.upper[0] = up;
    pm.upper[1] = mirrorMeshX(up);
    Mesh fo = buildForeArm(L, true);  // sleeves rolled up to the elbow, bare forearms
    pm.fore[0] = fo;
    pm.fore[1] = mirrorMeshX(fo);
    makePersonMats(pm, L, r);
    apronSkirt = buildApronSkirtMesh();
}
