#!/usr/bin/env python3
"""Compare golden image artifacts for C/C++ rendering work.

The script is dependency-free for PPM/PGM files and uses Pillow when available
for PNG and other common image formats. It prints a compact Markdown evidence
packet and exits non-zero when the candidate differs beyond the threshold.
"""

from __future__ import annotations

import argparse
import math
import sys
from dataclasses import dataclass
from pathlib import Path


WHITESPACE = set(b" \t\r\n")
PNM_CHANNELS = {b"P5": 1, b"P6": 3}


@dataclass(frozen=True)
class ImageData:
    path: Path
    width: int
    height: int
    channels: int
    mode: str
    pixels: bytes
    loader: str


@dataclass(frozen=True)
class DiffResult:
    passed: bool
    compared_pixels: int
    different_pixels: int
    different_channels: int
    max_channel_delta: int
    mean_absolute_delta: float
    rmse: float
    psnr: float | None
    reason: str


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare two image artifacts and print a Markdown pixel-diff report.",
    )
    parser.add_argument("baseline", type=Path, help="baseline golden image")
    parser.add_argument("candidate", type=Path, help="candidate image")
    parser.add_argument(
        "--threshold",
        type=int,
        default=0,
        help="allowed per-channel absolute delta, 0-255 (default: 0)",
    )
    return parser.parse_args(argv)


def next_token(data: bytes, index: int) -> tuple[bytes, int]:
    while index < len(data):
        byte = data[index]
        if byte in WHITESPACE:
            index += 1
            continue
        if byte == ord("#"):
            while index < len(data) and data[index] not in b"\r\n":
                index += 1
            continue
        break

    start = index
    while index < len(data) and data[index] not in WHITESPACE and data[index] != ord("#"):
        index += 1

    if start == index:
        raise ValueError("unexpected end of PNM header")
    return data[start:index], index


def pixel_start(data: bytes, index: int) -> int:
    while index < len(data):
        byte = data[index]
        if byte in WHITESPACE:
            index += 1
            continue
        if byte == ord("#"):
            while index < len(data) and data[index] not in b"\r\n":
                index += 1
            continue
        break
    return index


def load_pnm(path: Path) -> ImageData:
    data = path.read_bytes()
    index = 0
    magic, index = next_token(data, index)
    if magic not in PNM_CHANNELS:
        raise ValueError("not a binary PGM/PPM file")

    width_token, index = next_token(data, index)
    height_token, index = next_token(data, index)
    maxval_token, index = next_token(data, index)
    width = int(width_token)
    height = int(height_token)
    maxval = int(maxval_token)
    if width <= 0 or height <= 0:
        raise ValueError("PNM dimensions must be positive")
    if maxval != 255:
        raise ValueError("only 8-bit PNM maxval=255 is supported")

    channels = PNM_CHANNELS[magic]
    start = pixel_start(data, index)
    expected = width * height * channels
    pixels = data[start : start + expected]
    if len(pixels) != expected:
        raise ValueError(f"PNM pixel data truncated: expected {expected}, got {len(pixels)}")

    mode = "RGB" if channels == 3 else "L"
    return ImageData(path, width, height, channels, mode, pixels, "pnm")


def load_with_pillow(path: Path) -> ImageData:
    try:
        from PIL import Image
    except Exception as exc:  # pragma: no cover - depends on local optional dependency
        raise ValueError("Pillow is unavailable and the file is not supported by the PNM fallback") from exc

    with Image.open(path) as image:
        if image.mode == "L":
            converted = image.copy()
        elif "A" in image.getbands() or image.mode in {"P", "LA"}:
            converted = image.convert("RGBA")
        else:
            converted = image.convert("RGB")
        pixels = converted.tobytes()
        return ImageData(
            path=path,
            width=converted.width,
            height=converted.height,
            channels=len(converted.getbands()),
            mode=converted.mode,
            pixels=pixels,
            loader="pillow",
        )


