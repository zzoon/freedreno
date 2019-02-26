precision mediump float;

uniform sampler2D uSamp1;
varying vec2 vTexCoord;

void main()
{
  gl_FragColor = texture2D(uSamp1, vTexCoord + vec2(0.1, 0.1));
}

