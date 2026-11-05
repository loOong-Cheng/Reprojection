"""Writes the operator trajectory used for the method GIFs and temporal metrics (FTeleopReplay CSV).

A seated operator at the Template desk (tracking frame: x forward, y right, z up, cm; the head is the HMD centre):
looks right at the purple cube stack, reaches with the right hand, grabs the top cube, carries it to the front while
looking back, drops it on the desk, leans right and back, then turns the head quickly left and back. Every motion
is a minimum-jerk segment (the usual model of human point-to-point movement) and the head turns about a neck point
10 cm behind and 5 cm below the eye, like a real head (teleop_eval._head). A small idle sway runs all the time.

Cube positions come from the replay's "[replay] scene" log lines (operator recentered at the seat, head at 120 cm):
right stack top GrabActor_StaticMesh_Physics10 at (62, 70, 96.6); its grab radius is 6 cm around the hand.

Usage: python replay_trajectory.py <out.csv> [--hz 120]
"""

import argparse
import math

NECK_TO_EYE = (10.0, 0.0, 5.0)  # cm, same as the robot's camera mount over its neck pivot
EYE_HEIGHT = 120.0
REST_L = (35.0, -20.0, 85.0)
REST_R = (35.0, 20.0, 85.0)
CUBE = (62.0, 70.0, 96.6)       # top cube of the right stack
CARRY_TO = (52.0, 12.0, 106.0)  # in front of the operator, above the desk
DURATION = 10.0

# What happens when (s); used by the metrics (per-segment means) and the GIF captions.
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


def sample(t):
    k = {name: channel(keys, t) for name, keys in KEYS.items()}
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
    return row


HEADER = ("t,hx,hy,hz,hpitch,hyaw,hroll,lx,ly,lz,lpitch,lyaw,lroll,rx,ry,rz,rpitch,ryaw,rroll,"
          "lgrasp,lcurl,lpoint,lthumb,rgrasp,rcurl,rpoint,rthumb")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--hz", type=float, default=120.0)
    a = ap.parse_args()
    n = int(round(DURATION * a.hz))
    with open(a.out, "w", newline="\n") as f:
        f.write("# Tools/replay_trajectory.py: look at the cube stack, grab the top cube, carry it, drop it, lean right, quick look left\n")
        f.write(HEADER + "\n")
        for i in range(n + 1):
            f.write(",".join("%.4f" % v for v in sample(i / a.hz)) + "\n")
    print("wrote", a.out, n + 1, "samples,", DURATION, "s")


if __name__ == "__main__":
    main()
