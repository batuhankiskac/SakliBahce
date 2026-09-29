// 3D renderer for SaklıBahçe (gfx owner). Public API: r3d/Gfx.h (frozen).
//
// Frame: submissions are bounded (cached local spheres) and frustum-culled; each keeps a mask of the point
// lights that can reach it or the view rays toward it (exact: lights fall to 0 at their range). Shadow pass
// (key spot light: PCSS soft shadows with receiver-plane depth from a float depth map + hardware PCF, plus
// contact occlusion read from the same map), opaque lit pass after a depth pre-pass, one far->near sorted list
// of transparent meshes + smoke/steam particles + billboards, additive glows, then a tiny full-screen blend
// pass (vignette + film grain) that keeps the default framebuffer's MSAA intact. Lighting is done in linear
// HDR: Blinn-Phong with Fresnel, wrap/rim terms for cloth and skin, hemispheric ambient plus a warm bounce from
// the lit table, height fog whose colour is lit by the lamps (ray-marched key-light in-scatter and closed-form
// point-light in-scatter), then a Hable filmic curve with a gentle warm/cool split tone.
#include "r3d/Gfx.h"
#include "ui/Common.h"

#include <raymath.h>
#include <rlgl.h>

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#error "r3d/Gfx.cpp needs an OpenGL 3.3 header for depth textures and sampler objects"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace r3d {

// ============================================================================ tuning
namespace {

constexpr int SHADOW_SIZE = 2048;
constexpr float SHADOW_MAX_HALF_DEG = 60.f;  // shadow frustum half angle (tighter than the light cone)
constexpr float SHADOW_NEAR = 0.15f;       // clips the bulb itself (submitted with default flags) out of the map
constexpr float KEY_LIGHT_SIZE = 0.07f;      // metres: bulb + enamel shade mouth -> penumbra width
constexpr float BOUNCE_STRENGTH = 0.07f;     // warm fill reflected by the lit table top
constexpr float KEY_SCATTER = 0.14f;         // in-scatter strength of the key light (lamp cone in smoke)
constexpr float POINT_SCATTER = 0.11f;       // in-scatter strength of point lights (halo around lamps)
constexpr float SCATTER_G = 0.35f;           // Henyey-Greenstein anisotropy of tobacco smoke
constexpr float SCATTER_K = 1.55f * SCATTER_G - 0.55f * SCATTER_G * SCATTER_G * SCATTER_G;  // Schlick k
constexpr float HAZE_SCALE = 0.9f;           // Fog::hazeDensity -> extinction per metre at the ceiling
constexpr float FOG_AMBIENT = 0.55f;         // unlit smoke radiance relative to Fog::color (lamps add the rest)
constexpr float MIN_TRANSMITTANCE = 0.06f;   // walls never vanish completely
constexpr float HAZE_WAVE = 0.22f;           // metres the underside of the smoke layer rolls up and down
constexpr float EXPOSURE = 2.0f;
constexpr float VIGNETTE = 0.30f;
constexpr float GRAIN = 0.035f;
constexpr int MAX_PARTICLES = 1400;
constexpr float ATT_K = 0.66f;               // light falloff: w(d)^2 * ATT_K / (d^2 + ATT_R2)
constexpr float ATT_R2 = 0.1225f;

// Textures known to be fully opaque (generated here, or uploaded through textureFromImage without any
// translucent pixel). Opaque submissions using them take the depth pre-pass; anything else (canvases,
// foreign textures, albedo alpha < 255) keeps plain blended drawing so transparent-background quads work.
std::unordered_set<unsigned int>& opaqueTextures() {
    static std::unordered_set<unsigned int> set;
    return set;
}

// ============================================================================ shaders
const char* kLitVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
invariant gl_Position;
void main() {
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char* kLitFS = R"(#version 330
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec4 matParams;      // specular, shininess, emissive, rim
uniform vec4 matMode;        // alpha multiplier, blend kind (0 opaque, 1 alpha, 2 additive), fog on, -

uniform vec3 viewPos;
uniform vec3 ambSky;
uniform vec3 ambGround;
uniform int numLights;
uniform int lightMask;       // bit i: point light i can reach this draw (CPU culled by range)
uniform vec4 lightPosRange[12];
uniform vec3 lightColor[12];

uniform vec3 keyPos;
uniform vec3 keyDir;
uniform vec3 keyColor;
uniform vec4 keyParams;      // cos inner, cos outer, range, light size (m)
uniform vec4 bounce;         // xyz: centre of the lit table, w: strength

uniform int shadowOn;
uniform sampler2DShadow shadowMap;
uniform sampler2D shadowDepth;
uniform mat4 lightVP;
uniform vec4 shadowParams;   // near, far, 1/size, tan(half fov)
uniform vec3 keyRight;       // the shadow camera's right and up axes (world)
uniform vec3 keyUp;

uniform vec3 fogColor;       // linear, pre-tonemap
uniform vec4 fogParams;      // density /m, haze start, haze top, haze density /m
uniform vec4 scatterParams;  // key in-scatter, point in-scatter, HG anisotropy, min transmittance
uniform vec4 hazeWave;       // amplitude (m), time

out vec4 finalColor;

const float PI = 3.14159265;
const float ATT_K = 0.66;
const float ATT_R2 = 0.1225;
const float EXPOSURE = 2.0;
const float WHITE_SCALE = 1.15300;   // 1 / hable(11.2)

const vec2 POISSON[16] = vec2[16](
    vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725), vec2(-0.09418410, -0.92938870),
    vec2(0.34495938, 0.29387760), vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379), vec2(0.44323325, -0.97511554),
    vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590), vec2(0.19984126, 0.78641367),
    vec2(0.14383161, -0.14100790));

float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

float lightAtt(float d, float range) {
    float x = d / range;
    float w = clamp(1.0 - x * x * x * x, 0.0, 1.0);
    return w * w * ATT_K / (d * d + ATT_R2);
}

vec3 hable(vec3 x) { return (x * (0.22 * x + 0.03) + 0.002) / (x * (0.22 * x + 0.30) + 0.06) - 0.0333333; }
vec3 tonemap(vec3 c) {
    vec3 x = hable(max(c, 0.0) * EXPOSURE) * WHITE_SCALE;
    float l = dot(x, vec3(0.2126, 0.7152, 0.0722));
    x *= mix(vec3(0.965, 0.99, 1.045), vec3(1.025, 1.0, 0.955), smoothstep(0.015, 0.5, l));
    return pow(clamp(x, 0.0, 1.0), vec3(1.0 / 2.2));
}

// ---- key light shadow: PCSS (blocker search on raw depth, then rotated Poisson PCF with hardware compare)
float linDepth(float d) {
    float n = shadowParams.x, f = shadowParams.y;
    return (2.0 * n * f) / (f + n - (d * 2.0 - 1.0) * (f - n));
}
float bufDepth(float z) {
    float n = shadowParams.x, f = shadowParams.y;
    return ((f + n) / (f - n) - (2.0 * f * n) / ((f - n) * z)) * 0.5 + 0.5;
}
// Returns the key light visibility and, in `ao`, contact occlusion measured on the shadow map, which from a
// lamp hanging over the table is a height field: nearby samples standing a little above the receiver
// (tiles on the felt, racks, glasses, hands) darken it softly. Every tap is compared against the receiver's
// own plane extrapolated to that tap (receiver-plane depth), so thin casters such as an 11 mm tile lying on
// the felt register fully without acne and without a slope bias that would detach their shadows.
float keyShadow(vec3 P, vec3 N, vec3 L, float ndl, float rot, out float ao) {
    ao = 0.0;
    float z = (lightVP * vec4(P, 1.0)).w;              // view depth along the light axis
    if (z <= shadowParams.x) return 1.0;
    float uvPerM = 1.0 / (2.0 * z * shadowParams.w);   // shadow-map uv per metre at that depth
    float texel = shadowParams.z / uvPerM;              // metres per shadow texel
    float sinT = sqrt(max(1.0 - ndl * ndl, 0.0));
    vec4 c = lightVP * vec4(P + N * (texel * (0.6 + 1.2 * sinT)), 1.0);   // normal offset
    vec2 uv = c.xy / c.w * 0.5 + 0.5;
    float edge = min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y));
    if (edge <= 0.0) return 1.0;
    float fade = smoothstep(0.0, 0.04, edge);
    float zr = c.w;
    // depth change of the receiver plane per metre of offset along the light's right/up axes
    vec2 grad = vec2(dot(N, keyRight), dot(N, keyUp)) * (dot(-L, keyDir) / max(ndl, 0.3));
    float gl = length(grad);
    if (gl > 3.0) grad *= 3.0 / gl;
    float cr = cos(rot), sr = sin(rot);
    mat2 R = mat2(cr, sr, -sr, cr);
    // one ring of raw depth taps serves both the blocker search and the contact occlusion
    const float AO_R = 0.015;
    float searchUV = clamp(AO_R * uvPerM, 3.0 * shadowParams.z, 40.0 * shadowParams.z);
    float searchM = searchUV / uvPerM;
    float bias = texel * 1.2;
    float bsum = 0.0, bn = 0.0, occ = 0.0, wsum = 0.0;
    for (int i = 0; i < 16; i += 2) {
        vec2 o = R * POISSON[i];
        float zb = linDepth(texture(shadowDepth, uv + o * searchUV).r);
        float h = zr + dot(grad, o) * searchM - zb - bias;
        float w = 1.25 - length(o);      // near taps count more
        wsum += w;
        if (h > 0.0) {
            bsum += zb;
            bn += 1.0;
            // ~8 mm of height already occludes fully; tall things far above (hands, the lamp) do not
            occ += w * clamp(h / 0.008, 0.0, 1.0) * clamp(2.0 - h / 0.12, 0.0, 1.0);
        }
    }
    ao = occ / wsum * fade;
    if (bn < 0.5) return 1.0;
    float zb = bsum / bn;
    float pen = keyParams.w * (zr - zb) / zb;           // penumbra width (m) at the receiver
    float rUV = clamp(0.5 * pen * uvPerM, 1.25 * shadowParams.z, 26.0 * shadowParams.z);
    float rM = rUV / uvPerM;
    float s = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec2 o = R * POISSON[i];
        float ref = bufDepth(zr + dot(grad, o) * rM - texel * 0.5);
        s += texture(shadowMap, vec3(uv + o * rUV, ref));
    }
    return mix(1.0, s * (1.0 / 16.0), fade);
}

// ---- smoke: height-varying extinction, lamp-lit in-scatter
// the smoke layer's underside slowly undulates instead of being a flat slab (wavelength ~7 m: evaluated at
// three points per view ray and interpolated, which keeps the trigonometry out of the march)
float waveAt(vec3 x) {
    return hazeWave.x * sin(0.9 * x.x + 0.31 * hazeWave.y) * cos(0.73 * x.z - 0.23 * hazeWave.y + 1.3 * sin(0.4 * x.x));
}
float density(float y, float wave) { return fogParams.x + fogParams.w * smoothstep(fogParams.y + wave, fogParams.z, y); }
// Schlick's approximation of Henyey-Greenstein (k from g), normalised so that isotropic = 1
float phase(float c) {
    float k = scatterParams.z;
    float d = 1.0 - k * c;
    return (1.0 - k * k) / (d * d);
}
float fastAtan(float x) {   // max error ~0.0015 rad
    float a = abs(x);
    float t = a > 1.0 ? 1.0 / a : a;
    float r = 0.7853982 * t - t * (t - 1.0) * (0.2447 + 0.0663 * t);
    r = a > 1.0 ? 1.5707963 - r : r;
    return sign(x) * r;
}
vec3 applyFog(vec3 col, vec3 P, bool additive, float jitter) {
    vec3 rd = P - viewPos;
    float len = length(rd);
    rd /= max(len, 1e-4);
    const int N = 6;
    float dt = len / float(N);
    float od = 0.0;
    float sk = 0.0;
    // quadratic through the wave at the eye, the midpoint and the surface: w(t) = wa + t * (wb + t * wc)
    float w0 = waveAt(viewPos), wm = waveAt(viewPos + rd * (0.5 * len)), w1 = waveAt(P);
    float wa = w0, wb = 4.0 * wm - 3.0 * w0 - w1, wc = 2.0 * (w0 + w1) - 4.0 * wm;
    for (int i = 0; i < N; ++i) {
        float t = (float(i) + jitter) / float(N);
        vec3 x = viewPos + rd * (t * len);
        float dens = density(x.y, wa + t * (wb + t * wc));
        vec3 lv = keyPos - x;
        float d2 = dot(lv, lv);
        float inv = inversesqrt(max(d2, 1e-8));
        vec3 L = lv * inv;
        float cone = smoothstep(keyParams.y, keyParams.x, dot(-L, keyDir));
        float xr = d2 / (keyParams.z * keyParams.z);
        float w = clamp(1.0 - xr * xr, 0.0, 1.0);
        sk += dens * exp2(-1.442695 * od) * (w * w * ATT_K / (d2 + ATT_R2)) * cone * phase(dot(L, rd));
        od += dens * dt;
    }
    vec3 sc = keyColor * sk;
    float T = max(exp(-od), scatterParams.w);
    if (additive) return col * T;
    sc *= dt * scatterParams.x;
    for (int i = 0; i < 12; ++i) {
        if (i >= numLights) break;
        if (((lightMask >> i) & 1) == 0) continue;
        vec3 toL = lightPosRange[i].xyz - viewPos;
        float t0 = dot(toL, rd);
        float h2 = max(dot(toL, toL) - t0 * t0, 0.0);
        float xr = sqrt(h2) / lightPosRange[i].w;
        if (xr >= 1.0) continue;
        float w = 1.0 - xr * xr * xr * xr;
        float H = sqrt(h2 + ATT_R2);
        float I = (fastAtan((len - t0) / H) + fastAtan(t0 / H)) / H;
        float tc = clamp(t0, 0.0, len);
        float tn = tc / max(len, 1e-4);
        float dens = density(viewPos.y + rd.y * tc, wa + tn * (wb + tn * wc));
        sc += lightColor[i] * (scatterParams.y * ATT_K * I * w * w * dens * exp(-od * tc / max(len, 1e-4)));
    }
    return col * T + fogColor * (1.0 - T) + sc;
}

