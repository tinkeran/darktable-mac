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

#include "common/ai/sky_detect.h"
#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// minimum connected-component size (in grid cells) to accept as "sky" --
// guards against seeding on a single bright/blue outlier cell
#define MIN_COMPONENT_CELLS 4

// weights for the combined per-cell score; blueness/whiteness/brightness
// together outweigh smoothness/position since colour is the strongest
// sky signal, texture and position are supporting evidence
#define W_BLUENESS 0.30f
#define W_WHITENESS 0.20f
#define W_BRIGHTNESS 0.20f
#define W_SMOOTHNESS 0.15f
#define W_VERTICAL 0.15f

#define SCORE_PERCENTILE_LO 0.02f
#define SCORE_PERCENTILE_HI 0.98f
#define SCORE_THRESHOLD_PERCENTILE 0.85f

typedef struct
{
  float brightness, blueness, whiteness;
} _cell_rgb_t;

static int _cmp_float(const void *a, const void *b)
{
  const float fa = *(const float *)a;
  const float fb = *(const float *)b;
  return (fa > fb) - (fa < fb);
}

// normalize `values` (n elements) in place to [0,1] via percentile clipping,
// using `scratch` (n elements) as sort scratch space
static void _normalize_percentile(float *values, float *scratch, const int n)
{
  if(n <= 0) return;
  memcpy(scratch, values, sizeof(float) * n);
  qsort(scratch, n, sizeof(float), _cmp_float);

  const int lo_idx = CLAMP((int)(SCORE_PERCENTILE_LO * (n - 1)), 0, n - 1);
  const int hi_idx = CLAMP((int)(SCORE_PERCENTILE_HI * (n - 1)), 0, n - 1);
  const float lo = scratch[lo_idx];
  const float hi = scratch[hi_idx];
  const float range = fmaxf(hi - lo, 1e-6f);

  for(int i = 0; i < n; i++)
    values[i] = CLAMP((values[i] - lo) / range, 0.0f, 1.0f);
}

// largest-component connected labeling over a boolean grid (4-connectivity),
// returns the id (>=1) of the biggest component in `labels`, or 0 if none
static int _label_largest_component(const gboolean *mask,
                                    int *labels,
                                    const int gw,
                                    const int gh)
{
  memset(labels, 0, sizeof(int) * (size_t)gw * gh);
  int *stack = g_malloc(sizeof(int) * (size_t)gw * gh);
  int *comp_size = NULL;
  int n_comp = 0;
  int comp_cap = 0;

  int next_label = 0;
  for(int start = 0; start < gw * gh; start++)
  {
    if(!mask[start] || labels[start] != 0) continue;

    next_label++;
    if(next_label > comp_cap)
    {
      comp_cap = next_label + 16;
      comp_size = g_realloc(comp_size, sizeof(int) * comp_cap);
    }
    comp_size[next_label - 1] = 0;
    n_comp = next_label;

    int sp = 0;
    stack[sp++] = start;
    labels[start] = next_label;

    while(sp > 0)
    {
      const int idx = stack[--sp];
      comp_size[next_label - 1]++;
      const int x = idx % gw;
      const int y = idx / gw;

      const int nx[4] = { x - 1, x + 1, x, x };
      const int ny[4] = { y, y, y - 1, y + 1 };
      for(int k = 0; k < 4; k++)
      {
        if(nx[k] < 0 || nx[k] >= gw || ny[k] < 0 || ny[k] >= gh) continue;
        const int nidx = ny[k] * gw + nx[k];
        if(mask[nidx] && labels[nidx] == 0)
        {
          labels[nidx] = next_label;
          stack[sp++] = nidx;
        }
      }
    }
  }
  g_free(stack);

  int best = 0, best_size = 0;
  for(int i = 0; i < n_comp; i++)
    if(comp_size[i] > best_size) { best_size = comp_size[i]; best = i + 1; }
  g_free(comp_size);

  return (best_size >= MIN_COMPONENT_CELLS) ? best : 0;
}

