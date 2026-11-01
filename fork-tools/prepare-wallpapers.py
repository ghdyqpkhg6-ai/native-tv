#!/usr/bin/env python3
"""
Fork tool: turn a folder of wallpapers into the files Native Tv Home's gallery reads.

The originals are only read. For each picture it writes, into the output folder:

  gNN.jpg        Fill: 1920x1080, cropped around the faces it finds (OpenCV), or else around the
                 most detailed part of the picture, so the subject stays on screen.
  gNN-fit.jpg    Fit: the whole picture within 1920x1080, its own aspect ratio kept.
  gNN-blur.jpg   480x270, the picture blurred and darkened: the sides behind a portrait. The TV
                 scales it up on the GPU, which costs nothing and stays smooth since it is blurred.
  gNN-thumb.jpg  352x198, the picture as Automatic shows it (Fill for landscape, the blurred
                 sides for portrait), for the gallery's tiles.
  gallery.json   id, name, size, orientation and focus point of each, and a revision.

Every file is a baseline JPEG of at most 1920x1080 and well under 5 MB, which is what the TV's
wallpaper route accepts. Ids are kept by file name across runs (from the previous gallery.json),
so the wallpaper chosen on the TV stays chosen when pictures are added.

    prepare-wallpapers.py <walls folder> <output folder>
"""

import hashlib
import json
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageFilter, ImageOps

SCREEN = (1920, 1080)
BLUR = (480, 270)
THUMB = (352, 198)
EXTENSIONS = {'.jpg', '.jpeg', '.png', '.webp', '.bmp'}
CASCADES = [cv2.data.haarcascades + name for name in
            ('haarcascade_frontalface_default.xml', 'haarcascade_profileface.xml')]


def load(path):
    image = Image.open(path)
    image = ImageOps.exif_transpose(image)  # phone photos carry their rotation in EXIF
    if image.mode in ('RGBA', 'LA', 'P'):
        image = image.convert('RGBA')
        flat = Image.new('RGB', image.size, (0, 0, 0))
        flat.paste(image, mask=image.split()[3])
        return flat
    return image.convert('RGB')


def orientation(width, height):
    ratio = width / height
    if ratio >= 1.2:
        return 'landscape'
    if ratio <= 0.85:
        return 'portrait'
    return 'square'


def faces(image):
    """Face boxes as fractions of the picture."""
    scale = min(1.0, 1200 / max(image.size))
    small = image.resize((max(1, int(image.width * scale)), max(1, int(image.height * scale))))
    grey = cv2.equalizeHist(cv2.cvtColor(np.asarray(small), cv2.COLOR_RGB2GRAY))
    side = int(min(grey.shape) * 0.05)
    found = []
    for path in CASCADES:
        detector = cv2.CascadeClassifier(path)
        if detector.empty():
            continue
        for (x, y, w, h) in detector.detectMultiScale(grey, 1.1, 6, minSize=(side, side)):
            found.append((x / grey.shape[1], y / grey.shape[0], w / grey.shape[1], h / grey.shape[0]))
    return found


def salient_point(image):
    """Centre of the most distinctive part of the picture (spectral residual saliency)."""
    grey = np.asarray(image.convert('L').resize((64, 64)), dtype=np.float32)
    spectrum = np.fft.fft2(grey)
    log = np.log(np.abs(spectrum) + 1e-6)
    residual = log - cv2.blur(log, (3, 3))
    saliency = np.abs(np.fft.ifft2(np.exp(residual + 1j * np.angle(spectrum)))) ** 2
    saliency = cv2.GaussianBlur(saliency, (9, 9), 2.5)
    top = saliency >= np.quantile(saliency, 0.8)
    ys, xs = np.nonzero(top)
    weights = saliency[top]
    x = float((xs * weights).sum() / weights.sum() + 0.5) / 64
    y = float((ys * weights).sum() / weights.sum() + 0.5) / 64
    # Pulled towards the middle: saliency is a hint, not a certainty.
    return 0.5 + (x - 0.5) * 0.7, 0.5 + (y - 0.5) * 0.7


