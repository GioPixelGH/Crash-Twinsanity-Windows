// The CRT filter (the graphic options' scanlines, NativeGraphicsSetPostFilter(1)): the picture as a period PAL television showed it,
// kept subtle. The presenter (graphics/presenter.h) compiles this fragment shader in place of its plain one: `place` the picture's
// coordinates (0-1, top row first), `picture` the PS2's picture, `pictureSize` its pixels, `viewSize` the pixels it covers
//
// What it does, each a little: the beam's horizontal softness (a short blur across), a dark gap between the picture's lines (each of
// the picture's rows a scanline, brighter in the middle, the gap's depth less where the line is bright, as a beam blooms), an aperture
// grille's faint vertical stripes at the screen's own pixels, the brightness the gaps take given back, and the tube's corners a touch
// darker
#include "common.h"

const char* NativeUiCrtShaderSource()
{
    return R"(#version 330 core
in vec2 place;
out vec4 colour;
uniform sampler2D picture;
uniform vec2 pictureSize;
uniform vec2 viewSize;
uniform float time;

vec3 Sample(vec2 at)
{
    return texture(picture, at).rgb;
}

void main()
{
    vec2 texel = 1.0 / pictureSize;
    // The beam's softness across the line
    vec3 beam = Sample(place) * 0.5 + Sample(place - vec2(texel.x, 0.0)) * 0.25 + Sample(place + vec2(texel.x, 0.0)) * 0.25;
    // The scanline: each of the picture's rows, darker between them; bright lines bloom over the gap
    float row = fract(place.y * pictureSize.y);
    float brightness = max(max(beam.r, beam.g), beam.b);
    float depth = mix(0.35, 0.12, brightness);
    float line = 1.0 - depth * pow(abs(row - 0.5) * 2.0, 2.0);
    // The grille's stripes, at the window's own pixels when they're fine enough to be stripes
    float column = mod(floor(place.x * viewSize.x), 3.0);
    vec3 grille = viewSize.x >= pictureSize.x * 2.0 ? (column == 0.0 ? vec3(1.0, 0.94, 0.94)
                                                     : column == 1.0 ? vec3(0.94, 1.0, 0.94) : vec3(0.94, 0.94, 1.0))
                                                   : vec3(1.0);
    // The gaps' light given back, the corners a touch darker
    vec2 centred = place * 2.0 - 1.0;
    float vignette = 1.0 - 0.08 * dot(centred * centred, vec2(0.5, 0.5));
    colour = vec4(clamp(beam * line * grille * 1.12 * vignette, 0.0, 1.0), 1.0);
}
)";
}