void main() {
    vec4 base = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    vec3 albedo = pow(base.rgb, vec3(2.2));
    float alpha = base.a * matMode.x;
    int kind = int(matMode.y + 0.5);
    vec3 P = fragPosition;
    vec3 N = normalize(fragNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 V = normalize(viewPos - P);
    float NdV = max(dot(N, V), 0.0);
    float specK = matParams.x;
    float shin = max(matParams.y, 1.0);
    float emis = clamp(matParams.z, 0.0, 1.0);
    float rim = clamp(matParams.w, 0.0, 1.0);
    float wrap = rim * 0.45;
    float specNorm = (shin + 8.0) / (8.0 * PI);
    float noise = ign(gl_FragCoord.xy);

    vec3 amb = mix(ambGround, ambSky, N.y * 0.5 + 0.5);
    vec3 diff = vec3(0.0);
    vec3 spec = vec3(0.0);
    vec3 back = ambSky * 0.5;
    float ao = 0.0;

    {   // key spot light (shadowed)
        vec3 lv = keyPos - P;
        float d = length(lv);
        vec3 L = lv / max(d, 1e-4);
        float att = lightAtt(d, keyParams.z) * smoothstep(keyParams.y, keyParams.x, dot(-L, keyDir));
        if (att > 1e-5) {
            float ndl = dot(N, L);
            float ndw = clamp((ndl + wrap) / (1.0 + wrap), 0.0, 1.0);
            float sh = 1.0;
            if (shadowOn == 1 && ndw > 0.0) sh = keyShadow(P, N, L, max(ndl, 0.0), noise * 6.2831853, ao);
            vec3 c = keyColor * att;
            diff += c * (ndw * sh * (1.0 - 0.55 * ao));   // the big enamel shade is an area light, too
            if (specK > 0.0) {
                vec3 H = normalize(L + V);
                float f1 = 1.0 - max(dot(V, H), 0.0);
                float f2 = f1 * f1;
                float fr = specK * (1.0 + 2.0 * f2 * f2 * f1);
                spec += c * (sh * fr * specNorm * pow(max(dot(N, H), 0.0), shin) * max(ndl, 0.0));
            }
            back += c * ((0.35 + 0.65 * sh) * (0.25 + 0.75 * max(dot(-V, L), 0.0)));
        }
    }
    for (int i = 0; i < 12; ++i) {
        if (i >= numLights) break;
        if (((lightMask >> i) & 1) == 0) continue;
        vec3 lv = lightPosRange[i].xyz - P;
        float d = length(lv);
        float att = lightAtt(d, lightPosRange[i].w);
        if (att <= 1e-5) continue;
        vec3 L = lv / max(d, 1e-4);
        float ndl = dot(N, L);
        vec3 c = lightColor[i] * att;
        diff += c * clamp((ndl + wrap) / (1.0 + wrap), 0.0, 1.0);
        if (specK > 0.0 && ndl > 0.0) {
            vec3 H = normalize(L + V);
            float f1 = 1.0 - max(dot(V, H), 0.0);
            float f2 = f1 * f1;
            float fr = specK * (1.0 + 2.0 * f2 * f2 * f1);
            spec += c * (fr * specNorm * pow(max(dot(N, H), 0.0), shin) * ndl);
        }
        back += c * (0.25 + 0.75 * max(dot(-V, L), 0.0));
    }
    {   // warm light bounced off the lit table top onto faces, hands, chins, rack undersides
        vec3 bv = bounce.xyz - P;
        float bd = length(bv);
        float k = bounce.w * step(bounce.y - 0.03, P.y) * max(dot(N, bv / max(bd, 1e-4)), 0.0);
        diff += keyColor * (k / (1.0 + 5.0 * bd * bd)) * (1.0 - 0.7 * ao);
    }
    diff += amb * (1.0 - 0.9 * ao);

    vec3 color = albedo * diff + spec * (1.0 - 0.5 * ao);
    float f1v = 1.0 - NdV;
    float f2v = f1v * f1v;
    float f5 = f2v * f2v * f1v;
    vec3 R = reflect(-V, N);
    color += mix(ambGround, ambSky, R.y * 0.5 + 0.5) * (1.3 * specK * (0.04 + 0.96 * f5));
    float amax = max(max(albedo.r, albedo.g), max(albedo.b, 0.02));
    color += back * (rim * 0.8 * f2v * f1v) * mix(vec3(1.0), albedo / amax, 0.45);
    color = mix(color, albedo * 1.5, emis);

    if (kind == 1)
        alpha = clamp(alpha + (1.0 - alpha) * f5 * max(rim, specK) + dot(spec, vec3(0.3, 0.5, 0.2)) * 0.5, 0.0, 1.0);
    if (matMode.z > 0.5) color = applyFog(color, P, kind == 2, fract(noise + 0.37));
    color = tonemap(color) + (noise - 0.5) * (1.0 / 255.0);
    finalColor = vec4(color, alpha);
}
)";

const char* kDepthVS = R"(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
invariant gl_Position;
void main() { gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";

const char* kDepthFS = R"(#version 330
out vec4 finalColor;
void main() { finalColor = vec4(1.0); }
)";

// Camera-facing sprites (smoke, steam, billboards, glows): colour is lit on the CPU.
const char* kSpriteVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 uv;
out vec4 col;
void main() {
    uv = vertexTexCoord;
    col = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";
const char* kSpriteFS = R"(#version 330
in vec2 uv;
in vec4 col;
uniform sampler2D texture0;
out vec4 finalColor;
void main() { finalColor = texture(texture0, uv) * col; }
)";

// Full-screen overlay blended as dst = src.rgb + dst * src.a: vignette + film grain + a faint warm lift.
const char* kPostVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
out vec2 uv;
void main() { uv = vertexTexCoord; gl_Position = vec4(vertexPosition.xy, 0.0, 1.0); }
)";

const char* kPostFS = R"(#version 330
in vec2 uv;
uniform vec4 postParams;   // aspect, frame seed, vignette, grain
out vec4 finalColor;
float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
void main() {
    vec2 p = (uv - 0.5) * vec2(postParams.x, 1.0);
    float r = length(p) / (0.5 * length(vec2(postParams.x, 1.0)));
    float vig = 1.0 - postParams.z * pow(smoothstep(0.25, 1.05, r), 1.6);
    vec2 q = floor(gl_FragCoord.xy) + vec2(postParams.y * 37.0, postParams.y * 91.0);
    float n = hash(q) + hash(q + 17.3) - 1.0;   // triangular, -1..1
    float g = postParams.w;
    finalColor = vec4(vec3(0.0045, 0.0032, 0.0020) + max(n, 0.0) * g * 0.02, vig * (1.0 - g + g * n));
}
)";

