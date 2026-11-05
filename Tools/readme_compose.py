# Composes the README screenshots in the spectator-screen layout from Tools/readme_shots.ps1 output: the observer
# render target (left) + the operator's left eye (right, without the unlit band at the top of the eye buffer, at the
# observer's aspect). Usage: python readme_compose.py <shots_dir> <out_dir>
import os
import sys

from PIL import Image, ImageDraw, ImageFont

LABEL = {"GT": "GT (reference, not available to a real system)", "M0": "M0 head-locked", "M1": "M1 sphere, R 1 m",
         "M2": "M2 sphere, adaptive R", "M3": "M3 depth mesh", "M4f0": "M4 mesh + sphere fill (fill 0)",
         "M4f1": "M4 mesh + nearest background (fill 1)", "M4f2": "M4 mesh + keyframe memory (fill 2)"}
HALF_W = 800


def main(src, out):
    os.makedirs(out, exist_ok=True)
    try:
        font = ImageFont.truetype("segoeuib.ttf", 17)
    except OSError:
        font = ImageFont.load_default()
    for f in sorted(os.listdir(src)):
        if not f.endswith("_obs"):
            continue
        tag, m = f[:-len("_obs")].split("_", 1)
        eyes = Image.open(os.path.join(src, "%s_%s_eyes.png" % (tag, m))).convert("RGB")
        obs = Image.open(os.path.join(src, f)).convert("RGB")
        top, bottom = int(0.057 * eyes.height), int(0.986 * eyes.height)
        w = int(round((bottom - top) * obs.width / obs.height))
        x0 = (eyes.width // 2 - w) // 2
        eye = eyes.crop((x0, top, x0 + w, bottom))
        hh, bar = int(round(HALF_W * obs.height / obs.width)), 30
        sheet = Image.new("RGB", (2 * HALF_W + 4, hh + bar), (16, 18, 20))
        sheet.paste(obs.resize((HALF_W, hh), Image.LANCZOS), (0, bar))
        sheet.paste(eye.resize((HALF_W, hh), Image.LANCZOS), (HALF_W + 4, bar))
        d = ImageDraw.Draw(sheet)
        d.text((8, 5), "Third-person observer (PC window only)", fill=(235, 235, 235), font=font)
        d.text((HALF_W + 12, 5), "Operator's left eye: " + LABEL.get(m, m), fill=(255, 255, 255), font=font)
        path = os.path.join(out, "%s_%s.jpg" % (tag, m))
        sheet.save(path, "JPEG", quality=84, optimize=True)
        print(path, sheet.size)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
