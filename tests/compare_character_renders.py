"""Compare the fixed source renderer scene with the native GTX capture."""
import json
import hashlib
import math
import sys
from pathlib import Path
from PIL import Image

source = Image.open(sys.argv[1]).convert("RGB")
native = Image.open(sys.argv[2]).convert("RGB")
if source.size != (1024, 1200) or native.size != source.size:
    raise SystemExit("Expected matching 1024 x 1200 character captures")
background = (31, 38, 46)
weapons = ["shooter", "twins", "brolly", "blaster", "spinner", "charger", "bow", "roller", "brush", "blade", "mitts", "bucket"]
if len(sys.argv)>3 and sys.argv[3]=="presets":
    weapons=["rookie","dash","pip","coral","marlo","riptide","nori","suki","kelp","skipper"]
elif len(sys.argv)>3 and sys.argv[3]=="subs":
    weapons=["bomb","sticky","burst","shaker","seeker","waddle","torpedo","tracer","boomerang","scan","curtain","sprinkler","mine","beacon","mist"]
elif len(sys.argv)>3:
    raise SystemExit("Expected comparison mode presets or subs")
results = []
for index, weapon in enumerate(weapons):
    x, y = index % 4 * 256, index // 4 * 300
    region = (x, y, x + 256, y + 300)
    union = intersection = error = squared = channels = 0
    for a, b in zip(source.crop(region).get_flattened_data(), native.crop(region).get_flattened_data()):
        visible_a = any(abs(a[c] - background[c]) > 3 for c in range(3))
        visible_b = any(abs(b[c] - background[c]) > 3 for c in range(3))
        union += visible_a or visible_b
        intersection += visible_a and visible_b
        if visible_a and visible_b:
            for c in range(3):
                delta = abs(a[c] - b[c])
                error += delta
                squared += delta * delta
                channels += 1
    iou = intersection / union if union else 0
    mae = error / channels if channels else 255
    results.append(dict(weapon=weapon, silhouette_iou=iou, rgb_mae=mae,
                        rgb_rmse=math.sqrt(squared / channels) if channels else 255,
                        passed=iou >= 0.995 and mae <= 1.0))
report = dict(source=str(Path(sys.argv[1]).resolve()), native=str(Path(sys.argv[2]).resolve()),
              source_sha256=hashlib.sha256(Path(sys.argv[1]).read_bytes()).hexdigest(),
              native_sha256=hashlib.sha256(Path(sys.argv[2]).read_bytes()).hexdigest(),
              thresholds=dict(silhouette_iou_min=0.995, rgb_mae_max=1.0), models=results)
print(json.dumps(report, indent=2))
raise SystemExit(0 if all(row["passed"] for row in results) else 1)
