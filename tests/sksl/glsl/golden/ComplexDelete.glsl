
out vec4 sk_FragColor;
uniform mat4 colorXform;
uniform sampler2D sampler;
void main() {
    vec4 tmpColor;
    sk_FragColor = (tmpColor = texture(sampler, vec2(1.0)) , colorXform != mat4(1.0) ? vec4(clamp((colorXform * vec4(tmpColor.xyz, 1.0)).xyz, 0.0, tmpColor.w), tmpColor.w) : tmpColor);
}
