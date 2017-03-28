#version 310 es

uniform sampler2D uTexture;
uniform sampler2D uTex2D0;
uniform vec4 uVec4;
in vec4 in_position;
out vec3 vPosition;

void main()
{
    vPosition = in_position.xyz + texture(uTexture, in_position.xy).xyz + uVec4.xyz + texture(uTex2D0, in_position.xy).xyz;
}

