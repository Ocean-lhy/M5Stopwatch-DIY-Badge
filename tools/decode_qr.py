#!/usr/bin/env python3
"""Print the content of a QR code screenshot, for the "text" field of profile.json.

    python3 tools/decode_qr.py profile/wechat_qr.jpg

Needs OpenCV (pip install opencv-python). WeChat codes have rounded finder
patterns and a logo, so a few crops and thresholds are tried.
"""
import sys

try:
    import cv2
    import numpy as np
except ImportError:
    sys.exit("decode_qr: OpenCV is required: pip install opencv-python")


def decode(path):
    gray = cv2.imdecode(np.fromfile(path, dtype=np.uint8), cv2.IMREAD_GRAYSCALE)
    if gray is None:
        sys.exit(f"decode_qr: cannot read {path}")
    detector = cv2.QRCodeDetector()
    h, w = gray.shape
    # The code sits slightly below the middle of a WeChat "My QR code" screenshot.
    for fraction in (1.0, 0.85, 0.75, 0.65):
        side = int(min(w, h) * fraction)
        for center in (0.5, 0.55, 0.6):
            x = (w - side) // 2
            y = max(0, min(h - side, int(h * center - side / 2)))
            crop = gray[y:y + side, x:x + side]
            _, binary = cv2.threshold(crop, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
            for image in (crop, binary):
                for target in (side, 400, 480, 640):
                    scaled = image if target == side else cv2.resize(image, (target, target), interpolation=cv2.INTER_AREA)
                    text = detector.detectAndDecode(scaled)[0]
                    if text:
                        return text
    return None


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    result = decode(sys.argv[1])
    if not result:
        sys.exit("decode_qr: no QR code found; try a sharper screenshot")
    print(result)
