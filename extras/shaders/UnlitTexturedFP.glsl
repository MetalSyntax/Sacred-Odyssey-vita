precision mediump float;
uniform sampler2D texture;
uniform vec4 DiffuseColor;
varying mediump vec2 vTexCoord0;
varying lowp vec4 vColor0;

void main(void)
{
    vec4 color = texture2D(texture, vTexCoord0);
    vec4 vc = (vColor0.a > 0.01 && (vColor0.r > 0.01 || vColor0.g > 0.01 || vColor0.b > 0.01)) ? vColor0 : vec4(1.0);
    color *= vc;
    gl_FragColor = color;
}
