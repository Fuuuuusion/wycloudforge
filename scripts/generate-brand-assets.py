"""Generate deterministic FuSinplayer branding assets from the supplied artwork.

This script intentionally performs only extraction, trimming and resampling.  It does
not redraw or reinterpret the user's mark.
"""

from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path

import numpy as np
from PIL import Image


def largest_component(mask: np.ndarray) -> np.ndarray:
    height, width = mask.shape
    seen = np.zeros_like(mask, dtype=bool)
    best: list[tuple[int, int]] = []
    for y in range(height):
        for x in range(width):
            if not mask[y, x] or seen[y, x]:
                continue
            queue = deque([(y, x)])
            seen[y, x] = True
            points: list[tuple[int, int]] = []
            while queue:
                cy, cx = queue.popleft()
                points.append((cy, cx))
                for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    ny, nx = cy + dy, cx + dx
                    if (0 <= ny < height and 0 <= nx < width
                            and mask[ny, nx] and not seen[ny, nx]):
                        seen[ny, nx] = True
                        queue.append((ny, nx))
            if len(points) > len(best):
                best = points
    result = np.zeros_like(mask, dtype=bool)
    for y, x in best:
        result[y, x] = True
    return result


def trim_alpha(image: Image.Image, padding: int = 0) -> Image.Image:
    alpha = image.getchannel("A")
    bbox = alpha.getbbox()
    if not bbox:
        raise ValueError("input image has no visible pixels")
    left, top, right, bottom = bbox
    left = max(0, left - padding)
    top = max(0, top - padding)
    right = min(image.width, right + padding)
    bottom = min(image.height, bottom + padding)
    return image.crop((left, top, right, bottom))


def contain(image: Image.Image, size: int, padding_ratio: float) -> Image.Image:
    content = trim_alpha(image)
    usable = max(1, round(size * (1.0 - padding_ratio * 2.0)))
    scale = min(usable / content.width, usable / content.height)
    target = (max(1, round(content.width * scale)), max(1, round(content.height * scale)))
    content = content.resize(target, Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.alpha_composite(content, ((size - target[0]) // 2, (size - target[1]) // 2))
    return canvas


def extract_logo(source: Path) -> Image.Image:
    image = Image.open(source).convert("RGBA")
    rgb = np.asarray(image, dtype=np.uint8)[..., :3]
    red = rgb[..., 0].astype(np.int16)
    other = np.maximum(rgb[..., 1], rgb[..., 2]).astype(np.int16)
    candidate = (red > 150) & ((red - other) > 55)

    # The outer guide rectangle is intentionally excluded.  The supplied central
    # symbol is the largest contiguous red region, so its original geometry remains
    # untouched.
    component = largest_component(candidate)
    ys, xs = np.nonzero(component)
    if len(xs) < 100:
        raise ValueError("could not locate the central red logo component")
    left, top, right, bottom = xs.min(), ys.min(), xs.max() + 1, ys.max() + 1
    margin = 4
    left, top = max(0, left - margin), max(0, top - margin)
    right, bottom = min(image.width, right + margin), min(image.height, bottom + margin)

    crop = np.asarray(image.crop((left, top, right, bottom)), dtype=np.uint8).copy()
    # int32 avoids overflow while converting the red dominance score to alpha.
    crop_red = crop[..., 0].astype(np.int32)
    crop_other = np.maximum(crop[..., 1], crop[..., 2]).astype(np.int32)
    alpha = np.clip((crop_red - crop_other - 8) * 255 / 165, 0, 255).astype(np.uint8)
    alpha[crop_red < 90] = 0
    crop[..., 3] = alpha
    crop[..., :3] = np.array([255, 27, 36], dtype=np.uint8)
    return trim_alpha(Image.fromarray(crop, "RGBA"), 2)


def make_mask(source: Path, width: int = 128, height: int = 48) -> Image.Image:
    image = Image.open(source).convert("RGBA")
    alpha = image.getchannel("A")
    if alpha.getextrema() == (255, 255):
        rgb = np.asarray(image, dtype=np.uint8)[..., :3]
        darkness = 255 - np.min(rgb, axis=2)
        alpha = Image.fromarray(darkness.astype(np.uint8), "L")
        image.putalpha(alpha)
    content = trim_alpha(image)
    scale = min(width / content.width, height / content.height)
    target = (max(1, round(content.width * scale)), max(1, round(content.height * scale)))
    content = content.resize(target, Image.Resampling.LANCZOS)
    result = Image.new("RGBA", (width, height), (255, 255, 255, 0))
    mask = content.getchannel("A")
    white = Image.new("RGBA", target, (255, 255, 255, 255))
    white.putalpha(mask)
    result.alpha_composite(white, ((width - target[0]) // 2, (height - target[1]) // 2))
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--logo", type=Path, required=True)
    parser.add_argument("--account-vip", type=Path, required=True)
    parser.add_argument("--song-vip", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    logo = contain(extract_logo(args.logo), 512, 0.08)
    logo.save(args.output / "fusinplayer-logo.png", optimize=True)
    logo.save(args.output / "fusinplayer.ico", format="ICO",
              sizes=[(16, 16), (24, 24), (32, 32), (48, 48),
                     (64, 64), (128, 128), (256, 256)])
    make_mask(args.account_vip).save(args.output / "vip-account-mask.png", optimize=True)
    make_mask(args.song_vip).save(args.output / "vip-song-mask.png", optimize=True)


if __name__ == "__main__":
    main()
