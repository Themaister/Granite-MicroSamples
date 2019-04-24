#version 450
layout(location = 0) out vec4 MRT0;
layout(location = 1) out vec4 MRT1;

void main()
{
    MRT0 = vec4(1.0, 0.0, 0.0, 1.0);
    MRT1 = vec4(0.0, 0.0, 1.0, 1.0);
}
