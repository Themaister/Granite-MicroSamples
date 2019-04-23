#version 450

layout(set = 0, binding = 0) uniform Offset
{
    vec2 offset;
    vec2 scale;
};

layout(location = 0) in vec4 Position;
layout(location = 1) in vec4 Color;
layout(location = 0) out vec4 vColor;

void main()
{
    gl_Position = vec4(Position.xy * scale + offset, Position.zw);
    vColor = Color;
}