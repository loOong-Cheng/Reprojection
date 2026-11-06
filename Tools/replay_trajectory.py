"""Writes operator trajectories for the replay (FTeleopReplay CSV): the method GIFs, temporal metrics and the matrix.

Tracking frame: x forward, y right, z up, cm; the head is the HMD centre. Every motion is a minimum-jerk segment (the
usual model of human point-to-point movement) and the head turns about a neck point 10 cm behind and 5 cm below the
eye, like a real head (teleop_eval._head). A small idle sway runs all the time. Each starts with 1.5 s of stillness
(the automatic recenter fires after 0.5 s); the metrics start at 1 s.

Motions (--motion):
  desk      the README trajectory (10 s): turn 45 deg right to the cube stack, grab the top cube, carry it to the front
            while looking back, drop it, lean right 15 cm, quick look left 30 deg
  turn      yaw 40 deg left, 80 deg right, back (peak 125 deg/s; within the neck's 300 deg/s)
  fastturn  snap 90 deg right and back (peak 375 deg/s, 2560 deg/s^2: beyond the neck's 300 deg/s and 2000 deg/s^2),
            then a fast 35 deg nod
  biglean   lean 40 cm right (the waist rolls at most 20 deg: ~19 cm), back, lean in 30 cm (waist pitch limit 30 deg)
  standup   rise 25 cm looking down at the desk (the waist does not follow height at all), sit back
  tilt      roll the head 25 deg right, then left (the 2-DoF neck has no roll)
  strafe    the robot base drives 80 cm right and back (peak 75 cm/s); the operator sits still, looking ahead
  drive     the robot base backs up 100 cm and returns (peak 75 cm/s)
The base moves (strafe, drive) are commands: the robot executes them after the command latency, like the neck;
columns bx, by, byaw (station frame) are written only for motions that move the base.

Cube positions come from the replay's "[replay] scene" log lines (operator recentered at the seat, head at 120 cm):
right stack top GrabActor_StaticMesh_Physics10 at (62, 70, 96.6); its grab radius is 6 cm around the hand.

Usage: python replay_trajectory.py <out.csv> [--motion desk] [--hz 120]
"""

import argparse
import json
import math

NECK_TO_EYE = (10.0, 0.0, 5.0)  # cm, same as the robot's camera mount over its neck pivot
EYE_HEIGHT = 120.0
REST_L = (35.0, -20.0, 85.0)
REST_R = (35.0, 20.0, 85.0)
CUBE = (62.0, 70.0, 96.6)       # top cube of the right stack
CARRY_TO = (52.0, 12.0, 106.0)  # in front of the operator, above the desk
DURATION = 10.0

# What happens when (s); used by the metrics (per-segment means) and the GIF captions. Written into the CSV
# ("# segments=") so the tools read them from the run's trajectory.csv.
SEGMENTS = [
    ("still", 1.0, 1.5),
    ("turn right 45 deg to the cubes", 1.5, 2.4),
    ("reach, grab the top cube", 2.4, 3.9),
    ("carry it to the front, look back", 3.9, 5.0),
    ("drop it, hand back", 5.0, 6.1),
    ("lean right 15 cm and back", 6.1, 7.6),
    ("quick look left 30 deg and back", 7.6, 8.9),
    ("hold still", 8.9, 10.0),
]

# Key poses per channel: (time s, value). Between two keys the value follows a minimum-jerk curve; it holds otherwise.
KEYS = {
    # head rotation (deg) and translation of the neck (cm, tracking frame)
    "yaw":   [(0, 0), (1.5, 0), (2.4, 45), (3.9, 45), (5.0, 5), (5.4, 0), (7.8, 0), (8.2, -30), (8.5, -30), (8.9, 0)],
    "pitch": [(0, -15), (1.5, -15), (2.4, -25), (3.9, -25), (5.0, -18), (7.8, -18), (8.2, -12), (8.5, -12), (8.9, -15)],
    "roll":  [(0, 0), (6.1, 0), (6.7, 3), (7.0, 3), (7.6, 0)],
    "nx":    [(0, 0), (2.4, 0), (3.3, 4), (3.9, 4), (5.0, 0)],    # leans in a little while reaching
    "ny":    [(0, 0), (6.1, 0), (6.7, 15), (7.0, 15), (7.6, 0)],  # leans right 15 cm and back
    "nz":    [(0, 0)],
    # right hand: reach above the cube, lower onto it, grab, carry to the front, release, back to rest
    "rx": [(0, REST_R[0]), (2.4, REST_R[0]), (3.3, CUBE[0]), (3.6, CUBE[0]), (3.9, CUBE[0]), (5.0, CARRY_TO[0]), (5.5, CARRY_TO[0]), (6.2, REST_R[0])],
    "ry": [(0, REST_R[1]), (2.4, REST_R[1]), (3.3, CUBE[1]), (3.6, CUBE[1]), (3.9, CUBE[1]), (5.0, CARRY_TO[1]), (5.5, CARRY_TO[1]), (6.2, REST_R[1])],
    "rz": [(0, REST_R[2]), (2.4, REST_R[2]), (3.3, CUBE[2] + 6), (3.6, CUBE[2]), (3.9, CUBE[2] + 2), (5.0, CARRY_TO[2]), (5.5, CARRY_TO[2]), (6.2, REST_R[2])],
    "ryaw":   [(0, 0), (2.4, 0), (3.3, 35), (5.0, 5), (6.2, 0)],
    "rpitch": [(0, 0), (2.4, 0), (3.3, -35), (3.9, -35), (5.0, -10), (6.2, 0)],
    "rgrasp": [(0, 0), (3.65, 0), (3.8, 1), (5.2, 1), (5.35, 0)],
    "rcurl":  [(0, 0.2), (3.65, 0.2), (3.8, 1), (5.2, 1), (5.35, 0.2)],
}

