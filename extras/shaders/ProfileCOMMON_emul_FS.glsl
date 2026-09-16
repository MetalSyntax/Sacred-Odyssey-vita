precision mediump float;

#if defined(TEXTURED)
uniform sampler2D Sampler0;
uniform sampler2D texture;
varying mediump vec2 vTexCoord0;
#endif

#if defined(MULTITEXTURED)
uniform sampler2D Sampler1;
#endif

#if defined(LIGHTMAP)
uniform sampler2D Sampler2;
varying mediump vec2 vTexCoord1;
#endif

uniform vec4 DiffuseColor;

#if defined(LIGHTING)
uniform vec4 ambientcolor;
uniform vec4 specularcolor;
uniform vec4 emissioncolor;
uniform float shininess;
#endif

void main(void)
{
#if defined(TEXTURED)
    vec4 color = texture2D(Sampler0, vTexCoord0);
#if defined(MULTITEXTURED)
    vec4 tex1 = texture2D(Sampler1, vTexCoord0);
    color *= tex1;
#endif
#if defined(LIGHTMAP)
    vec4 light = texture2D(Sampler2, vTexCoord1);
    color.rgb *= light.rgb * 2.0;
#endif
#else
    vec4 color = vec4(1.0, 1.0, 1.0, 1.0);
#endif
    if (DiffuseColor.a > 0.01 && (DiffuseColor.r > 0.01 || DiffuseColor.g > 0.01 || DiffuseColor.b > 0.01)) {
        color.rgb *= DiffuseColor.rgb;
    }
    gl_FragColor = color;
}

