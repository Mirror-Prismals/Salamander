#version 330 core
out vec4 FragColor;
uniform vec2 uResolution;
uniform vec2 uCenter;
uniform vec2 uButtonSize;
uniform float uPressOffset;
uniform int uType;
uniform vec3 uColor;

float tri(vec2 p, vec2 c, float s, float dir) {
    float h = s * 0.86602540378;
    vec2 A = c + vec2(-dir * h / 3.0,  s / 2.0);
    vec2 B = c + vec2(-dir * h / 3.0, -s / 2.0);
    vec2 C = c + vec2( dir * 2.0 * h / 3.0, 0.0);
    vec2 v0 = B - A, v1 = C - A, v2 = p - A;
    float d00 = dot(v0,v0), d01 = dot(v0,v1), d11 = dot(v1,v1);
    float d20 = dot(v2,v0), d21 = dot(v2,v1);
    float denom = d00 * d11 - d01 * d01;
    float v = (d11 * d20 - d01 * d21) / denom;
    float w = (d00 * d21 - d01 * d20) / denom;
    float u = 1.0 - v - w;
    return step(0.0, min(u, min(v, w)));
}

float squareMask(vec2 p, vec2 c, float s) {
    vec2 d = abs(p - c);
    return step(max(d.x, d.y), s * 0.5);
}

float circleMask(vec2 p, vec2 c, float r) {
    return step(length(p - c), r);
}

float bar(vec2 p, vec2 c, vec2 size) {
    vec2 d = abs(p - c);
    return step(max(d.x - size.x, d.y - size.y), 0.0);
}

void main() {
    vec2 p = gl_FragCoord.xy;
    // Button positions are authored in top-left origin space; convert to GL bottom-left.
    // Apply press offset with full left shift and half vertical shift.
    vec2 pressVec = vec2(-uPressOffset, uPressOffset * 0.5);
    vec2 center = vec2(uCenter.x, uResolution.y - uCenter.y) + pressVec;

    float base = min(uButtonSize.x, uButtonSize.y) * 0.8;
    float s = base * 0.85;

    float mask = 0.0;
    if (uType == 0) { // Stop
        mask = squareMask(p, center, s);
    } else if (uType == 1) { // Play
        mask = tri(p, center, s, 1.0);
    } else if (uType == 2) { // Record
        mask = circleMask(p, center, s * 0.5);
    } else if (uType == 3) { // Back
        float mTri = tri(p, center + vec2(s * 0.18, 0.0), s, -1.0);
        float mBar = bar(p, center + vec2(-s * 0.42, 0.0), vec2(s * 0.027, s * 0.5));
        mask = max(mTri, mBar);
    }

    if (mask < 0.5) discard;
    FragColor = vec4(uColor, 1.0);
}
