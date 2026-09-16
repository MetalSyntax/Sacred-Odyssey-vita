precision mediump float;
uniform vec4 DiffuseColor;

void main(void)
{
    gl_FragColor = (DiffuseColor.a > 0.01) ? DiffuseColor : vec4(1.0, 1.0, 1.0, 1.0);
}