int dt_sky_detect_points(const uint8_t *rgb,
                         const int width,
                         const int height,
                         dt_seg_point_t *out_points)
{
  if(!rgb || width <= 0 || height <= 0 || !out_points)
    return 0;

  const int stride = CLAMP(MIN(width, height) / 128, 4, 64);
  const int gw = MAX(1, width / stride);
  const int gh = MAX(1, height / stride);
  const int n_cells = gw * gh;

  _cell_rgb_t *cells = g_malloc(sizeof(_cell_rgb_t) * n_cells);

  // block-average each grid cell -- doubles as a smoothing pre-filter
  for(int gy = 0; gy < gh; gy++)
  {
    for(int gx = 0; gx < gw; gx++)
    {
      const int x0 = gx * stride, y0 = gy * stride;
      const int x1 = MIN(x0 + stride, width), y1 = MIN(y0 + stride, height);
      double sr = 0.0, sg = 0.0, sb = 0.0;
      int n = 0;
      for(int y = y0; y < y1; y++)
      {
        const uint8_t *row = rgb + (size_t)y * width * 3;
        for(int x = x0; x < x1; x++)
        {
          sr += row[x * 3 + 0];
          sg += row[x * 3 + 1];
          sb += row[x * 3 + 2];
          n++;
        }
      }
      if(n == 0) n = 1;
      const float r = (float)(sr / n), g = (float)(sg / n), b = (float)(sb / n);
      _cell_rgb_t *c = &cells[gy * gw + gx];
      c->brightness = (r + g + b) / 3.0f;
      c->blueness = b - (r + g) / 2.0f;
      const float cmax = fmaxf(r, fmaxf(g, b));
      const float cmin = fminf(r, fminf(g, b));
      c->whiteness = 255.0f - (cmax - cmin);
    }
  }

  float *brightness = g_malloc(sizeof(float) * n_cells);
  float *blueness = g_malloc(sizeof(float) * n_cells);
  float *whiteness = g_malloc(sizeof(float) * n_cells);
  float *smoothness = g_malloc(sizeof(float) * n_cells);
  float *vertical = g_malloc(sizeof(float) * n_cells);
  float *score = g_malloc(sizeof(float) * n_cells);
  float *scratch = g_malloc(sizeof(float) * n_cells);

  for(int gy = 0; gy < gh; gy++)
  {
    for(int gx = 0; gx < gw; gx++)
    {
      const int idx = gy * gw + gx;
      brightness[idx] = cells[idx].brightness;
      blueness[idx] = cells[idx].blueness;
      whiteness[idx] = cells[idx].whiteness;
      vertical[idx] = 1.0f - (float)gy / gh;

      // local gradient magnitude on the (already block-averaged) brightness
      // grid, as a smoothness proxy -- higher gradient = more texture
      const float b_l = cells[gy * gw + MAX(gx - 1, 0)].brightness;
      const float b_r = cells[gy * gw + MIN(gx + 1, gw - 1)].brightness;
      const float b_u = cells[MAX(gy - 1, 0) * gw + gx].brightness;
      const float b_d = cells[MIN(gy + 1, gh - 1) * gw + gx].brightness;
      const float gx_grad = (b_r - b_l) / 2.0f;
      const float gy_grad = (b_d - b_u) / 2.0f;
      const float grad_mag = sqrtf(gx_grad * gx_grad + gy_grad * gy_grad);
      smoothness[idx] = 1.0f / (1.0f + grad_mag);
    }
  }
  g_free(cells);

  _normalize_percentile(brightness, scratch, n_cells);
  _normalize_percentile(blueness, scratch, n_cells);
  _normalize_percentile(whiteness, scratch, n_cells);
  _normalize_percentile(smoothness, scratch, n_cells);
  // vertical prior is already in [0,1] by construction, no normalization needed

  for(int i = 0; i < n_cells; i++)
    score[i] = W_BLUENESS * blueness[i]
             + W_WHITENESS * whiteness[i]
             + W_BRIGHTNESS * brightness[i]
             + W_SMOOTHNESS * smoothness[i]
             + W_VERTICAL * vertical[i];

  memcpy(scratch, score, sizeof(float) * n_cells);
  qsort(scratch, n_cells, sizeof(float), _cmp_float);
  const int thresh_idx = CLAMP((int)(SCORE_THRESHOLD_PERCENTILE * (n_cells - 1)),
                               0, n_cells - 1);
  const float threshold = scratch[thresh_idx];

  g_free(brightness);
  g_free(blueness);
  g_free(whiteness);
  g_free(smoothness);
  g_free(vertical);
  g_free(scratch);

  gboolean *mask = g_malloc(sizeof(gboolean) * n_cells);
  for(int i = 0; i < n_cells; i++)
    mask[i] = score[i] >= threshold;

  int *labels = g_malloc(sizeof(int) * n_cells);
  const int best_label = _label_largest_component(mask, labels, gw, gh);
  g_free(mask);

  int n_points = 0;
  if(best_label > 0)
  {
    // gather component cell coords, compute centroid and (if large enough)
    // two spread points near the horizontal extremes of the component
    int *cx = g_malloc(sizeof(int) * n_cells);
    int *cy = g_malloc(sizeof(int) * n_cells);
    int n_member = 0;
    double sum_x = 0.0, sum_y = 0.0;
    for(int i = 0; i < n_cells; i++)
    {
      if(labels[i] != best_label) continue;
      const int x = i % gw, y = i / gw;
      cx[n_member] = x;
      cy[n_member] = y;
      sum_x += x;
      sum_y += y;
      n_member++;
    }

    const float centroid_x = (float)(sum_x / n_member) * stride + stride / 2.0f;
    const float centroid_y = (float)(sum_y / n_member) * stride + stride / 2.0f;
    out_points[n_points++] = (dt_seg_point_t){ centroid_x, centroid_y, 1 };

    if(n_member > 8)
    {
      // sort component members by x to pick left/right spread points,
      // near the ~1/8 and ~7/8 index (mirrors the validated Python prototype)
      for(int i = 0; i < n_member - 1; i++)
        for(int j = 0; j < n_member - 1 - i; j++)
          if(cx[j] > cx[j + 1])
          {
            const int tx = cx[j]; cx[j] = cx[j + 1]; cx[j + 1] = tx;
            const int ty = cy[j]; cy[j] = cy[j + 1]; cy[j + 1] = ty;
          }
      const int left = n_member / 8;
      const int right = n_member - n_member / 8 - 1;
      out_points[n_points++] = (dt_seg_point_t){
        cx[left] * stride + stride / 2.0f, cy[left] * stride + stride / 2.0f, 1 };
      out_points[n_points++] = (dt_seg_point_t){
        cx[right] * stride + stride / 2.0f, cy[right] * stride + stride / 2.0f, 1 };
    }

    g_free(cx);
    g_free(cy);
  }

  g_free(score);
  g_free(labels);

  return n_points;
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
