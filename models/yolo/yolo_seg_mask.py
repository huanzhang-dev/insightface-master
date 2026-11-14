#!/usr/bin/env python3
"""
yolo_seg_mask.py - pixel masks for objects in an image, using YOLOv8-seg or YOLO11-seg.

Input : one image (jpg/png/bmp/...).
Output (written to --out, default ./seg_output/<image name>/):
    <name>_overlay.png        input image with coloured masks, boxes and labels
    <name>_mask_binary.png    8-bit, 255 where ANY detected object is, 0 = background
    <name>_mask_instances.png 16-bit, pixel value = instance id (1..N), 0 = background
    <name>_mask_color.png     each instance in its own colour on black
    <name>_detections.json    per-instance class, confidence, box, mask area
    instances/<name>_<id>_<class>.png  (optional, --save-instances) one binary mask per object

Install:
    pip install ultralytics opencv-python numpy

Examples:
    python yolo_seg_mask.py image.jpg
    python yolo_seg_mask.py image.jpg --model yolo11s-seg.pt --conf 0.4
    python yolo_seg_mask.py image.jpg --classes person car --save-instances
    python yolo_seg_mask.py image.jpg --model runs/segment/train/weights/best.pt   # your own trained model

Models (downloaded automatically on first use):
    YOLOv8 : yolov8n-seg.pt  yolov8s-seg.pt  yolov8m-seg.pt  yolov8l-seg.pt  yolov8x-seg.pt
    YOLO11 : yolo11n-seg.pt  yolo11s-seg.pt  yolo11m-seg.pt  yolo11l-seg.pt  yolo11x-seg.pt
    (n = fastest / least accurate ... x = slowest / most accurate)
"""

import argparse
import json
import sys
from pathlib import Path

import cv2
import numpy as np
from ultralytics import YOLO


def colour_for(i: int) -> tuple:
    """Distinct, repeatable BGR colour for instance i."""
    hue = int((i * 0.618034 * 180) % 180)  # golden-ratio step keeps neighbouring ids far apart in hue
    hsv = np.uint8([[[hue, 200, 230]]])
    return tuple(int(c) for c in cv2.cvtColor(hsv, cv2.COLOR_HSV2BGR)[0, 0])


def parse_args():
    p = argparse.ArgumentParser(description="Instance segmentation masks with YOLOv8-seg / YOLO11-seg")
    p.add_argument("image", help="path to the input image")
    p.add_argument("--model", default="yolo11n-seg.pt",
                   help="segmentation weights, e.g. yolov8n-seg.pt or yolo11n-seg.pt (default: %(default)s)")
    p.add_argument("--conf", type=float, default=0.25, help="minimum confidence (default: %(default)s)")
    p.add_argument("--iou", type=float, default=0.7, help="NMS IoU threshold (default: %(default)s)")
    p.add_argument("--imgsz", type=int, default=640, help="inference size in pixels (default: %(default)s)")
    p.add_argument("--classes", nargs="+", default=None,
                   help="keep only these class names, e.g. --classes person car")
    p.add_argument("--device", default=None, help="'cpu', '0' (first GPU), ... (default: auto)")
    p.add_argument("--alpha", type=float, default=0.5, help="mask opacity in the overlay (default: %(default)s)")
    p.add_argument("--out", default=None, help="output folder (default: ./seg_output/<image name>)")
    p.add_argument("--save-instances", action="store_true", help="also save one binary PNG per object")
    return p.parse_args()


