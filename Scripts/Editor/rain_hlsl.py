# Shared HLSL of the rain surfaces (section 18, docs/systems/environment-weather.md): the wet overlays (create_wet_surface_materials.py)
# and the landscape function MF_RainWetSurface (landscape_rain_patch.py). Plain strings, no unreal import: the scripts load this file by path.

# Procedural rain ripples (no texture, no sampler): two layers of cells; each cell has a random centre and phase, a ring grows from it and
# fades over one period. Returns the height slope (d/dx, d/dy) of the ripple field (unit-free; scale with RippleStrength).
# Inputs: WP (world position, cm), Time (s), CellCm, Speed (rings per second per cell), Ring (ring width in cell units).
RIPPLES = r"""
float2 slope = float2(0.0, 0.0);
[unroll] for (int layer = 0; layer < 2; ++layer)
{
    float cell = max(CellCm, 1.0) * (layer == 0 ? 1.0 : 0.71);
    float2 q = WP.xy / cell + (layer == 0 ? float2(0.0, 0.0) : float2(17.3, 41.9));
    float2 id = floor(q);
    float2 f = q - id;
    float h1 = frac(sin(dot(id, float2(127.1, 311.7))) * 43758.5453);
    float h2 = frac(sin(dot(id, float2(269.5, 183.3))) * 43758.5453);
    float h3 = frac(sin(dot(id, float2(419.2, 371.9))) * 43758.5453);
    float2 c = 0.3 + 0.4 * float2(h1, h2);
    float phase = frac(Time * Speed * (0.8 + 0.4 * h2) + h3);
    float2 d = f - c;
    float r = length(d);
    float x = (r - phase * 0.3) / max(Ring, 0.005);
    float env = saturate(1.0 - abs(x) * 0.5) * (1.0 - phase) * (1.0 - phase);
    float wave = sin(x * 3.14159265) * env;
    slope += (r > 0.0001 ? d / r : float2(0.0, 0.0)) * wave;
}
return slope;
"""

# The puddle noise field ~N(0.5, 0.2) from two lookups of T_TilingNoisePacked (R 0.31 +- 0.175 macro, G 0.50 +- 0.19 mid; the channel
# statistics measured for the snow ground overlay, section 12.d). Inputs: NoiseMacro, NoiseMid (RGBA).
PUDDLE_FIELD = r"""
float a = (NoiseMacro.r - 0.31) / 0.175;
float b = (NoiseMid.g - 0.50) / 0.19;
return 0.5 + 0.2 * (0.8 * a + 0.2 * b) / 0.825;
"""

# Puddle mask from the field: none at PuddleAmount 0 (exactly 0), up to ~PuddleFull coverage at 1; flat faces only; no standing water on a
# dry ground. Inputs: Field, PuddleAmount, Wetness, Up (world normal z), PuddleBare, PuddleFull, PuddleEdge, FlatMin.
PUDDLE_MASK = r"""
float p = saturate(PuddleAmount);
float th = lerp(PuddleBare, PuddleFull, sqrt(p));
float m = smoothstep(th - PuddleEdge, th + PuddleEdge, Field);
m *= saturate((Up - FlatMin) / max(1.0 - FlatMin, 0.001));
m *= saturate(saturate(Wetness) * 2.0);
return m * step(0.0001, p);
"""

# Interior zones (the snow ground overlay's rule, section 12.d): 1 inside any of the 6 rectangles (A = centre XY, half extent XY;
# B = cos yaw, sin yaw, top Z, bottom Z), 40 cm soft edge - the wet ground stays dry there.
ZONES = r"""
float4 A[6] = { Z0A, Z1A, Z2A, Z3A, Z4A, Z5A };
float4 B[6] = { Z0B, Z1B, Z2B, Z3B, Z4B, Z5B };
float inside = 0.0;
[unroll] for (int i = 0; i < 6; ++i)
{
    float4 a = A[i];
    float4 b = B[i];
    if (a.z > 0.0)
    {
        float2 d = WP.xy - a.xy;
        float2 l = float2(d.x * b.x + d.y * b.y, -d.x * b.y + d.y * b.x);
        float2 e = a.zw - abs(l);
        float m = saturate(min(e.x, e.y) / max(SoftCm, 1.0) + 0.5);
        m *= step(WP.z, b.z) * step(b.w - 100.0, WP.z);
        inside = max(inside, m);
    }
}
return inside;
"""