// ============================================================================ noise
uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
float hash2(int x, int y, uint32_t seed) {
    return (hash32((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ seed * 83492791u) & 0xffffff) /
           16777215.f;
}
float rnd(uint32_t& s) {
    s = hash32(s + 0x9e3779b9u);
    return (s & 0xffffff) / 16777215.f;
}
float smooth(float t) { return t * t * (3.f - 2.f * t); }
float smoothstepf(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
int wrapi(int i, int n) { return ((i % n) + n) % n; }
// Tileable value noise with periods px, py (lattice units).
float valueNoise(float x, float y, int px, int py, uint32_t seed) {
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    float fx = x - x0, fy = y - y0;
    int xa = wrapi(x0, px), xb = wrapi(x0 + 1, px), ya = wrapi(y0, py), yb = wrapi(y0 + 1, py);
    float a = hash2(xa, ya, seed), b = hash2(xb, ya, seed), c = hash2(xa, yb, seed), d = hash2(xb, yb, seed);
    float u = smooth(fx), v = smooth(fy);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
// Tileable fbm over the unit square (u, v in [0,1)), base frequency fx x fy lattice cells.
float fbm2(float u, float v, int fx, int fy, int octaves, uint32_t seed) {
    float sum = 0.f, amp = 0.5f, norm = 0.f;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * valueNoise(u * fx, v * fy, fx, fy, seed + (uint32_t)o * 131u);
        norm += amp;
        amp *= 0.5f;
        fx *= 2;
        fy *= 2;
    }
    return sum / norm;
}
float fbm(float u, float v, int freq, int octaves, uint32_t seed) { return fbm2(u, v, freq, freq, octaves, seed); }

Color mixc(Color a, Color b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}
unsigned char u8(float v) { return (unsigned char)std::clamp(v + 0.5f, 0.f, 255.f); }
Color scalec(Color c, float kr, float kg, float kb) { return Color{u8(c.r * kr), u8(c.g * kg), u8(c.b * kb), c.a}; }

// ============================================================================ colour pipeline (CPU mirror)
float srgbToLin(float v) { return std::pow(v, 2.2f); }
Vector3 lin(Color c, float k) {
    return {srgbToLin(c.r / 255.f) * k, srgbToLin(c.g / 255.f) * k, srgbToLin(c.b / 255.f) * k};
}
float hable(float x) { return (x * (0.22f * x + 0.03f) + 0.002f) / (x * (0.22f * x + 0.30f) + 0.06f) - 0.0333333f; }
constexpr float WHITE_SCALE = 1.15300f;
// Same curve and split tone as the shader, linear HDR -> display 0..1.
Vector3 tonemapCPU(Vector3 c) {
    float x = hable(std::max(c.x, 0.f) * EXPOSURE) * WHITE_SCALE;
    float y = hable(std::max(c.y, 0.f) * EXPOSURE) * WHITE_SCALE;
    float z = hable(std::max(c.z, 0.f) * EXPOSURE) * WHITE_SCALE;
    float l = 0.2126f * x + 0.7152f * y + 0.0722f * z;
    float t = smoothstepf(0.015f, 0.5f, l);
    x *= 0.965f + (1.025f - 0.965f) * t;
    y *= 0.99f + (1.0f - 0.99f) * t;
    z *= 1.045f + (0.955f - 1.045f) * t;
    auto g = [](float v) { return std::pow(std::clamp(v, 0.f, 1.f), 1.f / 2.2f); };
    return {g(x), g(y), g(z)};
}
// Linear HDR value that the tone curve maps to display value `d` (0..1), per channel, ignoring the tint.
float invTonemap1(float d) {
    float target = std::pow(std::clamp(d, 0.f, 0.985f), 2.2f);
    float lo = 0.f, hi = 40.f;
    for (int i = 0; i < 48; ++i) {
        float mid = 0.5f * (lo + hi);
        if (hable(mid * EXPOSURE) * WHITE_SCALE < target) lo = mid;
        else hi = mid;
    }
    return 0.5f * (lo + hi);
}
float phaseSchlick(float c) {
    float d = 1.f - SCATTER_K * c;
    return (1.f - SCATTER_K * SCATTER_K) / (d * d);
}
float lightAttCPU(float d, float range) {
    float x = d / std::max(range, 1e-3f);
    float w = std::clamp(1.f - x * x * x * x, 0.f, 1.f);
    return w * w * ATT_K / (d * d + ATT_R2);
}

// ============================================================================ frame data
struct DrawItem {
    const Mesh* mesh;
    const Mat* mat;
    Matrix xf;
    uint32_t flags;
    Vector3 center;  // world bounding sphere (radius < 0: unknown, never culled)
    float radius;
    int lights;      // point-light mask for this draw
};
struct Glow {
    Vector3 pos;
    float radius;
    Color color;
    float intensity;
};
struct Board {
    Texture2D tex;
    Rectangle src;
    Vector3 pos;
    Vector2 size;
    Color tint;
    bool additive;
};
struct Particle {
    Vector3 pos, vel;
    float age, life, s0, s1, alpha, rot, rotSpeed, turb, buoy, seed;
    Color color;
    int variant;  // atlas cell: 0..2 puffs, 3 steam wisp
};
struct SortEntry {
    float dist;
    int kind;  // 0 transparent mesh, 1 particle, 2 billboard
    int index;
    int first, count;  // sprite vertices (kinds 1, 2)
};
struct Sphere {
    Vector3 c;
    float r;
};
struct BoundsEntry {  // cached local bounds of an uploaded mesh, validated against the mesh it came from
    Sphere s;
    const float* vertices;
    int vertexCount;
};
struct Frustum {
    Vector4 p[6];  // inside: dot(p.xyz, x) + p.w >= 0, xyz normalised
    // Gribb-Hartmann planes of a raylib view-projection matrix (math: clip = M * x)
    void from(const Matrix& m) {
        const Vector4 r0{m.m0, m.m4, m.m8, m.m12}, r1{m.m1, m.m5, m.m9, m.m13}, r2{m.m2, m.m6, m.m10, m.m14},
            r3{m.m3, m.m7, m.m11, m.m15};
        auto add = [](Vector4 a, Vector4 b, float k) { return Vector4{a.x + b.x * k, a.y + b.y * k, a.z + b.z * k, a.w + b.w * k}; };
        p[0] = add(r3, r0, 1.f);
        p[1] = add(r3, r0, -1.f);
        p[2] = add(r3, r1, 1.f);
        p[3] = add(r3, r1, -1.f);
        p[4] = add(r3, r2, 1.f);
        p[5] = add(r3, r2, -1.f);
        for (Vector4& q : p) {
            float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
            if (l > 1e-12f) q = {q.x / l, q.y / l, q.z / l, q.w / l};
        }
    }
    bool visible(Vector3 c, float r) const {
        if (r < 0.f) return true;
        for (const Vector4& q : p)
            if (q.x * c.x + q.y * c.y + q.z * c.z + q.w < -r) return false;
        return true;
    }
};
struct SpriteVertex {
    float x, y, z, u, v;
    unsigned char r, g, b, a;
};

} // namespace

// ============================================================================ Renderer::Impl
struct Renderer::Impl {
    Shader lit{}, depth{}, post{}, sprite{};
    Material depthMat{};
    Texture2D white{}, puff{}, puffAtlas{}, glow{}, glowAtlas{};
    unsigned int spriteVao = 0, spriteVbo = 0;
    size_t spriteCap = 0;
    std::vector<SpriteVertex> sv;
    int locSpriteMvp = -1;
    unsigned int shadowFbo = 0, shadowTex = 0, samplerCmp = 0, samplerRaw = 0;
    bool shadowOk = false;
    Matrix lightVP = MatrixIdentity();
    float shadowFar = 6.f, shadowTanHalf = 1.f;

    int locMatParams = -1, locMatMode = -1, locViewPos = -1, locAmbSky = -1, locAmbGround = -1;
    int locNumLights = -1, locLightPosRange = -1, locLightColor = -1;
    int locKeyPos = -1, locKeyDir = -1, locKeyColor = -1, locKeyParams = -1, locBounce = -1;
    int locShadowOn = -1, locShadowMap = -1, locShadowDepth = -1, locLightVP = -1, locShadowParams = -1;
    int locKeyRight = -1, locKeyUp = -1, locLightMask = -1;
    Vector3 shadowRight{1, 0, 0}, shadowUp{0, 0, -1};
    int locFogColor = -1, locFogParams = -1, locScatterParams = -1, locHazeWave = -1;
    int locPostParams = -1;
    int locMvp = -1, locModel = -1, locNormalMat = -1, locColDiffuse = -1, locDepthMvp = -1;
    Matrix viewProj = MatrixIdentity();
    std::vector<int> opaqueA, opaqueB;  // pre-pass-safe opaque items / blended "opaque" items

    Ambient ambient;
    Fog fog;
    KeyLight key;
    std::vector<PointLight> points;
    std::vector<DrawItem> items;
    std::vector<Glow> glows;
    std::vector<Board> boards;
    std::vector<Particle> particles;
    std::vector<SortEntry> sorted;
    std::unordered_map<unsigned int, BoundsEntry> bounds;  // by VAO id: local bounding sphere
    Frustum camFrustum, lightFrustum;
    Camera3D lastCam{};
    int renderW = 1600, renderH = 900;
    float time = 0.f;
    uint32_t emitCounter = 0, frameCounter = 0;
    bool ready = false;

    // per-frame derived lighting (linear), shared by the uniforms and the CPU-lit particles
    Vector3 keyDir{0, -1, 0}, keyLin{0, 0, 0}, ambSkyLin{}, ambGroundLin{}, fogLin{};
    float keyCosIn = 0.8f, keyCosOut = 0.3f;
    int nLights = 0;
    Vector3 lPos[MAX_POINT_LIGHTS]{}, lCol[MAX_POINT_LIGHTS]{};
    float lRange[MAX_POINT_LIGHTS]{};

    // uniform/texture cache inside one pass (lit program bound)
    const Mat* curMat = nullptr;
    unsigned int curTex = 0xffffffffu;
    float curAlpha = -1.f, curKind = -1.f, curFog = -1.f;
    int curMask = -1;

    // ------------------------------------------------------------------ setup
    void setupShadowMap() {
        glGenTextures(1, &shadowTex);
        glBindTexture(GL_TEXTURE_2D, shadowTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, SHADOW_SIZE, SHADOW_SIZE, 0, GL_DEPTH_COMPONENT,
                     GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        GLint prevFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glGenFramebuffers(1, &shadowFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        shadowOk = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);

        GLuint s[2] = {0, 0};
        glGenSamplers(2, s);
        samplerCmp = s[0];
        samplerRaw = s[1];
        glSamplerParameteri(samplerCmp, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(samplerCmp, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(samplerCmp, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(samplerCmp, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(samplerCmp, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(samplerCmp, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glSamplerParameteri(samplerRaw, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glSamplerParameteri(samplerRaw, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glSamplerParameteri(samplerRaw, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(samplerRaw, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(samplerRaw, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        if (!shadowOk) TraceLog(LOG_WARNING, "r3d: shadow framebuffer incomplete, shadows disabled");
    }

    void genSpriteTextures() {
        {
            Image w = GenImageColor(4, 4, WHITE);
            white = LoadTextureFromImage(w);
            UnloadImage(w);
        }
        {   // public single puff (custom billboards) = atlas variant 0 at 128 px
            Image img = GenImageColor(128, 128, Color{255, 255, 255, 0});
            paintPuff((Color*)img.data, 128, 128, 0, 0, 0);
            puff = LoadTextureFromImage(img);
            GenTextureMipmaps(&puff);
            SetTextureFilter(puff, TEXTURE_FILTER_TRILINEAR);
            UnloadImage(img);
        }
        {   // particle atlas: 2x2 cells of 256 px: three billowy puffs + one steam wisp
            const int S = 512, C = 256;
            Image img = GenImageColor(S, S, Color{255, 255, 255, 0});
            Color* px = (Color*)img.data;
            for (int v = 0; v < 4; ++v) paintPuff(px, S, C, (v % 2) * C, (v / 2) * C, v);
            puffAtlas = LoadTextureFromImage(img);
            GenTextureMipmaps(&puffAtlas);
            SetTextureFilter(puffAtlas, TEXTURE_FILTER_TRILINEAR);
            SetTextureWrap(puffAtlas, TEXTURE_WRAP_CLAMP);
            UnloadImage(img);
        }
        auto radial = [](int S, auto fn) {
            Image img = GenImageColor(S, S, Color{255, 255, 255, 0});
            Color* px = (Color*)img.data;
            for (int y = 0; y < S; ++y)
                for (int x = 0; x < S; ++x) {
                    float dx = (x + 0.5f) / S - 0.5f, dy = (y + 0.5f) / S - 0.5f;
                    float r = std::sqrt(dx * dx + dy * dy) * 2.f;
                    px[y * S + x] = Color{255, 255, 255, u8(std::clamp(fn(r), 0.f, 1.f) * 255.f)};
                }
            Texture2D t = LoadTextureFromImage(img);
            GenTextureMipmaps(&t);
            SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
            SetTextureWrap(t, TEXTURE_WRAP_CLAMP);
            UnloadImage(img);
            return t;
        };
        // halo: long soft tail that reaches zero exactly at the rim
        glow = radial(128, [](float r) {
            float e = std::clamp(1.f - r, 0.f, 1.f);
            return (0.75f * std::exp(-r * 4.2f) + 0.25f * std::exp(-r * r * 9.f)) * e * e;
        });
        {   // internal glow atlas: soft halo (left half) + tight hot core (right half)
            const int S = 128;
            Image img = GenImageColor(S * 2, S, Color{255, 255, 255, 0});
            Color* px = (Color*)img.data;
            for (int y = 0; y < S; ++y)
                for (int x = 0; x < S; ++x) {
                    float dx = (x + 0.5f) / S - 0.5f, dy = (y + 0.5f) / S - 0.5f;
                    float r = std::sqrt(dx * dx + dy * dy) * 2.f;
                    float e = std::clamp(1.f - r, 0.f, 1.f);
                    float halo = (0.62f * std::exp(-r * 5.0f) + 0.38f * std::exp(-r * r * 7.f)) * e * e;
                    float core = std::exp(-r * r * 22.f) * e + 0.35f * std::exp(-r * 9.f) * e;
                    px[y * S * 2 + x] = Color{255, 255, 255, u8(std::clamp(halo, 0.f, 1.f) * 255.f)};
                    px[y * S * 2 + S + x] = Color{255, 255, 255, u8(std::clamp(core, 0.f, 1.f) * 255.f)};
                }
            glowAtlas = LoadTextureFromImage(img);
            GenTextureMipmaps(&glowAtlas);
            SetTextureFilter(glowAtlas, TEXTURE_FILTER_TRILINEAR);
            SetTextureWrap(glowAtlas, TEXTURE_WRAP_CLAMP);
            UnloadImage(img);
        }
    }

    // One smoke sprite into a cell of an RGBA image (alpha = density, rgb = baked self-shadowing).
    static void paintPuff(Color* px, int stride, int C, int ox, int oy, int variant) {
        uint32_t s = 1234567u + (uint32_t)variant * 7919u;
        if (variant == 3) {  // steam wisp: a thin, curling vertical streak that frays at the top
            float ph = rnd(s) * 6.28f;
            for (int y = 0; y < C; ++y)
                for (int x = 0; x < C; ++x) {
                    float u = (x + 0.5f) / C, v = (y + 0.5f) / C;  // v = 0 top
                    float h = 1.f - v;                              // 0 bottom .. 1 top
                    float cx = 0.5f + 0.10f * std::sin(h * 7.5f + ph) * h + 0.04f * std::sin(h * 17.f + 2.f * ph);
                    float w = 0.05f + 0.13f * h;
                    float dx = (u - cx) / w;
                    float n = fbm(u * 0.5f + 0.25f, v, 6, 4, 991u + variant);
                    float a = std::exp(-dx * dx * 1.6f) * smoothstepf(0.0f, 0.2f, h) * smoothstepf(1.0f, 0.62f, h);
                    a *= 0.55f + 0.9f * n;
                    a = std::clamp(a, 0.f, 1.f);
                    px[(oy + y) * stride + ox + x] = Color{255, 255, 255, u8(a * 255.f)};
                }
            return;
        }
        // billowy puff: a few overlapping soft lobes, eroded by domain-warped fbm
        struct Lobe {
            float x, y, r;
        };
        Lobe lobes[7];
        int nl = 5 + variant;
        for (int i = 0; i < nl; ++i) {
            float a = rnd(s) * 6.2832f, d = (i == 0) ? 0.f : 0.08f + rnd(s) * 0.16f;
            lobes[i] = {0.5f + std::cos(a) * d, 0.5f + std::sin(a) * d, 0.16f + rnd(s) * 0.12f};
        }
        uint32_t ns = 400u + (uint32_t)variant * 37u;
        for (int y = 0; y < C; ++y)
            for (int x = 0; x < C; ++x) {
                float u = (x + 0.5f) / C, v = (y + 0.5f) / C;
                float wu = u + (fbm(u, v, 3, 3, ns) - 0.5f) * 0.16f;
                float wv = v + (fbm(u, v, 3, 3, ns + 11u) - 0.5f) * 0.16f;
                float dens = 0.f;
                for (int i = 0; i < nl; ++i) {
                    float dx = wu - lobes[i].x, dy = wv - lobes[i].y;
                    dens += std::exp(-(dx * dx + dy * dy) / (lobes[i].r * lobes[i].r) * 2.2f);
                }
                float det = fbm(u, v, 5, 5, ns + 23u);
                dens *= 0.45f + 1.1f * det;
                float dx = u - 0.5f, dy = v - 0.5f;
                float edge = smoothstepf(0.5f, 0.3f, std::sqrt(dx * dx + dy * dy));
                float a = (1.f - std::exp(-dens * 1.35f)) * edge;
                float shade = 1.f - 0.28f * smoothstepf(0.35f, 1.0f, a);   // thick cores are self-shadowed
                px[(oy + y) * stride + ox + x] = Color{u8(255.f * shade), u8(255.f * shade), u8(255.f * (shade * 0.97f + 0.03f)),
                                                      u8(std::clamp(a, 0.f, 1.f) * 255.f)};
            }
    }

    // ------------------------------------------------------------------ per frame
    void computeFrame() {
        keyDir = Vector3Subtract(key.target, key.position);
        if (Vector3Length(keyDir) < 1e-4f) keyDir = {0, -1, 0};
        keyDir = Vector3Normalize(keyDir);
        keyLin = lin(key.color, std::max(key.intensity, 0.f));
        float inner = std::clamp(key.innerDeg, 0.f, 89.f), outer = std::clamp(key.outerDeg, inner + 0.5f, 89.5f);
        keyCosIn = std::cos(inner * DEG2RAD);
        keyCosOut = std::cos(outer * DEG2RAD);
        ambSkyLin = lin(ambient.sky, ambient.intensity);
        ambGroundLin = lin(ambient.ground, ambient.intensity);
        fogLin = {invTonemap1(fog.color.r / 255.f) * FOG_AMBIENT, invTonemap1(fog.color.g / 255.f) * FOG_AMBIENT,
                  invTonemap1(fog.color.b / 255.f) * FOG_AMBIENT};
        nLights = std::min((int)points.size(), MAX_POINT_LIGHTS);
        for (int i = 0; i < nLights; ++i) {
            lPos[i] = points[i].position;
            lCol[i] = lin(points[i].color, std::max(points[i].intensity, 0.f));
            lRange[i] = std::max(points[i].range, 0.01f);
        }
    }

    void uploadFrameUniforms(const Camera3D& cam, bool shadowOn) {
        SetShaderValue(lit, locViewPos, &cam.position, SHADER_UNIFORM_VEC3);
        SetShaderValue(lit, locAmbSky, &ambSkyLin, SHADER_UNIFORM_VEC3);
        SetShaderValue(lit, locAmbGround, &ambGroundLin, SHADER_UNIFORM_VEC3);
        Vector4 pr[MAX_POINT_LIGHTS]{};
        for (int i = 0; i < nLights; ++i) pr[i] = {lPos[i].x, lPos[i].y, lPos[i].z, lRange[i]};
        SetShaderValue(lit, locNumLights, &nLights, SHADER_UNIFORM_INT);
        if (nLights > 0) {
            SetShaderValueV(lit, locLightPosRange, pr, SHADER_UNIFORM_VEC4, nLights);
            SetShaderValueV(lit, locLightColor, lCol, SHADER_UNIFORM_VEC3, nLights);
        }
        SetShaderValue(lit, locKeyPos, &key.position, SHADER_UNIFORM_VEC3);
        SetShaderValue(lit, locKeyDir, &keyDir, SHADER_UNIFORM_VEC3);
        SetShaderValue(lit, locKeyColor, &keyLin, SHADER_UNIFORM_VEC3);
        Vector4 kp{keyCosIn, keyCosOut, std::max(key.range, 0.1f), KEY_LIGHT_SIZE};
        SetShaderValue(lit, locKeyParams, &kp, SHADER_UNIFORM_VEC4);
        Vector4 b{key.target.x, key.target.y, key.target.z, BOUNCE_STRENGTH};
        SetShaderValue(lit, locBounce, &b, SHADER_UNIFORM_VEC4);
        int so = shadowOn ? 1 : 0;
        SetShaderValue(lit, locShadowOn, &so, SHADER_UNIFORM_INT);
        SetShaderValueMatrix(lit, locLightVP, lightVP);
        Vector4 sp{SHADOW_NEAR, shadowFar, 1.f / SHADOW_SIZE, shadowTanHalf};
        SetShaderValue(lit, locShadowParams, &sp, SHADER_UNIFORM_VEC4);
        SetShaderValue(lit, locKeyRight, &shadowRight, SHADER_UNIFORM_VEC3);
        SetShaderValue(lit, locKeyUp, &shadowUp, SHADER_UNIFORM_VEC3);
        SetShaderValue(lit, locFogColor, &fogLin, SHADER_UNIFORM_VEC3);
        Vector4 fp{std::max(fog.density, 0.f), fog.hazeStart, std::max(fog.hazeTop, fog.hazeStart + 0.01f),
                   std::max(fog.hazeDensity, 0.f) * HAZE_SCALE};
        SetShaderValue(lit, locFogParams, &fp, SHADER_UNIFORM_VEC4);
        Vector4 sc{KEY_SCATTER, POINT_SCATTER, SCATTER_K, MIN_TRANSMITTANCE};
        SetShaderValue(lit, locScatterParams, &sc, SHADER_UNIFORM_VEC4);
        Vector4 hw{HAZE_WAVE, time, 0.f, 0.f};
        SetShaderValue(lit, locHazeWave, &hw, SHADER_UNIFORM_VEC4);
    }

    void resetLitCache() {
        curMat = nullptr;
        curTex = 0xffffffffu;
        curAlpha = curKind = curFog = -1.f;
        curMask = -1;
    }
    void setMat(const Mat& m, float alpha, float kind, float fogOn) {
        if (curMat != &m) {
            glUniform4f(locMatParams, m.specular, m.shininess, m.emissive, m.rim);
            Color c = m.material.maps[MATERIAL_MAP_ALBEDO].color;
            glUniform4f(locColDiffuse, c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f);
            unsigned int tex = m.material.maps[MATERIAL_MAP_ALBEDO].texture.id;
            if (tex != curTex) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, tex);
                curTex = tex;
            }
            curMat = &m;
        }
        if (alpha != curAlpha || kind != curKind || fogOn != curFog) {
            glUniform4f(locMatMode, alpha, kind, fogOn, 0.f);
            curAlpha = alpha;
            curKind = kind;
            curFog = fogOn;
        }
    }
    bool prepassSafe(const DrawItem& it) const {
        const MaterialMap& mm = it.mat->material.maps[MATERIAL_MAP_ALBEDO];
        return mm.color.a == 255 && it.mesh->vaoId != 0 && opaqueTextures().count(mm.texture.id) != 0;
    }

    // Lean draw of one mesh with the currently bound program (raylib's DrawMesh does far more per call).
    static void drawVao(const Mesh& m) {
        glBindVertexArray(m.vaoId);
        if (m.indices) glDrawElements(GL_TRIANGLES, m.triangleCount * 3, GL_UNSIGNED_SHORT, nullptr);
        else glDrawArrays(GL_TRIANGLES, 0, m.vertexCount);
    }
    void drawDepth(const DrawItem& it, const Matrix& vp) {
        if (it.mesh->vaoId == 0) {
            DrawMesh(*it.mesh, depthMat, it.xf);
            glUseProgram(depth.id);
            return;
        }
        glUniformMatrix4fv(locDepthMvp, 1, GL_FALSE, MatrixToFloatV(MatrixMultiply(it.xf, vp)).v);
        drawVao(*it.mesh);
    }
    void drawGeom(const DrawItem& it) {
        if (it.lights != curMask) {
            glUniform1i(locLightMask, it.lights);
            curMask = it.lights;
        }
        if (it.mesh->vaoId == 0) {
            DrawMesh(*it.mesh, it.mat->material, it.xf);
            glUseProgram(lit.id);
            float a = curAlpha, k = curKind, f = curFog;
            resetLitCache();
            setMat(*it.mat, a, k, f);
            return;
        }
        glUniformMatrix4fv(locMvp, 1, GL_FALSE, MatrixToFloatV(MatrixMultiply(it.xf, viewProj)).v);
        glUniformMatrix4fv(locModel, 1, GL_FALSE, MatrixToFloatV(it.xf).v);
        glUniformMatrix4fv(locNormalMat, 1, GL_FALSE, MatrixToFloatV(MatrixTranspose(MatrixInvert(it.xf))).v);
        drawVao(*it.mesh);
    }

    // Local bounding sphere of a mesh (cached by VAO); used to sort transparent meshes by their real centre.
    Sphere meshBounds(const Mesh& m) {
        auto it = bounds.find(m.vaoId);
        if (it != bounds.end() && it->second.vertices == m.vertices && it->second.vertexCount == m.vertexCount)
            return it->second.s;
        Sphere s{{0, 0, 0}, 0.f};
        if (m.vertices && m.vertexCount > 0) {
            Vector3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
            for (int i = 0; i < m.vertexCount; ++i) {
                Vector3 p{m.vertices[i * 3], m.vertices[i * 3 + 1], m.vertices[i * 3 + 2]};
                lo = Vector3Min(lo, p);
                hi = Vector3Max(hi, p);
            }
            s.c = Vector3Scale(Vector3Add(lo, hi), 0.5f);
            s.r = Vector3Distance(hi, s.c);
        }
        if (m.vaoId) bounds[m.vaoId] = {s, m.vertices, m.vertexCount};
        return s;
    }

    // World bounding sphere of every submission (for culling and sorting) and the point lights that can
    // reach it: directly (its sphere is within a light's range) or through in-scatter along the view rays
    // (the capsule from the camera to its sphere passes within range). Lights reach exactly 0 at their range,
    // so this culling is exact.
    void prepareItems(Vector3 camPos) {
        const int all = (1 << nLights) - 1;
        for (DrawItem& it : items) {
            it.radius = -1.f;
            it.lights = all;
            if (it.mesh->vaoId == 0) continue;
            Sphere s = meshBounds(*it.mesh);
            if (s.r <= 0.f) continue;
            const Matrix& m = it.xf;
            float sx = m.m0 * m.m0 + m.m1 * m.m1 + m.m2 * m.m2, sy = m.m4 * m.m4 + m.m5 * m.m5 + m.m6 * m.m6,
                  sz = m.m8 * m.m8 + m.m9 * m.m9 + m.m10 * m.m10;
            it.center = Vector3Transform(s.c, m);
            it.radius = s.r * std::sqrt(std::max({sx, sy, sz})) * 1.001f + 1e-4f;
            Vector3 seg = Vector3Subtract(it.center, camPos);
            float segLen2 = Vector3DotProduct(seg, seg);
            int mask = 0;
            for (int i = 0; i < nLights; ++i) {
                Vector3 d = Vector3Subtract(lPos[i], camPos);
                float t = segLen2 > 1e-12f ? std::clamp(Vector3DotProduct(d, seg) / segLen2, 0.f, 1.f) : 0.f;
                float dist = Vector3Distance(lPos[i], Vector3Add(camPos, Vector3Scale(seg, t)));
                if (dist - it.radius < lRange[i]) mask |= 1 << i;
            }
            it.lights = mask;
        }
    }

    float fogTransmittance(Vector3 from, Vector3 to) const {
        float len = Vector3Distance(from, to);
        float od = 0.f;
        float hz0 = fog.hazeStart, hz1 = std::max(fog.hazeTop, fog.hazeStart + 0.01f);
        for (int i = 0; i < 4; ++i) {
            float f = (i + 0.5f) / 4.f;
            Vector3 x = Vector3Lerp(from, to, f);
            float wave = HAZE_WAVE * std::sin(0.9f * x.x + 0.31f * time) *
                         std::cos(0.73f * x.z - 0.23f * time + 1.3f * std::sin(0.4f * x.x));
            od += std::max(fog.density, 0.f) + std::max(fog.hazeDensity, 0.f) * HAZE_SCALE * smoothstepf(hz0 + wave, hz1, x.y);
        }
        od *= len / 4.f;
        return std::max(std::exp(-od), MIN_TRANSMITTANCE);
    }

    // Light a smoke particle: linear radiance scattered toward the camera. `keyDirOut` = direction to the key
    // light, `keyShare` = fraction of the light that comes from it (for the top-lit gradient on the sprite).
    Vector3 smokeLight(Vector3 p, Vector3 toCam, Vector3& keyDirOut, float& keyShare) const {
        // unlit smoke has the radiance of the surrounding haze (so it never reads as a dark blot), tinted
        // slightly blue-grey against the warm lamp pools
        Vector3 l{fogLin.x * 1.0f + ambSkyLin.x * 0.2f, fogLin.y * 1.04f + ambSkyLin.y * 0.2f,
                  fogLin.z * 1.16f + ambSkyLin.z * 0.24f};
        Vector3 L = Vector3Subtract(key.position, p);
        float d = Vector3Length(L);
        float keyAmt = 0.f;
        keyDirOut = {0, 1, 0};
        if (d > 1e-4f) {
            L = Vector3Scale(L, 1.f / d);
            keyDirOut = L;
            float cone = smoothstepf(keyCosOut, keyCosIn, -Vector3DotProduct(L, keyDir));
            float att = lightAttCPU(d, key.range) * cone;
            if (att > 0.f) {
                float ph = phaseSchlick(-Vector3DotProduct(L, toCam));
                keyAmt = att * ph * 0.8f;
                l = Vector3Add(l, Vector3Scale(keyLin, keyAmt));
            }
        }
        float pointAmt = 0.f;
        for (int i = 0; i < nLights; ++i) {
            Vector3 v = Vector3Subtract(lPos[i], p);
            float dd = Vector3Length(v);
            float att = lightAttCPU(dd, lRange[i]);
            if (att <= 0.f) continue;
            float ph = dd > 1e-4f ? phaseSchlick(-Vector3DotProduct(Vector3Scale(v, 1.f / dd), toCam)) : 1.f;
            pointAmt += att * ph * 0.7f;
            l = Vector3Add(l, Vector3Scale(lCol[i], att * ph * 0.7f));
        }
        float kl = keyAmt * (keyLin.x + keyLin.y + keyLin.z);
        float tl = kl + 1e-4f + (l.x + l.y + l.z) - kl;
        keyShare = std::clamp(kl / tl, 0.f, 1.f);
        (void)pointAmt;
        return l;
    }

    Color shadeSmoke(Vector3 radiance, Color albedo, Vector3 pos, Vector3 camPos, float mul) const {
        // smoke barely absorbs: SmokeParams::color is a tint relative to light grey (200), not an albedo
        Vector3 a = lin(albedo, 1.f / 0.58f);
        a = {std::min(a.x, 1.35f), std::min(a.y, 1.35f), std::min(a.z, 1.35f)};
        Vector3 c{radiance.x * a.x * mul, radiance.y * a.y * mul, radiance.z * a.z * mul};
        float T = fogTransmittance(camPos, pos);
        c = Vector3Add(Vector3Scale(c, T), Vector3Scale(fogLin, 1.f - T));
        Vector3 d = tonemapCPU(c);
        return Color{u8(d.x * 255.f), u8(d.y * 255.f), u8(d.z * 255.f), 255};
    }

    // ------------------------------------------------------------------ passes
    void shadowPass() {
        Camera3D lc{};
        lc.position = key.position;
        lc.target = Vector3Add(key.position, keyDir);
        lc.up = std::fabs(keyDir.y) > 0.95f ? Vector3{0, 0, -1} : Vector3{0, 1, 0};
        float half = std::min(std::clamp(key.outerDeg, 5.f, 89.f), SHADOW_MAX_HALF_DEG);
        lc.fovy = half * 2.f;
        lc.projection = CAMERA_PERSPECTIVE;
        shadowTanHalf = std::tan(half * DEG2RAD);
        shadowFar = std::clamp(key.range, 1.f, 12.f);
        rlSetClipPlanes(SHADOW_NEAR, shadowFar);
        RenderTexture2D rt{};
        rt.id = shadowFbo;
        rt.texture.width = SHADOW_SIZE;
        rt.texture.height = SHADOW_SIZE;
        BeginTextureMode(rt);
        rlClearColor(255, 255, 255, 255);
        rlClearScreenBuffers();
        BeginMode3D(lc);
        Matrix lv = rlGetMatrixModelview();
        Matrix lp = rlGetMatrixProjection();
        rlDisableBackfaceCulling();
        rlDisableColorBlend();
        rlDrawRenderBatchActive();
        lightVP = MatrixMultiply(lv, lp);
        lightFrustum.from(lightVP);
        shadowRight = {lv.m0, lv.m4, lv.m8};
        shadowUp = {lv.m1, lv.m5, lv.m9};
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.4f, 2.0f);
        glUseProgram(depth.id);
        for (const DrawItem& it : items)
            if ((it.flags & CastShadow) && !(it.flags & Transparent) && lightFrustum.visible(it.center, it.radius))
                drawDepth(it, lightVP);
        glBindVertexArray(0);
        glDisable(GL_POLYGON_OFFSET_FILL);
        rlEnableColorBlend();
        rlEnableBackfaceCulling();
        EndMode3D();
        EndTextureMode();
    }

    void drawMeshItem(const DrawItem& it, float alpha, float kind) {
        setMat(*it.mat, alpha, kind, (it.flags & NoFog) ? 0.f : 1.f);
        const bool ds = it.flags & DoubleSided;
        if (ds && kind == 1.f) {
            // glass: inner (far) faces first, then the outer ones
            rlSetCullFace(RL_CULL_FACE_FRONT);
            drawGeom(it);
            rlSetCullFace(RL_CULL_FACE_BACK);
            drawGeom(it);
            return;
        }
        if (ds) rlDisableBackfaceCulling();
        drawGeom(it);
        if (ds) rlEnableBackfaceCulling();
    }

    void pushQuad(Vector3 c, Vector3 r, Vector3 u, float u0, float v0, float u1, float v1, const Color col[4]) {
        // corners: bottom-left, bottom-right, top-right, top-left (counter-clockwise toward the camera)
        SpriteVertex q[4] = {{c.x - r.x - u.x, c.y - r.y - u.y, c.z - r.z - u.z, u0, v1, col[0].r, col[0].g, col[0].b, col[0].a},
                             {c.x + r.x - u.x, c.y + r.y - u.y, c.z + r.z - u.z, u1, v1, col[1].r, col[1].g, col[1].b, col[1].a},
                             {c.x + r.x + u.x, c.y + r.y + u.y, c.z + r.z + u.z, u1, v0, col[2].r, col[2].g, col[2].b, col[2].a},
                             {c.x - r.x + u.x, c.y - r.y + u.y, c.z - r.z + u.z, u0, v0, col[3].r, col[3].g, col[3].b, col[3].a}};
        sv.push_back(q[0]);
        sv.push_back(q[1]);
        sv.push_back(q[2]);
        sv.push_back(q[0]);
        sv.push_back(q[2]);
        sv.push_back(q[3]);
    }

    void emitParticle(const Particle& p, const Camera3D& cam, Vector3 camR, Vector3 camU) {
        float t = std::clamp(p.age / p.life, 0.f, 1.f);
        const bool steam = p.variant == 3;
        float grow = 1.f - (1.f - t) * (1.f - t);
        float size = p.s0 + (p.s1 - p.s0) * (steam ? t : grow);
        float fadeIn = smoothstepf(0.f, steam ? 0.18f : 0.1f, t);
        float fadeOut = std::pow(1.f - t, steam ? 1.2f : 1.6f);
        Vector3 toCam = Vector3Subtract(cam.position, p.pos);
        float dist = Vector3Length(toCam);
        float near = smoothstepf(0.10f + size * 0.25f, 0.35f + size * 0.9f, dist);
        float a = p.alpha * fadeIn * fadeOut * near;
        if (a < 0.004f) return;
        toCam = Vector3Scale(toCam, 1.f / std::max(dist, 1e-4f));

        Vector3 kdir;
        float keyShare = 0.f;
        Vector3 rad = smokeLight(p.pos, toCam, kdir, keyShare);
        Color base = shadeSmoke(rad, p.color, p.pos, cam.position, steam ? 1.15f : 1.f);
        base.a = u8(std::clamp(a, 0.f, 1.f) * 255.f);

        float h = size * 0.5f;
        float cr = std::cos(p.rot * DEG2RAD), sr = std::sin(p.rot * DEG2RAD);
        Vector3 r, u;
        if (steam) {  // wisps align with their motion as seen from the camera and stretch with speed
            Vector3 v = Vector3Length(p.vel) > 1e-4f ? p.vel : Vector3{0, 1, 0};
            Vector3 vp = Vector3Subtract(v, Vector3Scale(toCam, Vector3DotProduct(v, toCam)));
            float vl = Vector3Length(vp);
            Vector3 up = vl > 1e-5f ? Vector3Scale(vp, 1.f / vl) : camU;
            Vector3 right = Vector3Normalize(Vector3CrossProduct(up, toCam));
            float stretch = 1.f + std::min(vl * 6.f, 0.8f);
            r = Vector3Scale(right, h * 0.7f);
            u = Vector3Scale(up, h * stretch);
        } else {
            r = Vector3Add(Vector3Scale(camR, h * cr), Vector3Scale(camU, h * sr));
            u = Vector3Add(Vector3Scale(camR, -h * sr), Vector3Scale(camU, h * cr));
        }
        // top-lit gradient: corners facing the key light get brighter, the others darker
        float g = 0.32f * keyShare + 0.08f;
        Vector3 offs[4] = {Vector3Negate(Vector3Add(r, u)), Vector3Subtract(r, u), Vector3Add(r, u), Vector3Subtract(u, r)};
        Color cols[4];
        for (int i = 0; i < 4; ++i) {
            float k = 1.f + g * Vector3DotProduct(Vector3Normalize(offs[i]), kdir);
            cols[i] = Color{u8(base.r * k), u8(base.g * k), u8(base.b * k), base.a};
        }
        float cu = (p.variant % 2) * 0.5f, cv = (p.variant / 2) * 0.5f;
        const float in = 1.f / 512.f;
        pushQuad(p.pos, r, u, cu + in, cv + in, cu + 0.5f - in, cv + 0.5f - in, cols);
    }

    void emitBoard(const Board& b, const Camera3D& cam, Vector3 camR) {
        float tw = (float)std::max(b.tex.width, 1), th = (float)std::max(b.tex.height, 1);
        float u0 = b.src.x / tw, v0 = b.src.y / th;
        float u1 = (b.src.x + std::fabs(b.src.width)) / tw, v1 = (b.src.y + std::fabs(b.src.height)) / th;
        if (b.src.width < 0) std::swap(u0, u1);
        if (b.src.height < 0) std::swap(v0, v1);
        Color c = b.tint;
        float T = fogTransmittance(cam.position, b.pos);
        if (b.additive) {
            c.r = u8(c.r * T);
            c.g = u8(c.g * T);
            c.b = u8(c.b * T);
        } else {
            Vector3 f = tonemapCPU(fogLin);
            c.r = u8(c.r * T + f.x * 255.f * (1.f - T));
            c.g = u8(c.g * T + f.y * 255.f * (1.f - T));
            c.b = u8(c.b * T + f.z * 255.f * (1.f - T));
        }
        Color cols[4] = {c, c, c, c};
        // upright (cylindrical) billboard, like raylib's DrawBillboardPro with up = +Y
        pushQuad(b.pos, Vector3Scale(camR, b.size.x * 0.5f), Vector3{0, b.size.y * 0.5f, 0}, u0, v0, u1, v1, cols);
    }

    void emitGlows(const Camera3D& cam, Vector3 camR, Vector3 camU) {
        for (const Glow& g : glows) {
            float T = fogTransmittance(cam.position, g.pos);
            float k = std::clamp(g.intensity, 0.f, 4.f);
            // pull the sprite a little toward the camera so the bulb's own geometry doesn't clip the halo
            Vector3 toCam = Vector3Normalize(Vector3Subtract(cam.position, g.pos));
            Vector3 hp = Vector3Add(g.pos, Vector3Scale(toCam, g.radius * 0.35f));
            Vector3 cp = Vector3Add(g.pos, Vector3Scale(toCam, g.radius * 0.12f));
            Color halo = g.color;
            halo.a = u8(std::min(k * 0.55f, 1.f) * (0.55f + 0.45f * T) * 255.f);
            Color core = mixc(g.color, WHITE, 0.55f);
            core.a = u8(std::min(k * 0.9f, 1.f) * T * 255.f);
            Color hc[4] = {halo, halo, halo, halo}, cc[4] = {core, core, core, core};
            pushQuad(hp, Vector3Scale(camR, g.radius), Vector3Scale(camU, g.radius), 0.f, 0.f, 0.5f, 1.f, hc);
            float cr = g.radius * 0.32f;
            pushQuad(cp, Vector3Scale(camR, cr), Vector3Scale(camU, cr), 0.5f, 0.f, 1.f, 1.f, cc);
        }
    }

    void uploadSprites() {
        glBindVertexArray(spriteVao);
        glBindBuffer(GL_ARRAY_BUFFER, spriteVbo);
        size_t bytes = std::max<size_t>(sv.size(), 6) * sizeof(SpriteVertex);
        if (bytes > spriteCap) spriteCap = std::max(bytes, spriteCap * 2);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)spriteCap, nullptr, GL_STREAM_DRAW);  // orphan: no GPU sync
        if (!sv.empty()) glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(sv.size() * sizeof(SpriteVertex)), sv.data());
    }
    void setupSpriteBuffers() {
        glGenVertexArrays(1, &spriteVao);
        glGenBuffers(1, &spriteVbo);
        glBindVertexArray(spriteVao);
        glBindBuffer(GL_ARRAY_BUFFER, spriteVbo);
        spriteCap = 4096 * sizeof(SpriteVertex);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)spriteCap, nullptr, GL_STREAM_DRAW);
        const GLsizei st = sizeof(SpriteVertex);
        glEnableVertexAttribArray(RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION);
        glVertexAttribPointer(RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION, 3, GL_FLOAT, GL_FALSE, st, (void*)0);
        glEnableVertexAttribArray(RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD);
        glVertexAttribPointer(RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD, 2, GL_FLOAT, GL_FALSE, st, (void*)12);
        glEnableVertexAttribArray(RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR);
        glVertexAttribPointer(RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void*)20);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // blend helpers (direct GL; render() restores raylib's default alpha blending at the end)
    static void blendAlpha() {
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    static void blendAdd() {
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    }

    void postPass(RenderTexture2D* target, int postFirst) {
        int w = target ? target->texture.width : renderW, h = target ? target->texture.height : renderH;
        Vector4 pp{(float)w / (float)std::max(h, 1), (float)(frameCounter % 4096u) * 0.61803f, VIGNETTE, GRAIN};
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glBindVertexArray(spriteVao);
        glUseProgram(post.id);
        glUniform4f(locPostParams, pp.x, pp.y, pp.z, pp.w);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_ONE, GL_SRC_ALPHA);  // dst = src.rgb + dst * src.a
        glDrawArrays(GL_TRIANGLES, postFirst, 6);
        if (target) {
            // keep the target opaque: blended smoke/glass leave alpha < 1 behind
            glUseProgram(sprite.id);
            Matrix id = MatrixIdentity();
            glUniformMatrix4fv(locSpriteMvp, 1, GL_FALSE, MatrixToFloatV(id).v);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, white.id);
            glDisable(GL_BLEND);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
            glDrawArrays(GL_TRIANGLES, postFirst, 6);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glEnable(GL_BLEND);
        }
        blendAlpha();
        glEnable(GL_CULL_FACE);
        glBindVertexArray(0);
        glUseProgram(0);
    }
};

// ============================================================================ Renderer
Renderer::Renderer() : impl_(new Impl) {}
Renderer::~Renderer() {
    shutdown();
    delete impl_;
}

bool Renderer::init() {
    Impl& I = *impl_;
    if (I.ready) return true;
    I.lit = LoadShaderFromMemory(kLitVS, kLitFS);
    if (I.lit.id == 0 || I.lit.id == rlGetShaderIdDefault()) return false;
    I.lit.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(I.lit, "matModel");
    I.lit.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(I.lit, "matNormal");
    auto L = [&](const char* n) { return GetShaderLocation(I.lit, n); };
    I.locMatParams = L("matParams");
    I.locMatMode = L("matMode");
    I.locViewPos = L("viewPos");
    I.locAmbSky = L("ambSky");
    I.locAmbGround = L("ambGround");
    I.locNumLights = L("numLights");
    I.locLightPosRange = L("lightPosRange");
    I.locLightColor = L("lightColor");
    I.locKeyPos = L("keyPos");
    I.locKeyDir = L("keyDir");
    I.locKeyColor = L("keyColor");
    I.locKeyParams = L("keyParams");
    I.locBounce = L("bounce");
    I.locShadowOn = L("shadowOn");
    I.locShadowMap = L("shadowMap");
    I.locShadowDepth = L("shadowDepth");
    I.locLightVP = L("lightVP");
    I.locShadowParams = L("shadowParams");
    I.locKeyRight = L("keyRight");
    I.locKeyUp = L("keyUp");
    I.locLightMask = L("lightMask");
    I.locFogColor = L("fogColor");
    I.locFogParams = L("fogParams");
    I.locScatterParams = L("scatterParams");
    I.locHazeWave = L("hazeWave");
    int slotCmp = 10, slotRaw = 11;
    SetShaderValue(I.lit, I.locShadowMap, &slotCmp, SHADER_UNIFORM_INT);
    SetShaderValue(I.lit, I.locShadowDepth, &slotRaw, SHADER_UNIFORM_INT);
    Vector4 defMode{1.f, 0.f, 1.f, 0.f};
    SetShaderValue(I.lit, I.locMatMode, &defMode, SHADER_UNIFORM_VEC4);

    I.locMvp = I.lit.locs[SHADER_LOC_MATRIX_MVP];
    I.locModel = I.lit.locs[SHADER_LOC_MATRIX_MODEL];
    I.locNormalMat = I.lit.locs[SHADER_LOC_MATRIX_NORMAL];
    I.locColDiffuse = I.lit.locs[SHADER_LOC_COLOR_DIFFUSE];
    int unit0 = 0;
    SetShaderValue(I.lit, I.lit.locs[SHADER_LOC_MAP_DIFFUSE], &unit0, SHADER_UNIFORM_INT);
    I.depth = LoadShaderFromMemory(kDepthVS, kDepthFS);
    I.locDepthMvp = I.depth.locs[SHADER_LOC_MATRIX_MVP];
    I.depthMat = LoadMaterialDefault();
    I.depthMat.shader = I.depth;
    I.post = LoadShaderFromMemory(kPostVS, kPostFS);
    I.sprite = LoadShaderFromMemory(kSpriteVS, kSpriteFS);
    I.locSpriteMvp = I.sprite.locs[SHADER_LOC_MATRIX_MVP];
    I.setupSpriteBuffers();
    I.locPostParams = GetShaderLocation(I.post, "postParams");

    I.genSpriteTextures();
    opaqueTextures().insert(I.white.id);
    I.setupShadowMap();
    I.items.reserve(1024);
    I.sorted.reserve(2048);
    I.sv.reserve(12000);
    I.particles.reserve(MAX_PARTICLES);
    I.ready = true;
    return true;
}

void Renderer::shutdown() {
    Impl& I = *impl_;
    if (!I.ready) return;
    UnloadShader(I.lit);
    I.depthMat.shader = Shader{};
    UnloadShader(I.depth);
    UnloadShader(I.post);
    UnloadShader(I.sprite);
    if (I.spriteVbo) glDeleteBuffers(1, &I.spriteVbo);
    if (I.spriteVao) glDeleteVertexArrays(1, &I.spriteVao);
    I.spriteVbo = I.spriteVao = 0;
    I.spriteCap = 0;
    RL_FREE(I.depthMat.maps);
    I.depthMat.maps = nullptr;
    UnloadTexture(I.white);
    UnloadTexture(I.puff);
    UnloadTexture(I.puffAtlas);
    UnloadTexture(I.glow);
    UnloadTexture(I.glowAtlas);
    if (I.shadowFbo) glDeleteFramebuffers(1, &I.shadowFbo);
    if (I.shadowTex) glDeleteTextures(1, &I.shadowTex);
    GLuint s[2] = {I.samplerCmp, I.samplerRaw};
    if (s[0] || s[1]) glDeleteSamplers(2, s);
    I.shadowFbo = I.shadowTex = I.samplerCmp = I.samplerRaw = 0;
    I.shadowOk = false;
    I.items.clear();
    I.glows.clear();
    I.boards.clear();
    I.points.clear();
    I.particles.clear();
    I.bounds.clear();
    I.ready = false;
}

void Renderer::update(float dt) {
    Impl& I = *impl_;
    dt = std::clamp(dt, 0.f, 0.1f);
    I.time += dt;
    // a very slow room draft that meanders; older smoke follows it more
    Vector3 draft{0.012f * std::sin(I.time * 0.071f) + 0.006f * std::sin(I.time * 0.23f + 1.f), 0.f,
                  0.010f * std::cos(I.time * 0.053f + 0.4f)};
    for (Particle& p : I.particles) {
        p.age += dt;
        float t = I.time * 0.9f + p.seed;
        float life01 = p.age / std::max(p.life, 0.01f);
        Vector3 wob{std::sin(t * 1.7f + p.pos.y * 5.f) + 0.6f * std::sin(t * 0.61f + p.pos.z * 3.f + p.seed),
                    0.35f * std::sin(t * 1.1f + p.pos.x * 4.f),
                    std::cos(t * 1.3f + p.pos.y * 4.f) + 0.6f * std::cos(t * 0.73f + p.pos.x * 3.f + p.seed)};
        float grow = std::min(1.f, life01 * 3.f);
        p.vel = Vector3Add(p.vel, Vector3Scale(wob, p.turb * 0.035f * grow * dt));
        p.vel = Vector3Add(p.vel, Vector3Scale(Vector3Subtract(draft, Vector3{p.vel.x, 0, p.vel.z}), 0.08f * life01 * dt));
        p.vel.y += p.buoy * dt;
        p.vel = Vector3Scale(p.vel, 1.f - std::min(0.35f * dt, 0.5f));
        p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt));
        p.rot += p.rotSpeed * dt;
        p.rotSpeed *= 1.f - std::min(0.15f * dt, 0.5f);
    }
    I.particles.erase(std::remove_if(I.particles.begin(), I.particles.end(),
                                     [](const Particle& p) { return p.age >= p.life; }),
                      I.particles.end());
}

float Renderer::time() const { return impl_->time; }
void Renderer::setRenderSize(int w, int h) {
    impl_->renderW = std::max(w, 1);
    impl_->renderH = std::max(h, 1);
}
int Renderer::renderWidth() const { return impl_->renderW; }
int Renderer::renderHeight() const { return impl_->renderH; }
void Renderer::setAmbient(const Ambient& a) { impl_->ambient = a; }
void Renderer::setFog(const Fog& f) { impl_->fog = f; }
void Renderer::setKeyLight(const KeyLight& k) { impl_->key = k; }
void Renderer::addPointLight(const PointLight& p) {
    if ((int)impl_->points.size() < MAX_POINT_LIGHTS) impl_->points.push_back(p);
}

Mat Renderer::makeMat(Color albedo, Texture2D texture, float specular, float shininess, float emissive, float rim) {
    Mat m;
    m.material = LoadMaterialDefault();
    m.material.shader = impl_->lit;
    m.material.maps[MATERIAL_MAP_ALBEDO].texture = texture.id ? texture : impl_->white;
    m.material.maps[MATERIAL_MAP_ALBEDO].color = albedo;
    m.specular = specular;
    m.shininess = shininess;
    m.emissive = emissive;
    m.rim = rim;
    m.alpha = 1.f; // albedo alpha already carries transparency; Mat::alpha multiplies it
    return m;
}

void Renderer::unloadMat(Mat& m) {
    if (m.material.maps) RL_FREE(m.material.maps);
    m.material.maps = nullptr;
}

void Renderer::submit(const Mesh* mesh, const Mat* mat, const Matrix& transform, uint32_t flags) {
    if (!mesh || !mat || mesh->vertexCount == 0 || !mat->material.maps) return;
    impl_->items.push_back({mesh, mat, transform, flags, Vector3{0, 0, 0}, -1.f, 0});
}
void Renderer::submitGlow(Vector3 pos, float radius, Color color, float intensity) {
    if (radius <= 0.f || intensity <= 0.f) return;
    impl_->glows.push_back({pos, radius, color, intensity});
}
void Renderer::submitBillboard(Texture2D tex, Rectangle src, Vector3 pos, Vector2 size, Color tint, bool additive) {
    if (tex.id == 0) return;
    impl_->boards.push_back({tex, src, pos, size, tint, additive});
}

void Renderer::emitSmoke(Vector3 pos, const SmokeParams& sp) {
    Impl& I = *impl_;
    if ((int)I.particles.size() >= MAX_PARTICLES) I.particles.erase(I.particles.begin());
    // via int32_t: converting a negative float straight to an unsigned type is undefined behaviour
    uint32_t s = hash32(++I.emitCounter * 2654435761u ^ (uint32_t)(int32_t)(pos.x * 1000.f) ^
                        ((uint32_t)(int32_t)(pos.z * 1000.f) << 11));
    float h0 = rnd(s), h1 = rnd(s), h2 = rnd(s);
    Particle p;
    p.pos = pos;
    p.vel = sp.velocity;
    p.age = 0.f;
    p.life = std::max(sp.life, 0.05f);
    p.s0 = std::max(sp.size0, 0.f);
    p.s1 = std::max(sp.size1, 0.f);
    p.alpha = std::clamp(sp.alpha, 0.f, 1.f);
    p.rot = h0 * 360.f;
    p.rotSpeed = (h1 - 0.5f) * 40.f;
    p.turb = sp.turbulence;
    p.buoy = sp.buoyancy;
    p.seed = h2 * 100.f;
    p.color = sp.color;
    // thin particles (tea steam, a cigarette's thread) use the wisp sprite; larger ones billowy puffs
    const bool wisp = p.s1 <= 0.12f;
    p.variant = wisp ? 3 : (int)(h2 * 2.999f);
    I.particles.push_back(p);
}
int Renderer::particleCount() const { return (int)impl_->particles.size(); }

void Renderer::render(const Camera3D& cam, RenderTexture2D* target, Color clearColor) {
    Impl& I = *impl_;
    if (!I.ready) return;
    I.lastCam = cam;
    ++I.frameCounter;
    rlDrawRenderBatchActive();
    I.computeFrame();
    I.prepareItems(cam.position);

    // ---- shadow pass
    const bool doShadow = I.shadowOk && I.key.shadows && (I.keyLin.x + I.keyLin.y + I.keyLin.z) > 0.f;
    if (doShadow) I.shadowPass();

    // ---- main pass
    if (target) {
        BeginTextureMode(*target);
        ClearBackground(clearColor);
    }
    rlSetClipPlanes(0.02, 60.0);
    BeginMode3D(cam);
    Matrix view = rlGetMatrixModelview();
    I.viewProj = MatrixMultiply(view, rlGetMatrixProjection());
    I.camFrustum.from(I.viewProj);
    rlDrawRenderBatchActive();
    I.uploadFrameUniforms(cam, doShadow);  // binds the lit program
    I.resetLitCache();
    // the shadow samplers stay bound even when the pass is skipped: an active sampler2DShadow without a
    // depth texture behind it is invalid on some drivers (Apple's GL logs it) even if the branch is not taken
    const bool bindShadow = I.shadowTex != 0;
    if (bindShadow) {
        glActiveTexture(GL_TEXTURE0 + 10);
        glBindTexture(GL_TEXTURE_2D, I.shadowTex);
        glBindSampler(10, I.samplerCmp);
        glActiveTexture(GL_TEXTURE0 + 11);
        glBindTexture(GL_TEXTURE_2D, I.shadowTex);
        glBindSampler(11, I.samplerRaw);
        glActiveTexture(GL_TEXTURE0);
    }
    glVertexAttrib4f(RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR, 1.f, 1.f, 1.f, 1.f);  // meshes without colours
    glDepthFunc(GL_LEQUAL);

    // ---- opaque: depth pre-pass, then the lit shader runs once per pixel
    I.opaqueA.clear();
    I.opaqueB.clear();
    for (int i = 0; i < (int)I.items.size(); ++i) {
        const DrawItem& it = I.items[i];
        if ((it.flags & Transparent) || !I.camFrustum.visible(it.center, it.radius)) continue;
        (I.prepassSafe(it) ? I.opaqueA : I.opaqueB).push_back(i);
    }
    std::sort(I.opaqueA.begin(), I.opaqueA.end(), [&](int a, int b) {
        const Mat* ma = I.items[a].mat;
        const Mat* mb = I.items[b].mat;
        unsigned ta = ma->material.maps[0].texture.id, tb = mb->material.maps[0].texture.id;
        return ta != tb ? ta < tb : (ma != mb ? ma < mb : a < b);
    });
    glUseProgram(I.depth.id);
    rlColorMask(false, false, false, false);
    for (int i : I.opaqueA) {
        const DrawItem& it = I.items[i];
        if (it.flags & DoubleSided) rlDisableBackfaceCulling();
        I.drawDepth(it, I.viewProj);
        if (it.flags & DoubleSided) rlEnableBackfaceCulling();
    }
    rlColorMask(true, true, true, true);
    glUseProgram(I.lit.id);
    rlDisableDepthMask();
    for (int i : I.opaqueA) I.drawMeshItem(I.items[i], 1.f, 0.f);
    rlEnableDepthMask();
    for (int i : I.opaqueB) I.drawMeshItem(I.items[i], 1.f, 0.f);
    glBindVertexArray(0);

    // ---- transparent meshes, smoke and billboards: one list, far to near
    Vector3 camR{view.m0, view.m4, view.m8}, camU{view.m1, view.m5, view.m9};
    I.sorted.clear();
    for (int i = 0; i < (int)I.items.size(); ++i) {
        const DrawItem& it = I.items[i];
        if (!(it.flags & Transparent) || !I.camFrustum.visible(it.center, it.radius)) continue;
        Vector3 c = it.radius >= 0.f ? it.center : Vector3{it.xf.m12, it.xf.m13, it.xf.m14};
        I.sorted.push_back({Vector3DistanceSqr(c, cam.position), 0, i, 0, 0});
    }
    for (int i = 0; i < (int)I.particles.size(); ++i)
        I.sorted.push_back({Vector3DistanceSqr(I.particles[i].pos, cam.position), 1, i, 0, 0});
    for (int i = 0; i < (int)I.boards.size(); ++i)
        I.sorted.push_back({Vector3DistanceSqr(I.boards[i].pos, cam.position), 2, i, 0, 0});
    std::sort(I.sorted.begin(), I.sorted.end(), [](const SortEntry& a, const SortEntry& b) { return a.dist > b.dist; });

    // all sprite geometry of the frame goes into one streamed buffer
    I.sv.clear();
    for (SortEntry& e : I.sorted) {
        e.first = (int)I.sv.size();
        if (e.kind == 1) I.emitParticle(I.particles[e.index], cam, camR, camU);
        else if (e.kind == 2) I.emitBoard(I.boards[e.index], cam, camR);
        e.count = (int)I.sv.size() - e.first;
    }
    const int glowFirst = (int)I.sv.size();
    I.emitGlows(cam, camR, camU);
    const int glowCount = (int)I.sv.size() - glowFirst;
    const int postFirst = (int)I.sv.size();
    {
        Color w[4] = {WHITE, WHITE, WHITE, WHITE};
        I.pushQuad({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, 0.f, 1.f, 1.f, 0.f, w);  // full-screen quad in NDC
    }
    I.uploadSprites();

    rlDisableDepthMask();
    // runs of consecutive sprites sharing texture + blend draw in one call; meshes break the runs
    int runFirst = 0, runCount = 0;
    unsigned int runTex = 0;
    bool runAdd = false, spriteState = false;
    const float16 vpf = MatrixToFloatV(I.viewProj);
    auto flushRun = [&]() {
        if (runCount <= 0) return;
        if (!spriteState) {
            glUseProgram(I.sprite.id);
            glUniformMatrix4fv(I.locSpriteMvp, 1, GL_FALSE, vpf.v);
            glBindVertexArray(I.spriteVao);
            glDisable(GL_CULL_FACE);
            spriteState = true;
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, runTex);
        if (runAdd) Impl::blendAdd();
        else Impl::blendAlpha();
        glDrawArrays(GL_TRIANGLES, runFirst, runCount);
        runCount = 0;
    };
    auto addRun = [&](int first, int count, unsigned int tex, bool add) {
        if (count <= 0) return;
        if (runCount > 0 && (tex != runTex || add != runAdd || first != runFirst + runCount)) flushRun();
        if (runCount == 0) {
            runFirst = first;
            runTex = tex;
            runAdd = add;
        }
        runCount += count;
    };
    for (const SortEntry& e : I.sorted) {
        if (e.kind == 1) {
            addRun(e.first, e.count, I.puffAtlas.id, false);
        } else if (e.kind == 2) {
            const Board& b = I.boards[e.index];
            addRun(e.first, e.count, b.tex.id, b.additive);
        } else {
            flushRun();
            if (spriteState) {
                glEnable(GL_CULL_FACE);
                spriteState = false;
            }
            const DrawItem& it = I.items[e.index];
            const bool add = it.flags & Additive;
            if (add) Impl::blendAdd();
            else Impl::blendAlpha();
            glUseProgram(I.lit.id);
            // a sprite run may have rebound texture unit 0 since the last mesh: rebind the material
            I.curMat = nullptr;
            I.curTex = 0xffffffffu;
            I.drawMeshItem(it, it.mat->alpha, add ? 2.f : 1.f);
        }
    }
    addRun(glowFirst, glowCount, I.glowAtlas.id, true);
    flushRun();
    if (spriteState) glEnable(GL_CULL_FACE);
    Impl::blendAlpha();
    rlEnableDepthMask();
    glBindVertexArray(0);

    if (bindShadow) {
        glBindSampler(10, 0);
        glBindSampler(11, 0);
        glActiveTexture(GL_TEXTURE0 + 10);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0 + 11);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
    }
    glUseProgram(0);
    EndMode3D();
    rlSetClipPlanes(RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);
    I.postPass(target, postFirst);
    if (target) EndTextureMode();

    I.items.clear();
    I.glows.clear();
    I.boards.clear();
    I.points.clear();
}

const Camera3D& Renderer::lastCamera() const { return impl_->lastCam; }

bool Renderer::projectToVirtual(Vector3 world, Vector2& out) const {
    const Impl& I = *impl_;
    const Camera3D& cam = I.lastCam;
    Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    if (Vector3DotProduct(Vector3Subtract(world, cam.position), fwd) <= 0.01f) return false;
    Vector2 s = GetWorldToScreenEx(world, cam, I.renderW, I.renderH);
    const ui::Viewport& vp = ui::currentViewport();
    float sc = vp.scale > 0 ? vp.scale : 1.f;
    out = {(s.x - vp.offset.x) / sc, (s.y - vp.offset.y) / sc};
    return true;
}

Ray Renderer::rayFromVirtual(Vector2 v) const {
    const Impl& I = *impl_;
    const ui::Viewport& vp = ui::currentViewport();
    float sc = vp.scale > 0 ? vp.scale : 1.f;
    Vector2 s{v.x * sc + vp.offset.x, v.y * sc + vp.offset.y};
    return GetScreenToWorldRayEx(s, I.lastCam, I.renderW, I.renderH);
}

Shader Renderer::litShader() const { return impl_->lit; }
Texture2D Renderer::whiteTexture() const { return impl_->white; }
Texture2D Renderer::puffTexture() const { return impl_->puff; }
Texture2D Renderer::glowTexture() const { return impl_->glow; }

// ============================================================================ MeshBuilder
void MeshBuilder::clear() {
    pos_.clear();
    nrm_.clear();
    uv_.clear();
    col_.clear();
    idx_.clear();
}
int MeshBuilder::vertexCount() const { return (int)pos_.size() / 3; }

void MeshBuilder::setTransform(const Matrix& m) {
    xf_ = m;
    nxf_ = MatrixTranspose(MatrixInvert(m));
    hasXf_ = true;
}
void MeshBuilder::resetTransform() {
    xf_ = MatrixIdentity();
    nxf_ = MatrixIdentity();
    hasXf_ = false;
}

int MeshBuilder::vertex(Vector3 p, Vector3 n, Vector2 uv, Color c) {
    if (hasXf_) {
        p = Vector3Transform(p, xf_);
        const Matrix& nm = nxf_;
        n = Vector3{nm.m0 * n.x + nm.m4 * n.y + nm.m8 * n.z, nm.m1 * n.x + nm.m5 * n.y + nm.m9 * n.z,
                    nm.m2 * n.x + nm.m6 * n.y + nm.m10 * n.z};
    }
    float l = Vector3Length(n);
    n = l > 1e-8f ? Vector3Scale(n, 1.f / l) : Vector3{0, 1, 0};
    pos_.insert(pos_.end(), {p.x, p.y, p.z});
    nrm_.insert(nrm_.end(), {n.x, n.y, n.z});
    uv_.insert(uv_.end(), {uv.x, uv.y});
    col_.insert(col_.end(), {c.r, c.g, c.b, c.a});
    return vertexCount() - 1;
}
void MeshBuilder::triangle(int a, int b, int c) {
    // A mirroring transform (negative determinant) would turn every primitive inside out: keep the
    // documented "counter-clockwise in local space = front" by flipping the winding.
    if (hasXf_) {
        const Matrix& m = xf_;
        float det = m.m0 * (m.m5 * m.m10 - m.m9 * m.m6) - m.m4 * (m.m1 * m.m10 - m.m9 * m.m2) +
                    m.m8 * (m.m1 * m.m6 - m.m5 * m.m2);
        if (det < 0.f) std::swap(b, c);
    }
    idx_.insert(idx_.end(), {(unsigned)a, (unsigned)b, (unsigned)c});
}
void MeshBuilder::quad(int a, int b, int c, int d) {
    triangle(a, b, c);
    triangle(a, c, d);
}

void MeshBuilder::box(Vector3 c, Vector3 s, Color col) {
    Vector3 h = Vector3Scale(s, 0.5f);
    struct F {
        Vector3 n, u, v;
    };
    const F faces[6] = {{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
                        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
                        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}};
    for (const F& f : faces) {
        auto P = [&](float a, float b) {
            return Vector3{c.x + (f.n.x + f.u.x * a + f.v.x * b) * h.x, c.y + (f.n.y + f.u.y * a + f.v.y * b) * h.y,
                           c.z + (f.n.z + f.u.z * a + f.v.z * b) * h.z};
        };
        int i0 = vertex(P(-1, -1), f.n, {0, 1}, col);
        int i1 = vertex(P(1, -1), f.n, {1, 1}, col);
        int i2 = vertex(P(1, 1), f.n, {1, 0}, col);
        int i3 = vertex(P(-1, 1), f.n, {0, 0}, col);
        quad(i0, i1, i2, i3);
    }
}

void MeshBuilder::roundedBox(Vector3 c, Vector3 s, float r, int seg, Color col) {
    Vector3 h = Vector3Scale(s, 0.5f);
    r = std::clamp(r, 0.f, std::min({h.x, h.y, h.z}));
    if (r <= 1e-6f) {
        box(c, s, col);
        return;
    }
    seg = std::max(seg, 1);
    Vector3 hi{h.x - r, h.y - r, h.z - r};
    struct F {
        Vector3 n, u, v;
    };
    const F faces[6] = {{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
                        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
                        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}};
    auto comp = [](Vector3 v, Vector3 axis) { return v.x * axis.x + v.y * axis.y + v.z * axis.z; };
    auto absAxis = [](Vector3 a) { return Vector3{std::fabs(a.x), std::fabs(a.y), std::fabs(a.z)}; };
    // Rows along one face axis: the bevel band [-h..-hi] (tan-spaced, so the bevel is split into equal
    // angles up to the 45deg seam with the neighbouring face), the flat middle, then [hi..h]. Each face
    // owns the part of every edge/corner where its axis dominates (cube -> rounded box projection), so
    // neighbouring faces meet exactly without overlap.
    auto coords = [&](float hh, float hin, std::vector<float>& out) {
        out.clear();
        float rr = hh - hin;
        for (int k = 0; k <= seg; ++k) out.push_back(-hin - rr * std::tan((1.f - (float)k / seg) * PI * 0.25f));
        for (int k = hin > 1e-6f ? 0 : 1; k <= seg; ++k) out.push_back(hin + rr * std::tan((float)k / seg * PI * 0.25f));
    };
    std::vector<float> cu, cv;
    for (const F& f : faces) {
        float hu = comp(h, absAxis(f.u)), hv = comp(h, absAxis(f.v)), hn = comp(h, absAxis(f.n));
        float iu = comp(hi, absAxis(f.u)), iv = comp(hi, absAxis(f.v));
        coords(hu, iu, cu);
        coords(hv, iv, cv);
        int nu = (int)cu.size(), nv = (int)cv.size();
        int base = vertexCount();
        for (int j = 0; j < nv; ++j)
            for (int i = 0; i < nu; ++i) {
                Vector3 p = Vector3Add(Vector3Add(Vector3Scale(f.n, hn), Vector3Scale(f.u, cu[i])),
                                       Vector3Scale(f.v, cv[j]));
                Vector3 q{std::clamp(p.x, -hi.x, hi.x), std::clamp(p.y, -hi.y, hi.y), std::clamp(p.z, -hi.z, hi.z)};
                Vector3 d = Vector3Subtract(p, q);
                Vector3 n = Vector3Length(d) > 1e-7f ? Vector3Normalize(d) : f.n;
                Vector3 pos = Vector3Add(Vector3Add(q, Vector3Scale(n, r)), c);
                Vector2 uv{(cu[i] + hu) / (2.f * hu), 1.f - (cv[j] + hv) / (2.f * hv)};
                vertex(pos, n, uv, col);
            }
        for (int j = 0; j + 1 < nv; ++j)
            for (int i = 0; i + 1 < nu; ++i) {
                int a = base + j * nu + i, b = a + 1, cc = a + nu + 1, d = a + nu;
                quad(a, b, cc, d);
            }
    }
}

void MeshBuilder::lathe(const std::vector<Vector2>& prof, int seg, bool capB, bool capT, Color col) {
    int np = (int)prof.size();
    if (np < 2) return;
    seg = std::max(seg, 3);
    std::vector<Vector2> nrm(np);
    for (int i = 0; i < np; ++i) {
        // normal from the neighbouring segments (averaged), outward for a bottom->top profile
        Vector2 a = prof[std::max(i - 1, 0)], b = prof[std::min(i + 1, np - 1)];
        Vector2 t{b.x - a.x, b.y - a.y};
        Vector2 n{t.y, -t.x};
        float l = std::sqrt(n.x * n.x + n.y * n.y);
        nrm[i] = l > 1e-7f ? Vector2{n.x / l, n.y / l} : Vector2{1, 0};
    }
    float total = 0.f;
    std::vector<float> acc(np, 0.f);
    for (int i = 1; i < np; ++i) {
        float dx = prof[i].x - prof[i - 1].x, dy = prof[i].y - prof[i - 1].y;
        total += std::sqrt(dx * dx + dy * dy);
        acc[i] = total;
    }
    int base = vertexCount();
    for (int i = 0; i < np; ++i)
        for (int j = 0; j <= seg; ++j) {
            float a = 2.f * PI * j / seg;
            float s = std::sin(a), c = std::cos(a);
            Vector3 p{prof[i].x * s, prof[i].y, prof[i].x * c};
            Vector3 n{nrm[i].x * s, nrm[i].y, nrm[i].x * c};
            vertex(p, n, {j / (float)seg, total > 0 ? 1.f - acc[i] / total : 0.f}, col);
        }
    for (int i = 0; i + 1 < np; ++i) {
        // rings on the axis (radius 0) are poles: skip the zero-area half of each quad there
        const bool poleLo = prof[i].x <= 1e-7f, poleHi = prof[i + 1].x <= 1e-7f;
        for (int j = 0; j < seg; ++j) {
            int a = base + i * (seg + 1) + j, b = a + 1, cc = a + (seg + 1) + 1, d = a + (seg + 1);
            if (!poleLo) triangle(a, b, cc);
            if (!poleHi) triangle(a, cc, d);
        }
    }
    auto cap = [&](const Vector2& pr, bool top) {
        if (pr.x <= 1e-6f) return;
        Vector3 n{0, top ? 1.f : -1.f, 0};
        int center = vertex({0, pr.y, 0}, n, {0.5f, 0.5f}, col);
        int ring = vertexCount();
        for (int j = 0; j <= seg; ++j) {
            float a = 2.f * PI * j / seg;
            vertex({pr.x * std::sin(a), pr.y, pr.x * std::cos(a)}, n,
                   {0.5f + 0.5f * std::sin(a), 0.5f - 0.5f * std::cos(a)}, col);
        }
        for (int j = 0; j < seg; ++j) {
            if (top) triangle(center, ring + j, ring + j + 1);
            else triangle(center, ring + j + 1, ring + j);
        }
    };
    if (capB) cap(prof.front(), false);
    if (capT) cap(prof.back(), true);
}

void MeshBuilder::cylinder(Vector3 b, float r, float h, int seg, Color col) {
    Matrix save = xf_;
    bool saveHas = hasXf_;
    Matrix t = MatrixTranslate(b.x, b.y, b.z);
    setTransform(saveHas ? MatrixMultiply(t, save) : t);
    // duplicated rim rows give the caps a crisp edge instead of a smeared normal
    lathe({{r, 0.f}, {r, h}}, seg, true, true, col);
    if (saveHas) setTransform(save);
    else resetTransform();
}

void MeshBuilder::ellipsoid(Vector3 c, Vector3 rad, int rings, int slices, Color col) {
    rings = std::max(rings, 3);
    slices = std::max(slices, 3);
    int base = vertexCount();
    for (int i = 0; i <= rings; ++i) {
        float th = PI * i / rings; // 0 top .. PI bottom
        for (int j = 0; j <= slices; ++j) {
            float ph = 2.f * PI * j / slices;
            Vector3 u{std::sin(th) * std::sin(ph), std::cos(th), std::sin(th) * std::cos(ph)};
            Vector3 p{c.x + u.x * rad.x, c.y + u.y * rad.y, c.z + u.z * rad.z};
            Vector3 n = Vector3Normalize({u.x / std::max(rad.x, 1e-6f), u.y / std::max(rad.y, 1e-6f),
                                         u.z / std::max(rad.z, 1e-6f)});
            vertex(p, n, {j / (float)slices, i / (float)rings}, col);
        }
    }
    for (int i = 0; i < rings; ++i)
        for (int j = 0; j < slices; ++j) {
            int a = base + i * (slices + 1) + j, b = a + 1, d = a + (slices + 1), cc = d + 1;
            // rows go top -> bottom; CCW from outside. Skip the degenerate triangle at each pole.
            if (i > 0) triangle(a, d, b);
            if (i + 1 < rings) triangle(b, d, cc);
        }
}

void MeshBuilder::sphere(Vector3 c, float r, int rings, int slices, Color col) { ellipsoid(c, {r, r, r}, rings, slices, col); }

void MeshBuilder::capsule(Vector3 a, Vector3 b, float r, int seg, Color col) {
    Vector3 d = Vector3Subtract(b, a);
    float len = Vector3Length(d);
    std::vector<Vector2> prof;
    int hs = std::max(seg / 2, 3);
    for (int i = 0; i <= hs; ++i) {
        float t = -PI / 2 + (PI / 2) * i / hs;
        prof.push_back({r * std::cos(t), r * std::sin(t)});
    }
    for (int i = 0; i <= hs; ++i) {
        float t = (PI / 2) * i / hs;
        prof.push_back({r * std::cos(t), len + r * std::sin(t)});
    }
    Matrix save = xf_;
    bool saveHas = hasXf_;
    Vector3 y{0, 1, 0};
    Vector3 dir = len > 1e-6f ? Vector3Scale(d, 1.f / len) : y;
    Vector3 axis = Vector3CrossProduct(y, dir);
    float ang = std::acos(std::clamp(Vector3DotProduct(y, dir), -1.f, 1.f));
    Matrix rot = Vector3Length(axis) > 1e-6f ? MatrixRotate(Vector3Normalize(axis), ang)
                                             : (dir.y < 0 ? MatrixRotateX(PI) : MatrixIdentity());
    Matrix m = MatrixMultiply(rot, MatrixTranslate(a.x, a.y, a.z));
    setTransform(saveHas ? MatrixMultiply(m, save) : m);
    lathe(prof, seg, false, false, col);
    if (saveHas) setTransform(save);
    else resetTransform();
}

void MeshBuilder::tube(const std::vector<Vector3>& path, float r, int seg, Color col) {
    int n = (int)path.size();
    if (n < 2) return;
    seg = std::max(seg, 3);
    const bool closed = n > 3 && Vector3Distance(path.front(), path.back()) < 1e-5f;
    auto tangent = [&](int i) {
        Vector3 t;
        if (i == 0 || i == n - 1) {
            t = i == 0 ? Vector3Subtract(path[1], path[0]) : Vector3Subtract(path[n - 1], path[n - 2]);
            if (closed) t = Vector3Add(Vector3Normalize(Vector3Subtract(path[1], path[0])),
                                       Vector3Normalize(Vector3Subtract(path[n - 1], path[n - 2])));
        } else {
            t = Vector3Add(Vector3Normalize(Vector3Subtract(path[i + 1], path[i])),
                           Vector3Normalize(Vector3Subtract(path[i], path[i - 1])));
        }
        float l = Vector3Length(t);
        return l > 1e-8f ? Vector3Scale(t, 1.f / l) : Vector3{0, 1, 0};
    };
    Vector3 t0 = tangent(0);
    Vector3 ref = std::fabs(t0.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
    Vector3 nrm = Vector3Normalize(Vector3CrossProduct(t0, ref));
    int base = vertexCount();
    float acc = 0.f;
    Vector3 firstN{}, firstB{}, lastN{}, lastB{}, lastT{};
    for (int i = 0; i < n; ++i) {
        Vector3 t = tangent(i);
        nrm = Vector3Normalize(Vector3Subtract(nrm, Vector3Scale(t, Vector3DotProduct(nrm, t)))); // transport
        Vector3 bin = Vector3CrossProduct(t, nrm);
        if (i > 0) acc += Vector3Distance(path[i], path[i - 1]);
        if (i == 0) {
            firstN = nrm;
            firstB = bin;
        }
        lastN = nrm;
        lastB = bin;
        lastT = t;
        for (int j = 0; j <= seg; ++j) {
            float a = 2.f * PI * j / seg;
            Vector3 dir = Vector3Add(Vector3Scale(nrm, std::cos(a)), Vector3Scale(bin, std::sin(a)));
            vertex(Vector3Add(path[i], Vector3Scale(dir, r)), dir, {j / (float)seg, acc}, col);
        }
    }
    for (int i = 0; i + 1 < n; ++i)
        for (int j = 0; j < seg; ++j) {
            int a = base + i * (seg + 1) + j, b = a + 1, d = a + (seg + 1), cc = d + 1;
            quad(a, b, cc, d);
        }
    if (closed) return;
    // flat end caps so open tubes don't show their hollow inside
    auto cap = [&](Vector3 c, Vector3 nn, Vector3 bb, Vector3 facing, bool end) {
        int center = vertex(c, facing, {0.5f, 0.5f}, col);
        int ring = vertexCount();
        for (int j = 0; j <= seg; ++j) {
            float a = 2.f * PI * j / seg;
            Vector3 dir = Vector3Add(Vector3Scale(nn, std::cos(a)), Vector3Scale(bb, std::sin(a)));
            vertex(Vector3Add(c, Vector3Scale(dir, r)), facing, {0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)}, col);
        }
        for (int j = 0; j < seg; ++j) {
            if (end) triangle(center, ring + j, ring + j + 1);
            else triangle(center, ring + j + 1, ring + j);
        }
    };
    cap(path.front(), firstN, firstB, Vector3Negate(t0), false);
    cap(path.back(), lastN, lastB, lastT, true);
}

void MeshBuilder::plane(Vector3 c, Vector2 size, Vector3 normal, Vector2 uvScale, Color col) {
    Vector3 n = Vector3Normalize(normal);
    Vector3 ref = std::fabs(n.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{0, 0, -1};
    Vector3 u = Vector3Normalize(Vector3CrossProduct(ref, n)); // right
    Vector3 v = Vector3CrossProduct(n, u);                     // up
    Vector3 hu = Vector3Scale(u, size.x * 0.5f), hv = Vector3Scale(v, size.y * 0.5f);
    int a = vertex(Vector3Subtract(Vector3Subtract(c, hu), hv), n, {0, uvScale.y}, col);
    int b = vertex(Vector3Subtract(Vector3Add(c, hu), hv), n, {uvScale.x, uvScale.y}, col);
    int cc = vertex(Vector3Add(Vector3Add(c, hu), hv), n, {uvScale.x, 0}, col);
    int d = vertex(Vector3Add(Vector3Subtract(c, hu), hv), n, {0, 0}, col);
    quad(a, b, cc, d);
}

Mesh MeshBuilder::build(bool withColors) const {
    Mesh m{};
    int vc = vertexCount();
    if (vc == 0 || idx_.empty()) return m;
    const bool indexed = vc <= 65535;
    auto alloc = [](size_t n) { return RL_MALLOC(n); };
    if (indexed) {
        m.vertexCount = vc;
        m.triangleCount = (int)idx_.size() / 3;
        m.vertices = (float*)alloc(pos_.size() * sizeof(float));
        m.normals = (float*)alloc(nrm_.size() * sizeof(float));
        m.texcoords = (float*)alloc(uv_.size() * sizeof(float));
        std::memcpy(m.vertices, pos_.data(), pos_.size() * sizeof(float));
        std::memcpy(m.normals, nrm_.data(), nrm_.size() * sizeof(float));
        std::memcpy(m.texcoords, uv_.data(), uv_.size() * sizeof(float));
        if (withColors) {
            m.colors = (unsigned char*)alloc(col_.size());
            std::memcpy(m.colors, col_.data(), col_.size());
        }
        m.indices = (unsigned short*)alloc(idx_.size() * sizeof(unsigned short));
        for (size_t i = 0; i < idx_.size(); ++i) m.indices[i] = (unsigned short)idx_[i];
    } else {
        int n = (int)idx_.size();
        m.vertexCount = n;
        m.triangleCount = n / 3;
        m.vertices = (float*)alloc(n * 3 * sizeof(float));
        m.normals = (float*)alloc(n * 3 * sizeof(float));
        m.texcoords = (float*)alloc(n * 2 * sizeof(float));
        if (withColors) m.colors = (unsigned char*)alloc(n * 4);
        for (int i = 0; i < n; ++i) {
            unsigned v = idx_[i];
            std::memcpy(m.vertices + i * 3, &pos_[v * 3], 3 * sizeof(float));
            std::memcpy(m.normals + i * 3, &nrm_[v * 3], 3 * sizeof(float));
            std::memcpy(m.texcoords + i * 2, &uv_[v * 2], 2 * sizeof(float));
            if (withColors) std::memcpy(m.colors + i * 4, &col_[v * 4], 4);
        }
    }
    UploadMesh(&m, false);
    return m;
}

Mesh genRoundedBox(float w, float h, float d, float radius, int seg) {
    MeshBuilder b;
    b.roundedBox({0, 0, 0}, {w, h, d}, radius, seg);
    return b.build();
}

Mesh genLathe(const std::vector<Vector2>& profile, int segments, bool capBottom, bool capTop) {
    MeshBuilder b;
    b.lathe(profile, segments, capBottom, capTop);
    return b.build();
}

Mesh genCanvasQuad(float width, float height) {
    MeshBuilder b;
    float hw = width * 0.5f, hh = height * 0.5f;
    Vector3 n{0, 0, 1};
    // canvas textures are upside down: v = 0 at the bottom edge
    int a = b.vertex({-hw, -hh, 0}, n, {0, 0});
    int c1 = b.vertex({hw, -hh, 0}, n, {1, 0});
    int c2 = b.vertex({hw, hh, 0}, n, {1, 1});
    int d = b.vertex({-hw, hh, 0}, n, {0, 1});
    b.quad(a, c1, c2, d);
    return b.build();
}

// ============================================================================ textures
Texture2D textureFromImage(Image img) {
    Texture2D t = LoadTextureFromImage(img);
    bool opaque = img.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8 || img.format == PIXELFORMAT_UNCOMPRESSED_GRAYSCALE ||
                  img.format == PIXELFORMAT_UNCOMPRESSED_R5G6B5;
    if (img.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 && img.data) {
        const Color* px = (const Color*)img.data;
        opaque = true;
        for (int i = 0, n = img.width * img.height; i < n && opaque; ++i) opaque = px[i].a == 255;
    }
    if (t.id) {
        if (opaque) opaqueTextures().insert(t.id);
        else opaqueTextures().erase(t.id);
    }
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);   // min: linear-mipmap-linear, mag: linear
    rlTextureParameters(t.id, RL_TEXTURE_FILTER_ANISOTROPIC, 8);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    return t;
}

// Grain runs along u (horizontal); growth rings stack along v. `rings` is rounded so the texture tiles.
Texture2D genWoodTexture(int size, Color light, Color dark, uint32_t seed, float rings) {
    size = std::max(size, 16);
    const int R = std::max(1, (int)std::lround(rings));
    Image img = GenImageColor(size, size, light);
    Color* px = (Color*)img.data;
    const int poreF = std::max(32, size / 3);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float u = (x + 0.5f) / size, v = (y + 0.5f) / size;
            // flat-sawn figure: rings wander slowly along the grain, with a few cathedral arches
            float figure = fbm2(u, v, 2, 3, 4, seed) - 0.5f;
            float wav = fbm2(u, v, 3, 10, 3, seed + 7u) - 0.5f;
            float phase = v * R + figure * 2.6f + wav * 0.45f;
            float f = phase - std::floor(phase);
            // earlywood (light, wide) -> latewood (dark, narrow) with a crisp end of the season
            float late = smoothstepf(0.55f, 0.88f, f) * (1.f - smoothstepf(0.93f, 1.0f, f));
            // fibres and pores: noise stretched along the grain
            float fib = fbm2(u, v, 3, poreF, 2, seed + 3u) - 0.5f;
            float pore = std::max(fbm2(u, v, 12, poreF * 2, 1, seed + 11u) - 0.62f, 0.f) * 2.6f;
            // colour variation across the board and a faint sheen band
            float streak = fbm2(u, v, 1, 6, 3, seed + 19u) - 0.5f;
            float t = 0.16f + late * 0.52f + fib * 0.30f + pore * 0.35f + streak * 0.30f;
            Color c = mixc(light, dark, std::clamp(t, 0.f, 1.f));
            float warm = 1.f + streak * 0.08f;
            px[y * size + x] = scalec(c, warm, 1.f, 1.f / warm);
        }
    Texture2D t = textureFromImage(img);
    UnloadImage(img);
    return t;
}

// Worn baize: soft mottling, a fine nap and thousands of short fibres (all wrapped, so it tiles).
Texture2D genFeltTexture(int size, Color base, uint32_t seed) {
    size = std::max(size, 16);
    std::vector<float> acc((size_t)size * size, 0.f);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float u = (x + 0.5f) / size, v = (y + 0.5f) / size;
            float mott = fbm2(u, v, 3, 3, 4, seed) - 0.5f;
            float nap = fbm2(u, v, 24, 24, 2, seed + 1u) - 0.5f;
            acc[(size_t)y * size + x] = mott * 0.13f + nap * 0.07f + (hash2(x, y, seed + 5u) - 0.5f) * 0.05f;
        }
    uint32_t s = seed * 7919u + 17u;
    const int fibres = size * size / 9;
    for (int k = 0; k < fibres; ++k) {
        float x0 = rnd(s) * size, y0 = rnd(s) * size;
        float ang = rnd(s) * 6.2832f;
        float len = 2.f + rnd(s) * rnd(s) * 9.f * (size / 512.f);
        float amp = (rnd(s) - 0.42f) * 0.10f;
        float dx = std::cos(ang) * 0.5f, dy = std::sin(ang) * 0.5f;
        int steps = (int)(len * 2.f);
        for (int st = 0; st <= steps; ++st) {
            float fx = x0 + dx * st, fy = y0 + dy * st;
            int ix = (int)std::floor(fx), iy = (int)std::floor(fy);
            float ax = fx - ix, ay = fy - iy;
            float w[4] = {(1 - ax) * (1 - ay), ax * (1 - ay), (1 - ax) * ay, ax * ay};
            int ox[4] = {0, 1, 0, 1}, oy[4] = {0, 0, 1, 1};
            float taper = 1.f - std::fabs(2.f * st / std::max(steps, 1) - 1.f) * 0.6f;
            for (int q = 0; q < 4; ++q)
                acc[(size_t)wrapi(iy + oy[q], size) * size + wrapi(ix + ox[q], size)] += amp * w[q] * taper;
        }
    }
    Image img = GenImageColor(size, size, base);
    Color* px = (Color*)img.data;
    for (int i = 0; i < size * size; ++i) {
        float k = 1.f + acc[i];
        // bright fibres are a touch less saturated (light catching wool)
        float lift = std::max(acc[i], 0.f) * 0.35f;
        px[i] = Color{u8(base.r * k + 255.f * lift * 0.18f), u8(base.g * k + 255.f * lift * 0.14f),
                      u8(base.b * k + 255.f * lift * 0.12f), 255};
    }
    Texture2D t = textureFromImage(img);
    UnloadImage(img);
    return t;
}

