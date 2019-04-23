#version 450
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 1) uniform ColorMod
{
    vec4 color_mod;
};

void main()
{
    FragColor = vColor * color_mod;
}