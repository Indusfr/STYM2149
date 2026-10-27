// STYM2149 — pluginui: the Atari logo of the menu bar (file written by tools/icon/make_fuji_data.py, D-404)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Do not edit by hand: run `python3 tools/icon/make_fuji_data.py` again. The outline of the Atari logo of Simple Icons
// (CC0 1.0) fitted to 17 × 14 pixels; the bits are its raster at scale 1, a pixel lit when its centre lies inside.
#include "pluginui/IconData.h"

namespace stym::pluginui::icons {
namespace {

//   .....#.###.#.....
//   .....#.###.#.....
//   .....#.###.#.....
//   .....#.###.#.....
//   .....#.###.#.....
//   .....#.###.#.....
//   .....#.###.#.....
//   ....##.###.##....
//   ....##.###.##....
//   ...###.###.###...
//   ..###..###..###..
//   .###...###...###.
//   ###....###....###
//   ##.....###.....##
const uint8_t kFujiBits[42] = {
    0x05, 0xd0, 0x00,
    0x05, 0xd0, 0x00,
    0x05, 0xd0, 0x00,
    0x05, 0xd0, 0x00,
    0x05, 0xd0, 0x00,
    0x05, 0xd0, 0x00,
    0x05, 0xd0, 0x00,
    0x0d, 0xd8, 0x00,
    0x0d, 0xd8, 0x00,
    0x1d, 0xdc, 0x00,
    0x39, 0xce, 0x00,
    0x71, 0xc7, 0x00,
    0xe1, 0xc3, 0x80,
    0xc1, 0xc1, 0x80,
};

// Left prong, centre bar, right prong: x, y of each vertex.
const float kFujiXy[204] = {
    0.000f, 14.000f, 0.460f, 14.000f, 0.527f, 13.981f, 1.027f, 13.805f,
    1.565f, 13.578f, 2.122f, 13.295f, 2.683f, 12.949f, 3.230f, 12.534f,
    3.707f, 12.111f, 4.125f, 11.697f, 4.488f, 11.292f, 4.803f, 10.891f,
    5.074f, 10.494f, 5.306f, 10.097f, 5.506f, 9.700f, 5.679f, 9.299f,
    5.830f, 8.892f, 5.963f, 8.478f, 6.080f, 8.005f, 6.177f, 7.440f,
    6.255f, 6.809f, 6.318f, 6.138f, 6.366f, 5.453f, 6.401f, 4.780f,
    6.425f, 4.145f, 6.440f, 3.574f, 6.448f, 3.093f, 6.450f, 2.728f,
    6.450f, 0.000f, 5.395f, 0.000f, 5.395f, 1.011f, 5.386f, 1.503f,
    5.368f, 2.062f, 5.338f, 2.675f, 5.293f, 3.327f, 5.231f, 4.003f,
    5.150f, 4.691f, 5.045f, 5.375f, 4.916f, 6.041f, 4.759f, 6.676f,
    4.572f, 7.265f, 4.022f, 8.498f, 3.380f, 9.506f, 2.680f, 10.312f,
    1.959f, 10.939f, 1.252f, 11.407f, 0.593f, 11.741f, 0.019f, 11.961f,
    0.000f, 11.967f, 7.450f, 0.000f, 9.550f, 0.000f, 9.550f, 14.000f,
    7.450f, 14.000f, 17.000f, 11.967f, 16.981f, 11.961f, 16.407f, 11.741f,
    15.748f, 11.407f, 15.041f, 10.939f, 14.320f, 10.312f, 13.620f, 9.506f,
    12.978f, 8.498f, 12.428f, 7.265f, 12.241f, 6.676f, 12.084f, 6.041f,
    11.955f, 5.375f, 11.850f, 4.691f, 11.769f, 4.003f, 11.707f, 3.327f,
    11.662f, 2.675f, 11.632f, 2.062f, 11.614f, 1.503f, 11.605f, 1.011f,
    11.605f, 0.000f, 10.550f, 0.000f, 10.550f, 2.728f, 10.552f, 3.093f,
    10.560f, 3.574f, 10.575f, 4.145f, 10.599f, 4.780f, 10.634f, 5.453f,
    10.682f, 6.138f, 10.745f, 6.809f, 10.823f, 7.440f, 10.920f, 8.005f,
    11.037f, 8.478f, 11.170f, 8.892f, 11.321f, 9.299f, 11.494f, 9.700f,
    11.694f, 10.097f, 11.926f, 10.494f, 12.197f, 10.891f, 12.512f, 11.292f,
    12.875f, 11.697f, 13.293f, 12.111f, 13.770f, 12.534f, 14.317f, 12.949f,
    14.878f, 13.295f, 15.435f, 13.578f, 15.973f, 13.805f, 16.473f, 13.981f,
    16.540f, 14.000f, 17.000f, 14.000f,
};
const uint16_t kFujiEnds[3] = {49, 53, 102};
const gemui::Outline kFujiOutline{kFujiXy, kFujiEnds};

}  // namespace

const gemui::Bitmap kFuji{17, 14, kFujiBits, &kFujiOutline};

}  // namespace stym::pluginui::icons
