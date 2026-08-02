/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "common/ai/segmentation.h"
#include <stdint.h>

/**
 * @brief Maximum number of seed points dt_sky_detect_points() can return.
 */
#define DT_SKY_DETECT_MAX_POINTS 3

/**
 * @brief Heuristically locate candidate "sky" seed points in an RGB image,
 *        for use as SAM2 foreground point prompts (dt_seg_compute_mask())
 *        when no manual click is available -- i.e. automatic sky detection
 *        layered on top of the existing interactive object-mask decoder,
 *        with no dedicated sky segmentation model.
 *
 *        Scores a downsampled grid of the image on: blueness, whiteness
 *        (low saturation -- catches white/grey clouds and hazy sky),
 *        brightness, local smoothness (sky lacks the high-frequency
 *        texture of foliage/rock/buildings), and a vertical-position
 *        prior (sky is typically near the top of frame). Thresholds the
 *        combined score, keeps the largest connected component (to avoid
 *        seeding on an isolated bright/blue outlier), and returns the
 *        component's centroid plus up to two points spread along its
 *        horizontal extent (multiple foreground points make the SAM2
 *        decoder more robust than a single point).
 *
 * @param rgb RGB uint8 image data (HWC layout, 3 channels).
 * @param width Image width in pixels.
 * @param height Image height in pixels.
 * @param out_points Caller-allocated array of at least
 *        DT_SKY_DETECT_MAX_POINTS dt_seg_point_t; all label=1 (foreground)
 *        on return.
 * @return Number of points written to out_points (0 if no plausible sky
 *         region was found -- e.g. indoor shots, no clear horizon --
 *         callers should treat this as "no automatic sky detected" and
 *         fall back to manual selection, not an error).
 */
int dt_sky_detect_points(const uint8_t *rgb,
                         const int width,
                         const int height,
                         dt_seg_point_t *out_points);

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
