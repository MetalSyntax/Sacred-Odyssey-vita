attribute highp vec4 Position;
attribute lowp vec4 Color0;
uniform highp mat4 WorldViewProjectionMatrix;
varying lowp vec4 vColor0;

void main(void)
{
    gl_Position = WorldViewProjectionMatrix * Position;
    vColor0 = Color0;
}