def load_image(path: Path) -> ImageData:
    if not path.is_file():
        raise ValueError(f"not a file: {path}")
    try:
        return load_pnm(path)
    except Exception:
        return load_with_pillow(path)


def compare_images(baseline: ImageData, candidate: ImageData, threshold: int) -> DiffResult:
    if baseline.width != candidate.width or baseline.height != candidate.height:
        return DiffResult(False, 0, 0, 0, 0, 0.0, 0.0, None, "image dimensions differ")
    if baseline.channels != candidate.channels:
        return DiffResult(False, 0, 0, 0, 0, 0.0, 0.0, None, "channel counts differ")

    compared_pixels = baseline.width * baseline.height
    different_pixels = 0
    different_channels = 0
    max_delta = 0
    total_abs = 0
    total_sq = 0
    channels = baseline.channels

    for offset in range(0, len(baseline.pixels), channels):
        pixel_differs = False
        for channel in range(channels):
            delta = abs(baseline.pixels[offset + channel] - candidate.pixels[offset + channel])
            total_abs += delta
            total_sq += delta * delta
            if delta > max_delta:
                max_delta = delta
            if delta > threshold:
                different_channels += 1
                pixel_differs = True
        if pixel_differs:
            different_pixels += 1

    compared_channels = len(baseline.pixels)
    mean_absolute_delta = total_abs / compared_channels if compared_channels else 0.0
    rmse = math.sqrt(total_sq / compared_channels) if compared_channels else 0.0
    psnr = None if rmse == 0.0 else 20.0 * math.log10(255.0 / rmse)
    passed = different_pixels == 0
    reason = "within threshold" if passed else "pixel deltas exceed threshold"
    return DiffResult(
        passed,
        compared_pixels,
        different_pixels,
        different_channels,
        max_delta,
        mean_absolute_delta,
        rmse,
        psnr,
        reason,
    )


def print_report(baseline: ImageData, candidate: ImageData, threshold: int, result: DiffResult) -> None:
    psnr_text = "infinite" if result.psnr is None else f"{result.psnr:.6f} dB"
    status = "passed" if result.passed else "failed"
    print("# Pixel Diff Report")
    print()
    print(f"- Baseline: `{baseline.path}`")
    print(f"- Candidate: `{candidate.path}`")
    print(f"- Baseline loader: `{baseline.loader}`")
    print(f"- Candidate loader: `{candidate.loader}`")
    print(f"- Size: `{baseline.width}x{baseline.height}` vs `{candidate.width}x{candidate.height}`")
    print(f"- Mode: `{baseline.mode}` vs `{candidate.mode}`")
    print(f"- Channels: `{baseline.channels}` vs `{candidate.channels}`")
    print(f"- Per-channel threshold: `{threshold}`")
    print(f"- Result: `{status}` ({result.reason})")
    print(f"- Compared pixels: `{result.compared_pixels}`")
    print(f"- Different pixels: `{result.different_pixels}`")
    print(f"- Different channels: `{result.different_channels}`")
    print(f"- Max channel delta: `{result.max_channel_delta}`")
    print(f"- Mean absolute channel delta: `{result.mean_absolute_delta:.6f}`")
    print(f"- RMSE: `{result.rmse:.6f}`")
    print(f"- PSNR: `{psnr_text}`")
    print()
    print("Interpretation: exact equality requires threshold 0. Any nonzero threshold is a visual contract and must be justified by the rendering backend, antialiasing, color management, or platform matrix.")


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.threshold < 0 or args.threshold > 255:
        print("error: --threshold must be in the range 0..255", file=sys.stderr)
        return 2

    try:
        baseline = load_image(args.baseline)
        candidate = load_image(args.candidate)
        result = compare_images(baseline, candidate, args.threshold)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    print_report(baseline, candidate, args.threshold, result)
    return 0 if result.passed else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
