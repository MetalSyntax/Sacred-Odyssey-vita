attribute highp vec4 Position;
#if defined(TEXTURED)
attribute mediump vec2 TexCoord0;
varying mediump vec2 vTexCoord0;
uniform mediump mat4 TextureMatrix0;
#endif
#if defined(LIGHTMAP)
attribute mediump vec2 TexCoord1;
varying mediump vec2 vTexCoord1;
uniform mediump mat4 TextureMatrix2;
#endif
attribute lowp vec4 Color0;
varying lowp vec4 vColor0;
uniform highp mat4 WorldViewProjectionMatrix;

void main(void)
{
    gl_Position = WorldViewProjectionMatrix * Position;
#if defined(TEXTURED)
    vTexCoord0 = (TextureMatrix0 * vec4(TexCoord0, 0.0, 1.0)).xy;
#endif
#if defined(LIGHTMAP)
    vTexCoord1 = (TextureMatrix2 * vec4(TexCoord1, 0.0, 1.0)).xy;
#endif
    vColor0 = Color0;
}

