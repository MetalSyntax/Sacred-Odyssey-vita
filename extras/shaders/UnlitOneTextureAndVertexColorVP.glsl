attribute highp vec4 Position;
attribute lowp vec4 Color0;
attribute mediump vec2 TexCoord0;
uniform highp mat4 WorldViewProjectionMatrix;
uniform mediump mat4 TextureMatrix0;
varying mediump vec2 vTexCoord0;
varying lowp vec4 vColor0;

void main(void)
{
    gl_Position = WorldViewProjectionMatrix * Position;
    vColor0 = Color0;
    vTexCoord0 = (TextureMatrix0 * vec4(TexCoord0, 0.0, 1.0)).xy;
}
