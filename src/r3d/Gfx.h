#pragma once
// 3D rendering foundation shared by Room, Characters and Table3D. PUBLIC API FROZEN.
// Implementation: src/r3d/Gfx.cpp (+ GfxMesh.cpp, GfxTexture.cpp) — gfx owner.
//
// Model: every frame, modules SUBMIT what they want drawn (meshes + materials + transforms, lights, glows,
// smoke particles) to the Renderer; App then calls Renderer::render(camera), which performs the shadow
// pass (key light), the opaque lit pass, transparent objects (sorted back-to-front), smoke/steam
// particles and additive glows. Modules never call BeginMode3D themselves.
//
// Lighting: Blinn-Phong with a hemispheric ambient, up to MAX_POINT_LIGHTS point lights, one shadow-casting
// key spot light (the pendant lamp over our table), distance fog and a height haze under the ceiling
// ("dumanaltı"). Colours/textures are sRGB; lighting is done in linear space with a soft tone curve.
#include <raylib.h>
#include <cstdint>
#include <string>
#include <vector>

namespace r3d {

// ---------------------------------------------------------------- materials
struct Mat {
    Material material{};    // raylib material: lit shader + albedo map (texture + colour)
    float specular = 0.15f; // 0..1 specular strength
    float shininess = 24.f; // Blinn-Phong exponent (4..256)
    float emissive = 0.f;   // 0..1: 1 = fully self-lit (bulbs, TV screen, embers, sky outside)
    float rim = 0.f;        // 0..1 fresnel rim (soft cloth/skin, glass edges)
    float alpha = 1.f;      // < 1 => draw it with the Transparent flag
};

// ---------------------------------------------------------------- lights & atmosphere
constexpr int MAX_POINT_LIGHTS = 12;
struct PointLight {
    Vector3 position{0, 2, 0};
    Color color{255, 196, 130, 255};
    float intensity = 1.f;  // linear multiplier
    float range = 4.f;      // metres: smooth falloff reaching 0 at `range`
};
struct KeyLight {           // spot light that casts shadows
    Vector3 position{0, 1.86f, 0};
    Vector3 target{0, 0.76f, 0};
    Color color{255, 206, 150, 255};
    float intensity = 3.f;
    float innerDeg = 38.f;  // full intensity inside
    float outerDeg = 72.f;  // zero outside (half angles)
    float range = 6.f;
    bool shadows = true;
};
struct Ambient {
    Color sky{120, 96, 72, 255};   // light from above (smoky ceiling bounce)
    Color ground{46, 34, 26, 255}; // light from below
    float intensity = 0.55f;
};
struct Fog {
    Color color{92, 76, 60, 255};  // smoky warm grey-brown
    float density = 0.10f;         // exp distance fog per metre
    float hazeStart = 1.7f;        // height where the ceiling smoke layer begins
    float hazeTop = 3.1f;          // ceiling
    float hazeDensity = 0.55f;     // extra fog inside the smoke layer
};

// ---------------------------------------------------------------- draw flags
enum DrawFlags : uint32_t {
    CastShadow = 1u << 0,   // rendered into the key light's shadow map
    Transparent = 1u << 1,  // drawn after opaques, sorted far->near, alpha blended, no depth write
    Additive = 1u << 2,     // (with Transparent) additive blending — light shafts, glows
    DoubleSided = 1u << 3,  // disable back-face culling for this draw
    NoFog = 1u << 4,        // skip fog/haze (e.g. things right in front of the camera)
};

// ---------------------------------------------------------------- smoke & steam particles
struct SmokeParams {
    Vector3 velocity{0, 0.06f, 0};
    float size0 = 0.03f, size1 = 0.25f; // metres at birth / death
    float life = 5.f;                    // seconds
    float alpha = 0.35f;                 // peak opacity
    Color color{200, 196, 188, 255};
    float turbulence = 1.f;              // curl-ish wobble strength
    float buoyancy = 0.02f;              // upward acceleration (m/s^2)
};

// ---------------------------------------------------------------- renderer
class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init();              // after InitWindow: shaders, shadow map, particle textures
    void shutdown();
    void update(float dt);    // particles, time
    float time() const;

    // Size of the target render() will draw into (window size in points, or a RenderTexture's size).
    // App sets it every frame; projections for 2D overlays use it.
    void setRenderSize(int width, int height);
    int renderWidth() const;
    int renderHeight() const;

    // --- per-frame lighting (Room sets these; others may add point lights) ---
    void setAmbient(const Ambient& a);
    void setFog(const Fog& f);
    void setKeyLight(const KeyLight& k);
    void addPointLight(const PointLight& p);  // cleared after every render()

    // --- materials ---
    // Lit material; `texture.id == 0` means plain colour (1x1 white texture).
    Mat makeMat(Color albedo, Texture2D texture = Texture2D{}, float specular = 0.15f, float shininess = 24.f,
                float emissive = 0.f, float rim = 0.f);
    void unloadMat(Mat& m);  // frees the material's map array only (not the shader or the texture)

    // --- submission (valid until the next render(); pointers must stay alive until then) ---
    void submit(const Mesh* mesh, const Mat* mat, const Matrix& transform, uint32_t flags = CastShadow);
    // Additive camera-facing soft glow (bulbs, embers, TV light, window light).
    void submitGlow(Vector3 pos, float radius, Color color, float intensity = 1.f);
    // Camera-facing textured quad (e.g. speech-free signs, far silhouettes). `additive` or alpha blended.
    void submitBillboard(Texture2D tex, Rectangle src, Vector3 pos, Vector2 size, Color tint, bool additive = false);

