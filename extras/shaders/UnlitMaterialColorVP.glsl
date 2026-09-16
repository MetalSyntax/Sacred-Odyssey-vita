attribute highp vec4 Position;
uniform highp mat4 WorldViewProjectionMatrix;

void main(void)
{
    gl_Position = WorldViewProjectionMatrix * Position;
}
