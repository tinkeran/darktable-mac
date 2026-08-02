"""
Prototype heuristic for locating a "sky-like" seed point in an image,
to be fed as a SAM2 point prompt (label=1) instead of a user click.

Score per pixel (downsampled grid) combines:
  - blueness/brightness (sky tends to be bright, blue-ish or white/grey near horizon)
  - local smoothness (low gradient magnitude -- sky lacks high-frequency texture)
  - vertical position prior (favor upper rows, since sky is typically near the top)

Then: threshold the score map, keep the largest connected component, seed point
is a robust interior point of that component (not just the argmax pixel, which
can land on a single noisy outlier).
"""
import numpy as np
from PIL import Image
from scipy import ndimage


def sky_candidate_points(rgb: np.ndarray, n_points: int = 3, stride: int = 8):
    h, w, _ = rgb.shape
    r = rgb[..., 0].astype(np.float32)
    g = rgb[..., 1].astype(np.float32)
    b = rgb[..., 2].astype(np.float32)

    brightness = (r + g + b) / 3.0
    blueness = b - (r + g) / 2.0  # positive when blue-dominant (sky), near 0 for white clouds
    whiteness = 255.0 - (np.max(rgb, axis=-1).astype(np.float32) - np.min(rgb, axis=-1).astype(np.float32))  # low saturation -> high

    # local smoothness via gradient magnitude on a blurred luminance
    gy, gx = np.gradient(ndimage.uniform_filter(brightness, size=5))
    grad_mag = np.sqrt(gx**2 + gy**2)
    smoothness = 1.0 / (1.0 + grad_mag)  # in (0,1], higher = smoother

    yy = np.arange(h).reshape(-1, 1).repeat(w, axis=1).astype(np.float32)
    vertical_prior = 1.0 - (yy / h)  # 1 at top, 0 at bottom

    # normalize each term to [0,1]
    def norm(a):
        lo, hi = np.percentile(a, 2), np.percentile(a, 98)
        return np.clip((a - lo) / max(hi - lo, 1e-6), 0, 1)

    score = (
        0.30 * norm(blueness)
        + 0.20 * norm(whiteness)
        + 0.20 * norm(brightness)
        + 0.15 * norm(smoothness)
        + 0.15 * vertical_prior
    )

    # downsample for connected-component search (cheap + more robust to noise)
    score_small = score[::stride, ::stride]
    thresh = np.percentile(score_small, 85)
    mask = score_small >= thresh

    labeled, n = ndimage.label(mask)
    if n == 0:
        # fallback: top-center point
        return [(w / 2, h * 0.15)], score, mask

    sizes = ndimage.sum(mask, labeled, range(1, n + 1))
    biggest = np.argmax(sizes) + 1
    ys, xs = np.where(labeled == biggest)

    # pick n_points spread within the component: centroid + a couple of
    # extremes along the horizontal spread, all scaled back to full res
    cy, cx = ys.mean(), xs.mean()
    pts = [(cx * stride, cy * stride)]
    if n_points > 1 and len(xs) > 4:
        order = np.argsort(xs)
        left = order[len(order) // 8]
        right = order[-len(order) // 8 - 1]
        pts.append((xs[left] * stride, ys[left] * stride))
        pts.append((xs[right] * stride, ys[right] * stride))

    return pts[:n_points], score, mask


if __name__ == "__main__":
    img = Image.open("/private/tmp/claude-501/-Users-rajubhupatiraju-Documents/01af1be7-fa17-4f71-bb7b-a70666f9e026/scratchpad/sky-heuristic/synthetic_landscape.png").convert("RGB")
    rgb = np.array(img)
    pts, score, mask = sky_candidate_points(rgb)
    print("image size:", img.size)
    print("candidate points (x,y):", pts)

    # visualize
    from PIL import ImageDraw
    vis = img.copy()
    d = ImageDraw.Draw(vis)
    for (x, y) in pts:
        d.ellipse([x - 10, y - 10, x + 10, y + 10], outline=(255, 0, 0), width=4)
    vis.save("/private/tmp/claude-501/-Users-rajubhupatiraju-Documents/01af1be7-fa17-4f71-bb7b-a70666f9e026/scratchpad/sky-heuristic/heuristic_points.png")

    score_img = Image.fromarray((score * 255).astype(np.uint8))
    score_img.save("/private/tmp/claude-501/-Users-rajubhupatiraju-Documents/01af1be7-fa17-4f71-bb7b-a70666f9e026/scratchpad/sky-heuristic/score_map.png")
    print("saved heuristic_points.png and score_map.png")