Texture2D genNoiseTexture(int size, Color a, Color b, float scale, uint32_t seed) {
    size = std::max(size, 4);
    Image img = GenImageColor(size, size, a);
    Color* px = (Color*)img.data;
    int f = std::max(1, (int)std::lround(scale));
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float u = (x + 0.5f) / size, v = (y + 0.5f) / size;
            px[y * size + x] = mixc(a, b, fbm(u, v, f, 5, seed));
        }
    Texture2D t = textureFromImage(img);
    UnloadImage(img);
    return t;
}

// Old distemper paint: uneven coats and trowel marks, nicotine/water blotches with darker tide lines,
// streaks running down (v grows downward), a few hairline cracks and fine grain. Tiles seamlessly.
Texture2D genPlasterTexture(int size, Color base, uint32_t seed) {
    size = std::max(size, 16);
    Image img = GenImageColor(size, size, base);
    Color* px = (Color*)img.data;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float u = (x + 0.5f) / size, v = (y + 0.5f) / size;
            float paint = fbm2(u, v, 3, 3, 5, seed) - 0.5f;
            float trowel = fbm2(u, v, 7, 4, 3, seed + 2u) - 0.5f;
            float s = fbm2(u, v, 2, 2, 5, seed + 4u);
            float stain = smoothstepf(0.54f, 0.62f, s);
            float tide = std::exp(-((s - 0.575f) * (s - 0.575f)) / (0.011f * 0.011f));
            float drip = std::pow(std::clamp(fbm2(u, v, 36, 2, 2, seed + 6u) * 1.25f - 0.25f, 0.f, 1.f), 5.f);
            drip *= 0.4f + 0.6f * fbm2(u, v, 4, 3, 2, seed + 7u);
            float cf = fbm2(u, v, 4, 4, 5, seed + 8u);
            float crack = (1.f - smoothstepf(0.f, 0.0075f, std::fabs(cf - 0.5f))) *
                          smoothstepf(0.56f, 0.7f, fbm2(u, v, 2, 2, 2, seed + 9u));
            float grain = hash2(x, y, seed + 17u) - 0.5f;
            float k = 1.06f + paint * 0.14f + trowel * 0.06f - stain * 0.10f - tide * 0.10f - drip * 0.13f -
                      crack * 0.30f + grain * 0.045f;
            float yel = stain * 0.10f + tide * 0.08f + drip * 0.06f;  // nicotine eats the blue first
            px[y * size + x] = Color{u8(base.r * k), u8(base.g * k * (1.f - yel * 0.35f)),
                                     u8(base.b * k * (1.f - yel)), 255};
        }
    Texture2D t = textureFromImage(img);
    UnloadImage(img);
    return t;
}