def main():
    args = parse_args()
    img_path = Path(args.image)
    image = cv2.imread(str(img_path))
    if image is None:
        sys.exit(f"Could not read image: {img_path}")
    h, w = image.shape[:2]
    name = img_path.stem
    out_dir = Path(args.out) if args.out else Path("seg_output") / name
    out_dir.mkdir(parents=True, exist_ok=True)

    model = YOLO(args.model)
    if model.task != "segment":
        sys.exit(f"{args.model} is a '{model.task}' model. Use a segmentation model (name ends in -seg.pt).")

    # Translate class names to the model's class ids
    class_ids = None
    if args.classes:
        lookup = {v.lower(): k for k, v in model.names.items()}
        unknown = [c for c in args.classes if c.lower() not in lookup]
        if unknown:
            sys.exit(f"Unknown class name(s): {unknown}. Available: {sorted(model.names.values())}")
        class_ids = [lookup[c.lower()] for c in args.classes]

    # retina_masks=True returns masks at the original image resolution
    result = model.predict(image, conf=args.conf, iou=args.iou, imgsz=args.imgsz, classes=class_ids,
                           device=args.device, retina_masks=True, verbose=False)[0]

    binary = np.zeros((h, w), np.uint8)          # any object
    instances = np.zeros((h, w), np.uint16)      # instance id per pixel
    colour_mask = np.zeros_like(image)
    overlay = image.copy()
    detections = []

    if result.masks is not None:
        masks = result.masks.data.cpu().numpy().astype(np.float32)  # (N, H, W) in [0, 1]; fp16 on GPU
        boxes = result.boxes
        # Paint large objects first so smaller, overlapping ones stay visible on top
        order = np.argsort([-m.sum() for m in masks])
        for inst_id, i in enumerate(order, start=1):
            m = masks[i]
            if m.shape != (h, w):
                m = cv2.resize(m, (w, h), interpolation=cv2.INTER_LINEAR)
            m = m > 0.5

            cls_id = int(boxes.cls[i])
            cls_name = model.names[cls_id]
            conf = float(boxes.conf[i])
            x1, y1, x2, y2 = (int(round(v)) for v in boxes.xyxy[i].tolist())
            col = colour_for(inst_id)

            binary[m] = 255
            instances[m] = inst_id
            colour_mask[m] = col

            tint = overlay.copy()
            tint[m] = col
            overlay = cv2.addWeighted(tint, args.alpha, overlay, 1 - args.alpha, 0)
            contours, _ = cv2.findContours(m.astype(np.uint8), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
            cv2.drawContours(overlay, contours, -1, col, 2)

            detections.append({
                "id": inst_id, "class_id": cls_id, "class": cls_name,
                "confidence": round(conf, 4),
                "box_xyxy": [x1, y1, x2, y2],
                "mask_area_px": int(m.sum()),
                "mask_area_pct": round(100.0 * m.sum() / (h * w), 3),
            })

            if args.save_instances:
                inst_dir = out_dir / "instances"
                inst_dir.mkdir(exist_ok=True)
                cv2.imwrite(str(inst_dir / f"{name}_{inst_id:03d}_{cls_name.replace(' ', '_')}.png"),
                            m.astype(np.uint8) * 255)

        # Boxes and labels drawn last so they sit on top of every mask
        for d in detections:
            x1, y1, x2, y2 = d["box_xyxy"]
            col = colour_for(d["id"])
            text = f'{d["id"]}: {d["class"]} {d["confidence"]:.2f}'
            cv2.rectangle(overlay, (x1, y1), (x2, y2), col, 2)
            (tw, th), _ = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, 0.6, 2)
            ty = max(y1, th + 8)
            cv2.rectangle(overlay, (x1, ty - th - 8), (x1 + tw + 6, ty), col, -1)
            cv2.putText(overlay, text, (x1 + 3, ty - 5), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2,
                        cv2.LINE_AA)

    cv2.imwrite(str(out_dir / f"{name}_overlay.png"), overlay)
    cv2.imwrite(str(out_dir / f"{name}_mask_binary.png"), binary)
    cv2.imwrite(str(out_dir / f"{name}_mask_instances.png"), instances)  # 16-bit PNG
    cv2.imwrite(str(out_dir / f"{name}_mask_color.png"), colour_mask)
    summary = {"image": str(img_path), "model": args.model, "width": w, "height": h,
               "num_objects": len(detections), "objects": detections}
    with open(out_dir / f"{name}_detections.json", "w") as f:
        json.dump(summary, f, indent=2)

    print(f"Model: {args.model}   image: {img_path} ({w}x{h})   objects: {len(detections)}")
    for d in detections:
        print(f'  {d["id"]:>3}  {d["class"]:<14} conf={d["confidence"]:.2f}  '
              f'box={d["box_xyxy"]}  area={d["mask_area_px"]} px ({d["mask_area_pct"]}%)')
    print(f"Saved to: {out_dir.resolve()}")


if __name__ == "__main__":
    main()
