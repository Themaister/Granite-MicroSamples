#version 450

layout(set = 0, binding = 0, input_attachment_index = 0) uniform subpassInput uMRT0;
layout(set = 0, binding = 1, input_attachment_index = 1) uniform subpassInput uMRT1;
layout(set = 0, binding = 2, input_attachment_index = 2) uniform subpassInput uDepth;
layout(location = 0) out vec4 FragColor;

void main()
{
    FragColor = subpassLoad(uDepth).x * (subpassLoad(uMRT0) + subpassLoad(uMRT1));
}