RenderTexture2D makeCanvas(int width, int height) {
    RenderTexture2D rt = LoadRenderTexture(width, height);
    opaqueTextures().erase(rt.texture.id);
    SetTextureFilter(rt.texture, TEXTURE_FILTER_BILINEAR);
    return rt;
}
void beginCanvas(RenderTexture2D& canvas) {
    BeginTextureMode(canvas);
    ClearBackground(Color{0, 0, 0, 0});
}
void endCanvas(RenderTexture2D& canvas) {
    EndTextureMode();
    GenTextureMipmaps(&canvas.texture);
    SetTextureFilter(canvas.texture, TEXTURE_FILTER_TRILINEAR);
    rlTextureParameters(canvas.texture.id, RL_TEXTURE_FILTER_ANISOTROPIC, 4);
}

// ============================================================================ transforms
Matrix trs(Vector3 t, Vector3 r, Vector3 s) {
    Matrix m = MatrixScale(s.x, s.y, s.z);
    m = MatrixMultiply(m, MatrixRotateX(r.x * DEG2RAD));
    m = MatrixMultiply(m, MatrixRotateY(r.y * DEG2RAD));
    m = MatrixMultiply(m, MatrixRotateZ(r.z * DEG2RAD));
    return MatrixMultiply(m, MatrixTranslate(t.x, t.y, t.z));
}
Matrix trsYaw(Vector3 t, float yawDeg, float scale) {
    Matrix m = MatrixScale(scale, scale, scale);
    m = MatrixMultiply(m, MatrixRotateY(yawDeg * DEG2RAD));
    return MatrixMultiply(m, MatrixTranslate(t.x, t.y, t.z));
}

} // namespace r3d
