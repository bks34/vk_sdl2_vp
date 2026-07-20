#version 450

layout(location = 0) in  vec2 fragTexCoord;
layout(location = 0) out vec4 fragColor;

layout(binding  = 0) uniform sampler2D texY;
layout(binding  = 1) uniform sampler2D texUV;
layout(push_constant) uniform PushBlock { ivec2 cfg; } p;
// cfg.x: 0=BT.601  1=BT.709  2=BT.2020
// cfg.y: 0=limited  1=full

// BT.709 limited-range Y-UV coefficient sets (pre-scaled for [0,1] input)
// Each row: { Cr_for_R,  Cb_for_G, Cr_for_G,  Cb_for_B }
const vec4 L709 = vec4(1.7927, -0.2132, -0.5329, 2.1124);
const vec4 F709 = vec4(1.5748, -0.1873, -0.4681, 1.8556);
const vec4 L601 = vec4(1.5958, -0.3938, -0.8130, 2.0172);
const vec4 F601 = vec4(1.402,  -0.3441, -0.7141, 1.772);
const vec4 L2020= vec4(1.6787, -0.1873, -0.6504, 2.1418);
const vec4 F2020= vec4(1.4746, -0.1646, -0.5714, 1.8814);

vec3 srgbToLinear(vec3 c) {
    // Exact sRGB EOTF: undo gamma so swapchain encode is identity
    bvec3 hi = greaterThan(c, vec3(0.04045));
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), hi);
}

void main() {
    float y = texture(texY,  fragTexCoord).r;
    vec2  uv = texture(texUV, fragTexCoord).rg - 0.5;

    // Y expansion for limited range
    if (p.cfg.y == 0) y = 1.1644 * (y - 0.0627);

    // Select coefficient set
    vec4 c;
    if (p.cfg.x == 2)      c = (p.cfg.y == 0) ? L2020 : F2020;
    else if (p.cfg.x == 1) c = (p.cfg.y == 0) ? L709  : F709;
    else                   c = (p.cfg.y == 0) ? L601  : F601;

    // RGB = [Y + Cr·c.x,  Y + Cb·c.y + Cr·c.z,  Y + Cb·c.w]
    vec3 rgb;
    rgb.r = y + c.x * uv.y;
    rgb.g = y + c.y * uv.x + c.z * uv.y;
    rgb.b = y + c.w * uv.x;

    fragColor = vec4(srgbToLinear(clamp(rgb, 0.0, 1.0)), 1.0);
}
