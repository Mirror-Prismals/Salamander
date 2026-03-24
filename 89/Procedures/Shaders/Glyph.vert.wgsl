@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> @builtin(position) vec4<f32> {
    let p = vec2<f32>(
        f32((vertexIndex << 1u) & 2u),
        f32(vertexIndex & 2u)
    );
    return vec4<f32>(p * 2.0 - 1.0, 0.0, 1.0);
}
