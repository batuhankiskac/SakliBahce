// Characters module: what the player's own hands (r3d::PlayerHands) borrow from the cast — the shared hand meshes and
// the player's tea glass (glass[0]): its place, its level, held by the player's hand (TeaGlass::holder 0, the glass
// follows playerGlassW) and its colour (Ayarlar "Sen" -> bardak).
#include "r3d/CharactersState.h"

namespace r3d {

namespace {
// Tints of the coloured glasses (the ince belli's own vertex colours are a clear white, alpha 46): coloured glass is
// denser, so the material's alpha multiplier raises it too.
constexpr Color kGlassTint[4] = {{255, 255, 255, 255}, {120, 220, 150, 255}, {120, 170, 255, 255}, {200, 130, 235, 255}};
constexpr float kGlassAlpha[4] = {1.f, 2.6f, 2.6f, 2.6f};
} // namespace

namespace chr {
void Cast::freePlayerGlass(Renderer& r) {
    for (Mat& m : playerGlassMat)
        if (m.material.maps) {
            r.unloadMat(m);
            m = Mat{};
        }
    playerGlassStyle = 0;
}
} // namespace chr

const Mesh* Characters::handMesh(bool left, int pose) const {
    const Impl& m = *impl_;
    if (!m.ready || pose < 0 || pose >= chr::HAND_POSES) return nullptr;
    return &m.M.hand[left ? 1 : 0][pose];
}

bool Characters::playerGlass(Vector3& base, float& level) const {
    const Impl& m = *impl_;
    if (!m.ready) return false;
    const chr::TeaGlass& g = m.glass[0];
    base = g.rest;
    level = g.level;
    return g.holder < 0 || g.holder == 0;
}

float Characters::glassHeight() const { return impl_->M.tulipH; }

bool Characters::holdPlayerGlass(const Matrix* world) {
    Impl& m = *impl_;
    if (!m.ready) return false;
    chr::TeaGlass& g = m.glass[0];
    if (!world) {
        if (g.holder == 0) {
            g.holder = -1;
            g.from = g.world;
            g.blend = 0.f;
            m.sfx(ui::Sfx::GlassSet);
        }
        return true;
    }
    if (g.holder > 0) return false;  // the çaycı is filling it
    if (g.holder < 0) {
        g.holder = 0;
        g.from = g.world;
        g.blend = 0.f;
    }
    m.playerGlassW = *world;
    return true;
}

void Characters::sipPlayerGlass() {
    Impl& m = *impl_;
    chr::TeaGlass& g = m.glass[0];
    if (g.holder == 0) g.level = std::max(0.f, g.level - m.rng.f(0.08f, 0.13f));
}

void Characters::setPlayerGlassStyle(int style) {
    Impl& m = *impl_;
    style = std::clamp(style, 0, 3);
    if (style > 0 && !m.playerGlassMat[style].material.maps && m.renderer) {
        Mat& g = m.playerGlassMat[style];
        g = m.renderer->makeMat(kGlassTint[style], Texture2D{}, 0.95f, 150.f, 0.f, 0.75f);
        g.alpha = kGlassAlpha[style];
    }
    m.playerGlassStyle = style > 0 && m.playerGlassMat[style].material.maps ? style : 0;
}

} // namespace r3d