    // --- particles ---
    void emitSmoke(Vector3 pos, const SmokeParams& p);  // one puff
    int particleCount() const;

    // --- frame ---
    // Draws everything submitted this frame with `cam`, then clears the submission lists and point lights.
    // Call it inside BeginDrawing() but NEVER inside BeginTextureMode() (the shadow pass switches render
    // targets): to render into a RenderTexture pass it as `target` (it is cleared to `clearColor` first).
    // Without a target it draws into the window's framebuffer (clear it yourself with ClearBackground).
    void render(const Camera3D& cam, RenderTexture2D* target = nullptr, Color clearColor = BLACK);
    const Camera3D& lastCamera() const;

    // Project a world point to the current UI virtual canvas (ui::currentViewport() + renderWidth/Height).
    // Returns false if the point is behind the camera.
    bool projectToVirtual(Vector3 world, Vector2& outVirtual) const;
    // Ray from a virtual-canvas point (e.g. ui::virtualMouse()) into the world.
    Ray rayFromVirtual(Vector2 virtualPos) const;

    Shader litShader() const;
    Texture2D whiteTexture() const;
    Texture2D puffTexture() const;  // soft smoke puff (for custom billboards)
    Texture2D glowTexture() const;  // radial glow

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

// ---------------------------------------------------------------- mesh building
// All generated meshes are uploaded (ready to draw) and have positions, normals and texcoords.
class MeshBuilder {
public:
    void clear();
    int vertexCount() const;
    // Append a vertex; returns its index.
    int vertex(Vector3 p, Vector3 n, Vector2 uv, Color c = WHITE);
    void triangle(int a, int b, int c);                  // counter-clockwise = front
    void quad(int a, int b, int c, int d);               // a-b-c-d counter-clockwise
    // Convenience primitives (in the builder's current transform).
    void box(Vector3 center, Vector3 size, Color c = WHITE);                 // UVs per face 0..1
    void roundedBox(Vector3 center, Vector3 size, float radius, int seg, Color c = WHITE);
    // Surface of revolution around +Y. profile = (radius, y) points from bottom to top. UV: u around,
    // v along the profile. capBottom/capTop close the ends with discs.
    void lathe(const std::vector<Vector2>& profile, int segments, bool capBottom, bool capTop, Color c = WHITE);
    void cylinder(Vector3 base, float radius, float height, int segments, Color c = WHITE);  // capped
    void sphere(Vector3 center, float radius, int rings, int slices, Color c = WHITE);
    void ellipsoid(Vector3 center, Vector3 radii, int rings, int slices, Color c = WHITE);
    void capsule(Vector3 a, Vector3 b, float radius, int segments, Color c = WHITE);        // between points
    void tube(const std::vector<Vector3>& path, float radius, int segments, Color c = WHITE); // swept circle
    void plane(Vector3 center, Vector2 size, Vector3 normal, Vector2 uvScale = {1, 1}, Color c = WHITE);
    // Transform applied to subsequently added vertices (identity by default).
    void setTransform(const Matrix& m);
    void resetTransform();
    Mesh build(bool withColors = false) const;  // uploads to GPU
private:
    std::vector<float> pos_, nrm_, uv_;
    std::vector<unsigned char> col_;
    std::vector<unsigned int> idx_;
    Matrix xf_{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    Matrix nxf_{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // inverse-transpose of xf_ for normals
    bool hasXf_ = false;
};

// Rounded box centred at the origin (convenience).
Mesh genRoundedBox(float w, float h, float d, float radius, int seg = 3);
Mesh genLathe(const std::vector<Vector2>& profile, int segments, bool capBottom, bool capTop);

// ---------------------------------------------------------------- procedural textures
// All returned textures have mipmaps, trilinear filtering and REPEAT wrapping. `seed` varies the pattern.
Texture2D genWoodTexture(int size, Color light, Color dark, uint32_t seed, float rings = 18.f);
Texture2D genFeltTexture(int size, Color base, uint32_t seed);
Texture2D genNoiseTexture(int size, Color a, Color b, float scale, uint32_t seed);
Texture2D genPlasterTexture(int size, Color base, uint32_t seed);   // stained, uneven wall paint
Texture2D textureFromImage(Image img);                              // upload + mipmaps + trilinear + repeat
// Render 2D content (in pixel units, y down) into a texture for 3D surfaces (chalkboards, TV screen,
// signs). Keep the returned RenderTexture alive; redraw it whenever its content changes. Canvas textures
// are stored upside-down (OpenGL convention) — put them on a genCanvasQuad(), which flips v for you.
RenderTexture2D makeCanvas(int width, int height);
void beginCanvas(RenderTexture2D& canvas);  // BeginTextureMode + clear transparent
void endCanvas(RenderTexture2D& canvas);    // EndTextureMode (+ regenerate mipmaps)
Mesh genCanvasQuad(float width, float height); // quad in the XY plane centred at the origin, facing +Z

// Transform helpers
Matrix trs(Vector3 t, Vector3 rotDeg, Vector3 s);  // scale, then rotate X, Y, Z (degrees), then translate
Matrix trsYaw(Vector3 t, float yawDeg, float scale = 1.f);

} // namespace r3d