def focus_point(image):
    boxes = faces(image)
    if boxes:
        area = sum(w * h for _, _, w, h in boxes)
        x = sum((bx + w / 2) * w * h for bx, _, w, h in boxes) / area
        # A little above the faces' centre, so heads are not cut at the top.
        y = sum((by + h / 2) * w * h for _, by, w, h in boxes) / area - 0.02
        return x, y, len(boxes)
    x, y = salient_point(image)
    return x, y, 0


def crop_box(width, height, aspect, fx, fy):
    """The largest box of `aspect` inside the picture, centred on the focus as far as it can be."""
    if width / height > aspect:
        w, h = height * aspect, height
    else:
        w, h = width, width / aspect
    left = min(max(fx * width - w / 2, 0), width - w)
    top = min(max(fy * height - h / 2, 0), height - h)
    return (round(left), round(top), round(left + w), round(top + h))


def fitted(width, height, box):
    scale = min(box[0] / width, box[1] / height)
    return max(1, round(width * scale)), max(1, round(height * scale))


def save(image, path, quality):
    image.save(path, 'JPEG', quality=quality, optimize=True, progressive=False, subsampling=0
               if quality >= 92 else 2)


def build(source, out):
    out.mkdir(parents=True, exist_ok=True)
    previous = {}
    old = out / 'gallery.json'
    if old.is_file():
        for item in json.loads(old.read_text(encoding='utf-8')).get('items', []):
            previous[item.get('file')] = item.get('id')
    files = sorted(p for p in source.iterdir() if p.is_file() and p.suffix.lower() in EXTENSIONS)
    used = {v for v in previous.values() if v}
    next_number = 1
    items = []
    for path in files:
        identifier = previous.get(path.name)
        if not identifier:
            while 'g%02d' % next_number in used:
                next_number += 1
            identifier = 'g%02d' % next_number
            used.add(identifier)
        image = load(path)
        width, height = image.size
        kind = orientation(width, height)
        fx, fy, face_count = focus_point(image)

        fill = image.crop(crop_box(width, height, SCREEN[0] / SCREEN[1], fx, fy))
        fill = fill.resize(SCREEN, Image.LANCZOS)
        save(fill, out / (identifier + '.jpg'), 90)

        fit_size = fitted(width, height, SCREEN)
        fit = image.resize(fit_size, Image.LANCZOS)
        save(fit, out / (identifier + '-fit.jpg'), 90)

        backdrop = image.crop(crop_box(width, height, BLUR[0] / BLUR[1], 0.5, fy)).resize(BLUR, Image.LANCZOS)
        backdrop = backdrop.filter(ImageFilter.GaussianBlur(14))
        backdrop = Image.eval(backdrop, lambda v: int(v * 0.55))
        save(backdrop, out / (identifier + '-blur.jpg'), 86)

        if kind == 'landscape':
            thumb = fill.resize(THUMB, Image.LANCZOS)
        else:
            thumb = backdrop.resize(THUMB, Image.LANCZOS)
            inner = image.resize(fitted(width, height, THUMB), Image.LANCZOS)
            thumb.paste(inner, ((THUMB[0] - inner.width) // 2, (THUMB[1] - inner.height) // 2))
        save(thumb, out / (identifier + '-thumb.jpg'), 86)

        items.append({'id': identifier, 'file': path.name, 'name': path.stem[:60],
                      'width': width, 'height': height, 'orientation': kind,
                      'fitW': fit_size[0], 'fitH': fit_size[1],
                      'focus': [round(fx, 3), round(fy, 3)], 'faces': face_count})
        print('%s  %-9s %5dx%-5d faces=%d  %s' % (identifier, kind, width, height, face_count, path.name))

    keep = {item['id'] for item in items}
    for stale in out.glob('g*.jpg'):
        if stale.name.split('-')[0].split('.')[0] not in keep:
            stale.unlink()
    digest = hashlib.sha256()
    for item in sorted(items, key=lambda i: i['id']):
        for suffix in ('', '-fit', '-blur', '-thumb'):
            digest.update((out / (item['id'] + suffix + '.jpg')).read_bytes())
    items.sort(key=lambda i: i['name'].lower())
    gallery = {'revision': digest.hexdigest()[:12], 'items': items}
    old.write_text(json.dumps(gallery, indent=1), encoding='utf-8')
    print('%d wallpapers, revision %s' % (len(items), gallery['revision']))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    build(Path(sys.argv[1]), Path(sys.argv[2]))
