struct Uniforms {
    model: mat4x4<f32>,
    view: mat4x4<f32>,
    projection: mat4x4<f32>,
    mvp: mat4x4<f32>,
    color: vec4<f32>,
    topColor: vec4<f32>,
    bottomColor: vec4<f32>,
    params: vec4<f32>,
    vec2Data: vec4<f32>,
    extra: vec4<f32>,
    cameraAndScale: vec4<f32>,
    lightAndGrid: vec4<f32>,
    ambientAndLeaf: vec4<f32>,
    diffuseAndWater: vec4<f32>,
    atlasInfo: vec4<f32>,
    wallStoneAndWater2: vec4<f32>,
    intParams0: vec4<i32>,
    intParams1: vec4<i32>,
    intParams2: vec4<i32>,
    intParams3: vec4<i32>,
    intParams4: vec4<i32>,
    intParams5: vec4<i32>,
    intParams6: vec4<i32>,
    blockDamageCells: array<vec4<i32>, 64>,
    blockDamageProgress: array<vec4<f32>, 16>,
};

@group(0) @binding(0)
var<uniform> u: Uniforms;

struct VSIn {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) texCoord: vec2<f32>,
    @location(3) offset: vec3<f32>,
    @location(4) colorOrRotation: vec3<f32>,
    @location(5) colorBranch: vec3<f32>,
};

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) texCoord: vec2<f32>,
    @location(1) fragColor: vec3<f32>,
    @location(2) instanceDistance: f32,
    @location(3) normal: vec3<f32>,
    @location(4) worldPos: vec3<f32>,
    @location(5) instanceCell: vec3<f32>,
};

fn rotY(r: f32) -> mat3x3<f32> {
    let c = cos(r);
    let s = sin(r);
    return mat3x3<f32>(
        vec3<f32>(c, 0.0, s),
        vec3<f32>(0.0, 1.0, 0.0),
        vec3<f32>(-s, 0.0, c)
    );
}

@vertex
fn vs_main(input: VSIn) -> VSOut {
    var out: VSOut;

    let behaviorType = u.intParams0.x;
    let time = u.params.x;
    let cameraPos = u.cameraAndScale.xyz;
    let instanceScale = u.cameraAndScale.w;

    var pos = input.position;
    var normal = input.normal;
    let instancePos = input.offset;
    var instanceColor = input.colorOrRotation;
    var instanceRotation = 0.0;

    if (behaviorType == 3) {
        instanceRotation = input.colorOrRotation.x;
        instanceColor = input.colorBranch;
    }

    if (behaviorType == 1) {
        let d = vec3<f32>(
            sin(time * 0.5 + instancePos.x * 1.3 + instancePos.y * 0.7) * 0.2,
            sin(time * 0.5 + instancePos.y * 1.3 + instancePos.z * 0.7) * 0.2,
            sin(time * 0.5 + instancePos.z * 1.3 + instancePos.x * 0.7) * 0.2
        );
        pos = input.position * 1.2 + d;
    } else if (behaviorType == 2) {
        let d = vec3<f32>(
            sin((instancePos.x + time) * 0.3) * 0.05,
            cos((instancePos.y + time) * 0.3) * 0.05,
            sin((instancePos.z + time) * 0.3) * 0.05
        );
        pos = input.position + d;
    } else if (behaviorType == 3) {
        let r = rotY(radians(instanceRotation));
        let s = mat3x3<f32>(
            vec3<f32>(0.3, 0.0, 0.0),
            vec3<f32>(0.0, 0.8, 0.0),
            vec3<f32>(0.0, 0.0, 0.3)
        );
        pos = r * (s * input.position);
        normal = r * input.normal;
    }

    var finalPos = instancePos + (pos * instanceScale);
    if (behaviorType == 4) {
        finalPos.y = finalPos.y + sin(time + instancePos.x * 0.1) * 0.5;
    }

    let worldPos4 = u.model * vec4<f32>(finalPos, 1.0);
    out.worldPos = worldPos4.xyz;
    out.position = u.projection * u.view * worldPos4;

    out.fragColor = instanceColor;
    out.texCoord = input.texCoord;
    out.instanceDistance = length(instancePos - cameraPos);
    out.normal = normalize((u.model * vec4<f32>(normal, 0.0)).xyz);
    out.instanceCell = instancePos;
    return out;
}
