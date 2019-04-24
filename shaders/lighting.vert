#version 450

const vec2 positions[3] = vec2[](vec2(-10.0, -10.0), vec2(-10.0, +10.0), vec2(+30.0, +10.0));

void main()
{
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}