# Matrix motions. Channels left out keep the rest pose (DEFAULTS).
DEFAULTS = {"yaw": 0.0, "pitch": -15.0, "roll": 0.0, "nx": 0.0, "ny": 0.0, "nz": 0.0, "rx": REST_R[0], "ry": REST_R[1],
            "rz": REST_R[2], "ryaw": 0.0, "rpitch": 0.0, "rgrasp": 0.0, "rcurl": 0.2, "bx": 0.0, "by": 0.0, "byaw": 0.0}
MOTIONS = {
    "desk": {"keys": KEYS, "duration": DURATION, "segments": SEGMENTS},
    "turn": {"duration": 6.0, "keys": {
        "yaw": [(0, 0), (1.5, 0), (2.3, -40), (2.6, -40), (3.8, 40), (4.1, 40), (4.9, 0)]},
        "segments": [("still", 1.0, 1.5), ("turn left 40 deg", 1.5, 2.6), ("turn right 80 deg", 2.6, 4.1),
                     ("back to the centre", 4.1, 4.9), ("hold", 4.9, 6.0)]},
    "fastturn": {"duration": 6.0, "keys": {
        "yaw": [(0, 0), (1.5, 0), (1.95, 90), (2.55, 90), (3.0, 0)],
        "pitch": [(0, -15), (3.6, -15), (3.95, -50), (4.3, -50), (4.65, -15)]},
        "segments": [("still", 1.0, 1.5), ("snap right 90 deg (375 deg/s)", 1.5, 2.55), ("snap back", 2.55, 3.6),
                     ("fast nod down 35 deg", 3.6, 4.65), ("hold", 4.65, 6.0)]},
    "biglean": {"duration": 7.5, "keys": {
        "ny": [(0, 0), (1.5, 0), (2.5, 40), (3.3, 40), (4.3, 0)],
        "roll": [(0, 0), (1.5, 0), (2.5, 5), (3.3, 5), (4.3, 0)],
        "nx": [(0, 0), (4.5, 0), (5.3, 30), (5.8, 30), (6.6, 0)],
        "pitch": [(0, -15), (4.5, -15), (5.3, -25), (5.8, -25), (6.6, -15)]},
        "segments": [("still", 1.0, 1.5), ("lean right 40 cm (waist reaches ~19)", 1.5, 3.3), ("back", 3.3, 4.5),
                     ("lean in 30 cm", 4.5, 5.8), ("back", 5.8, 6.6), ("hold", 6.6, 7.5)]},
    "standup": {"duration": 6.0, "keys": {
        "nz": [(0, 0), (1.5, 0), (2.7, 25), (4.0, 25), (5.2, 0)],
        "pitch": [(0, -15), (1.5, -15), (2.7, -35), (4.0, -35), (5.2, -15)]},
        "segments": [("still", 1.0, 1.5), ("stand up 25 cm, look down", 1.5, 2.7), ("stay up", 2.7, 4.0),
                     ("sit back", 4.0, 5.2), ("hold", 5.2, 6.0)]},
    "tilt": {"duration": 6.5, "keys": {
        "roll": [(0, 0), (1.5, 0), (2.3, 25), (3.3, 25), (4.5, -25), (5.0, -25), (5.8, 0)]},
        "segments": [("still", 1.0, 1.5), ("tilt the head right 25 deg", 1.5, 3.3), ("tilt left 25 deg", 3.3, 5.0),
                     ("back", 5.0, 5.8), ("hold", 5.8, 6.5)]},
    "strafe": {"duration": 7.0, "keys": {
        "by": [(0, 0), (1.5, 0), (3.5, 80), (4.0, 80), (6.0, 0)]},
        "segments": [("still", 1.0, 1.5), ("base drives 80 cm right", 1.5, 3.5), ("stops", 3.5, 4.0),
                     ("drives back", 4.0, 6.0), ("hold", 6.0, 7.0)]},
    "drive": {"duration": 8.0, "keys": {
        "bx": [(0, 0), (1.5, 0), (4.0, -100), (4.5, -100), (7.0, 0)]},
        "segments": [("still", 1.0, 1.5), ("base backs up 100 cm", 1.5, 4.0), ("stops", 4.0, 4.5),
                     ("drives forward", 4.5, 7.0), ("hold", 7.0, 8.0)]},
}
BASE_CHANNELS = ("bx", "by", "byaw")


