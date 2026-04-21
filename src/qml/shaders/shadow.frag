varying highp vec2 qt_TexCoord0;
uniform lowp float qt_Opacity;
uniform lowp sampler2D source;
uniform lowp vec4 color;
uniform highp float offset;

void main() {
    lowp vec4 tex = texture2D(source, qt_TexCoord0);
    lowp float shadow = texture2D(source, qt_TexCoord0 - vec2(offset)).a;
    gl_FragColor = (tex + color * shadow * (1.0 - tex.a)) * qt_Opacity;
}