def min_jerk(s):
    return s * s * s * (10 - 15 * s + 6 * s * s)


def channel(keys, t):
    if t <= keys[0][0]:
        return keys[0][1]
    for (t0, v0), (t1, v1) in zip(keys, keys[1:]):
        if t <= t1:
            return v0 + (v1 - v0) * min_jerk((t - t0) / (t1 - t0)) if t1 > t0 else v1
    return keys[-1][1]


def rotate(v, yaw, pitch, roll=0.0):
    """UE FRotator(pitch, yaw, roll) applied to v = (forward, right, up)."""
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    cr, sr = math.cos(math.radians(roll)), math.sin(math.radians(roll))
    fwd = (cp * cy, cp * sy, sp)
    right = (sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp)
    up = (-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp)
    return tuple(v[0] * fwd[i] + v[1] * right[i] + v[2] * up[i] for i in range(3))


def sample(t, keys=KEYS, base=False):
    k = dict(DEFAULTS)
    k.update({name: channel(ch, t) for name, ch in keys.items()})
    # Idle sway (a person never holds perfectly still): a few mm and a fraction of a degree.
    sway = (0.4 * math.sin(2 * math.pi * 0.23 * t), 0.5 * math.sin(2 * math.pi * 0.31 * t + 1.0), 0.3 * math.sin(2 * math.pi * 0.17 * t + 2.0))
    yaw = k["yaw"] + 0.4 * math.sin(2 * math.pi * 0.27 * t + 0.5)
    pitch = k["pitch"] + 0.3 * math.sin(2 * math.pi * 0.21 * t + 1.5)
    roll = k["roll"]
    # Eye (HMD centre) = neck + R * NECK_TO_EYE, with the neck at its rest place plus the lean.
    r = rotate(NECK_TO_EYE, yaw, pitch, roll)
    head = (k["nx"] + sway[0] + r[0] - NECK_TO_EYE[0], k["ny"] + sway[1] + r[1] - NECK_TO_EYE[1],
            EYE_HEIGHT + k["nz"] + sway[2] + r[2] - NECK_TO_EYE[2])
    left = (REST_L[0] + 0.8 * math.sin(2 * math.pi * 0.4 * t), REST_L[1] + 0.6 * math.sin(2 * math.pi * 0.33 * t + 1), REST_L[2] + 0.5 * math.sin(2 * math.pi * 0.29 * t + 2))
    right = (k["rx"], k["ry"], k["rz"])
    row = [t, *head, pitch, yaw, roll, *left, 0.0, 0.0, 0.0, *right, k["rpitch"], k["ryaw"], 0.0,
           0.0, 0.2, 0.0, 0.0,                    # left: relaxed, index and thumb resting on the controller
           k["rgrasp"], k["rcurl"], 0.0, 0.0]
    if base:
        row += [k[c] for c in BASE_CHANNELS]
    return row


HEADER = ("t,hx,hy,hz,hpitch,hyaw,hroll,lx,ly,lz,lpitch,lyaw,lroll,rx,ry,rz,rpitch,ryaw,rroll,"
          "lgrasp,lcurl,lpoint,lthumb,rgrasp,rcurl,rpoint,rthumb")


def write(out, motion="desk", hz=120.0):
    m = MOTIONS[motion]
    n = int(round(m["duration"] * hz))
    base = any(c in m["keys"] for c in BASE_CHANNELS)
    with open(out, "w", newline="\n") as f:
        f.write("# Tools/replay_trajectory.py --motion %s\n" % motion)
        f.write("# segments=%s\n" % json.dumps(m["segments"]))
        f.write(HEADER + ("," + ",".join(BASE_CHANNELS) if base else "") + "\n")
        for i in range(n + 1):
            f.write(",".join("%.4f" % v for v in sample(i / hz, m["keys"], base)) + "\n")
    return n + 1, m["duration"]


def read_segments(trajectory_csv):
    """Segments from a trajectory CSV written by write() (the README trajectory's SEGMENTS for older files)."""
    try:
        with open(trajectory_csv) as f:
            for line in f:
                if line.startswith("# segments="):
                    return [tuple(s) for s in json.loads(line[len("# segments="):])]
                if not line.startswith("#"):
                    break
    except OSError:
        pass
    return SEGMENTS


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--motion", default="desk", choices=sorted(MOTIONS))
    ap.add_argument("--hz", type=float, default=120.0)
    a = ap.parse_args()
    n, duration = write(a.out, a.motion, a.hz)
    print("wrote", a.out, n, "samples,", duration, "s")


if __name__ == "__main__":
    main()
