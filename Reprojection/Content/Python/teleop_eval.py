"""Evaluation of the presentation methods over many views (desktop PIE).

For each scene: the operator looks at a target and sways a little (the robot follows, M2's radius and the keyframe
memory build up as in normal use), then the remote side is frozen (teleop.Debug.Freeze: props, robot, shown frame,
radius and memory stand still) and the eye is moved by the scene's offset (what the robot has not caught up with:
latency, or a motion it cannot make). Every method and GT is then rendered for exactly that frozen state and saved,
plus two class maps for the metrics (teleop.Debug.MaskView): with the exact depth (red = disocclusion) and with the
degraded depth (red = every hole of the depth mesh).

Run in the editor while PIE runs the VR Template map (desktop, no HMD):
    import importlib, teleop_eval; importlib.reload(teleop_eval); teleop_eval.run(r"D:/out/dir")
Non-blocking: the sequence runs on the editor tick; it logs "EVAL| done" at the end. Metrics: Tools/eval_metrics.py.
"""

import json
import math
import os
import time

import unreal

# Methods: key -> console variables. GT first (reference); masks last.
METHODS = [
    ("GT", {"teleop.Method": 5}),
    ("M0", {"teleop.Method": 0}),
    ("M1", {"teleop.Method": 1}),
    ("M2", {"teleop.Method": 2}),
    ("M3", {"teleop.Method": 3}),
    ("M4f0s2", {"teleop.Method": 4, "teleop.M4.Fill": 0, "teleop.M4.Sphere": 2}),
    ("M4f1s2", {"teleop.Method": 4, "teleop.M4.Fill": 1, "teleop.M4.Sphere": 2}),
    ("M4f2s2", {"teleop.Method": 4, "teleop.M4.Fill": 2, "teleop.M4.Sphere": 2}),
    ("M4f0s1", {"teleop.Method": 4, "teleop.M4.Fill": 0, "teleop.M4.Sphere": 1}),
    ("M4f1s1", {"teleop.Method": 4, "teleop.M4.Fill": 1, "teleop.M4.Sphere": 1}),
    ("M4f2s1", {"teleop.Method": 4, "teleop.M4.Fill": 2, "teleop.M4.Sphere": 1}),
]
MASKS = [
    ("maskGT", {"teleop.Method": 4, "teleop.M4.Fill": 0, "teleop.M4.Sphere": 2, "teleop.Debug.MaskView": 1, "teleop.Debug.MeshGTDepth": 1}),
    ("maskDeg", {"teleop.Method": 4, "teleop.M4.Fill": 0, "teleop.M4.Sphere": 2, "teleop.Debug.MaskView": 1, "teleop.Debug.MeshGTDepth": 0}),
]

# Scenes. look = (yaw, pitch) deg; base = head offset (x fwd, y right, z up) cm at the look pose; hands = desktop
# controller rest poses (tracking frame, cm) or None for the default; delta = what the eye does after the freeze:
# (dx, dy, dz, dyaw, dpitch) in the tracking frame (x = seat forward); hand_motion = hand sway amplitude (cm) during
# the pre-motion (the robot hands move, so memory can hold stale hands, as in use).
# Offsets span what latency and robot limits produce: a head moving 0.3-1 m/s or turning 100-300 deg/s with ~60-100 ms
# of frame age plus the robot's response gives ~5-20 cm / 5-20 deg; the waist does not follow height changes.
SCENES = [
    {"id": "S01", "name": "desk front, lean right 10 cm", "look": (0.0, -15.0), "delta": (0, 10, 0, 0, 0), "hand_motion": 4.0,
     "content": "checkerboard, fan (moving), ball, doorway, white box + rods, cubes"},
    {"id": "S02", "name": "rods and boxes, lean left-up", "look": (30.0, -15.0), "delta": (0, -8, 3, 0, 0),
     "content": "1 cm rods (high depth gradient), white boxes, cubes, far wall"},
    {"id": "S03", "name": "fan and doorway, small lean + turn", "look": (-15.0, -5.0), "delta": (0, 6, 0, 4, 0),
     "content": "rotating fan blades (moving, thin), sliding block (moving), checkerboard, dark doorway (far)"},
    {"id": "S04", "name": "hands at the desk, lean in", "look": (0.0, -40.0), "delta": (6, 0, -5, 0, 0),
     "hands": ((50.0, -15.0, 92.0), (46.0, 18.0, 90.0)), "hand_motion": 8.0,
     "content": "robot hands (actuators, moving during the pre-motion), cubes, desk edge"},
    {"id": "S05", "name": "cube stack and ball, big lean right", "look": (-45.0, -30.0), "delta": (0, 15, 0, 0, 0), "hand_motion": 5.0,
     "content": "purple cube stack, ball, hands, desk edge, floor"},
    {"id": "S06", "name": "far courtyard corner, lean right 20 cm", "look": (150.0, 0.0), "delta": (0, 20, 0, 0, 0),
     "content": "far walls (4-10 m), blue cubes on the floor, sky"},
    {"id": "S07", "name": "head turn left 15 deg", "look": (-90.0, -10.0), "delta": (0, 2, 0, -15, 0),
     "content": "side wall, cube stack edge, desk corner"},
    {"id": "S08", "name": "stand up 15 cm (waist cannot follow)", "look": (30.0, -30.0), "delta": (0, 0, 15, 0, -5), "hand_motion": 4.0,
     "content": "desk top: rods, white box, cubes, hands"},
    {"id": "S09", "name": "lean back 20 cm", "look": (0.0, -25.0), "delta": (-20, 0, 0, 0, 0), "hand_motion": 4.0,
     "content": "whole desk: checkerboard, fan, ball, hands, rods"},
    {"id": "S10", "name": "fast diagonal nod", "look": (60.0, -20.0), "delta": (0, 4, 0, 8, -12),
     "content": "white boxes, cubes, desk, wall corner"},
    {"id": "S11", "name": "near floor and desk side", "look": (-120.0, -45.0), "delta": (0, -10, -5, 0, 0),
     "content": "floor (near, slanted), desk side, wall"},
    {"id": "S12", "name": "control: eye at the camera", "look": (0.0, 0.0), "delta": (0, 0, 0, 0, 0),
     "content": "desk front looking level (the camera is exactly at the eye only at pitch 0; pitched, the neck pivot puts it\n"
                "~3 cm off): all methods should match GT"},
]

DEFAULT_HANDS = ((35.0, -20.0, 85.0), (35.0, 20.0, 85.0))
SWAY_SEC = 3.0
SETTLE_SEC = 2.6        # > teleop.M2.WindowSec, so R reflects the look pose
METHOD_WAIT_SEC = 1.6   # TSR history and >= 90 frame-time samples
SHOT_WAIT_SEC = 0.5


def _world():
    return unreal.EditorLevelLibrary.get_game_world()


def _cmd(c):
    unreal.SystemLibrary.execute_console_command(_world(), c)


def _set(cvars):
    for k, v in cvars.items():
        _cmd("%s %s" % (k, v))


def _pawn():
    return unreal.GameplayStatics.get_player_pawn(_world(), 0)


def _hands(h):
    left, right = h or DEFAULT_HANDS
    p = _pawn()
    p.set_editor_property("desktop_left_hand", unreal.Vector(*left))
    p.set_editor_property("desktop_right_hand", unreal.Vector(*right))


# The head turns about a neck point behind and below the eye, as a real head does (the desktop head pose otherwise
# turns about the eye, so the implied neck moves and the robot's waist leans and rolls its cameras to follow it).
# Same offset as the robot's camera mount (10 cm forward, 5 cm up), so the robot matches a head that only turns.
NECK_TO_EYE = (10.0, 0.0, 5.0)


def _rotate(v, yaw, pitch):
    """UE FRotator(pitch, yaw, 0) applied to v = (forward, right, up)."""
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    fwd = (cp * cy, cp * sy, sp)
    right = (-sy, cy, 0.0)
    up = (-sp * cy, -sp * sy, cp)
    return tuple(v[0] * fwd[i] + v[1] * right[i] + v[2] * up[i] for i in range(3))


def _head(x, y, z, yaw, pitch):
    """Head pose: neck fixed, eye = neck + R(yaw, pitch) * NECK_TO_EYE, plus the translation (x, y, z) cm."""
    r = _rotate(NECK_TO_EYE, yaw, pitch)
    x += r[0] - NECK_TO_EYE[0]
    y += r[1] - NECK_TO_EYE[1]
    z += r[2] - NECK_TO_EYE[2]
    _set({"teleop.Debug.HeadOffsetX": "%.3f" % x, "teleop.Debug.HeadOffsetY": "%.3f" % y, "teleop.Debug.HeadOffsetZ": "%.3f" % z,
          "teleop.Debug.HeadYawDeg": "%.3f" % yaw, "teleop.Debug.HeadPitchDeg": "%.3f" % pitch})


class _Seq:
    """Timed steps on the editor tick: each step is (duration_s, fn(t_in_step) or None, label)."""

    def __init__(self, steps, on_done=None):
        self.steps = steps
        self.i = -1
        self.t0 = 0.0
        self.on_done = on_done
        self.handle = unreal.register_slate_post_tick_callback(self._tick)
        self._advance(time.time())

    def _advance(self, now):
        self.i += 1
        self.t0 = now
        if self.i < len(self.steps):
            dur, fn, label = self.steps[self.i]
            if fn:
                fn(0.0)

    def _tick(self, dt):
        now = time.time()
        if self.i >= len(self.steps):
            return
        dur, fn, label = self.steps[self.i]
        t = now - self.t0
        if t >= dur:
            self._advance(now)
            if self.i >= len(self.steps):
                unreal.unregister_slate_post_tick_callback(self.handle)
                if self.on_done:
                    self.on_done()
            return
        if fn and getattr(fn, "continuous", False):
            fn(t)


def _continuous(fn):
    fn.continuous = True
    return fn


def scene_steps(sc, out_dir, methods=METHODS, masks=MASKS, survey=False):
    """Steps for one scene."""
    yaw, pitch = sc["look"]
    bx, by, bz = sc.get("base", (0.0, 0.0, 0.0))
    dx, dy, dz, dyaw, dpitch = sc.get("delta", (0, 0, 0, 0, 0))
    sid = sc["id"]
    steps = []

    def setup(_):
        _set({"teleop.Debug.Freeze": 0, "teleop.Debug.MaskView": 0, "teleop.Debug.MeshGTDepth": 0, "teleop.Debug.HideHud": 1})
        # Pre-motion in M4 fill 2 (adaptive sphere): R and the memory evolve as in use.
        _set({"teleop.Method": 4 if not survey else 5, "teleop.M4.Fill": 2, "teleop.M4.Sphere": 2})
        _hands(sc.get("hands"))
        _head(bx, by, bz, yaw, pitch)
        _cmd("teleop.Memory.Clear")
        unreal.log("EVAL| scene %s start" % sid)
    steps.append((1.5, setup, "setup"))

    if not survey:
        @_continuous
        def sway(t):
            # A natural small sway around the look pose (lateral, vertical, yaw), as while watching.
            s = math.sin(2 * math.pi * t / SWAY_SEC)
            c = math.sin(2 * math.pi * t / (SWAY_SEC / 2))
            _head(bx + 2.0 * c, by + 6.0 * s, bz + 2.0 * c, yaw + 6.0 * s, pitch + 2.0 * c)
            hm = sc.get("hand_motion")
            if hm:
                left, right = sc.get("hands") or DEFAULT_HANDS
                p = _pawn()
                p.set_editor_property("desktop_left_hand", unreal.Vector(left[0] + hm * s, left[1] + hm * c, left[2] + 0.5 * hm * s))
                p.set_editor_property("desktop_right_hand", unreal.Vector(right[0] + hm * c, right[1] - hm * s, right[2] + 0.5 * hm * c))
        steps.append((SWAY_SEC, sway, "sway"))

        def back(_):
            _head(bx, by, bz, yaw, pitch)
            _hands(sc.get("hands"))
        steps.append((SETTLE_SEC, back, "settle"))

        def freeze(_):
            _cmd("teleop.Eval.State %s/pre" % sid)
            _cmd("teleop.Debug.Freeze 1")
        steps.append((0.4, freeze, "freeze"))

        def move(_):
            _head(bx + dx, by + dy, bz + dz, yaw + dyaw, pitch + dpitch)
        steps.append((0.6, move, "move"))

    for key, cv in list(methods) + ([] if survey else list(masks)):
        def apply(_, cv=cv):
            _set({"teleop.Debug.MaskView": 0, "teleop.Debug.MeshGTDepth": 0})
            _set(cv)
        steps.append((METHOD_WAIT_SEC, apply, "apply " + key))

        def shot(_, key=key):
            _cmd("teleop.Eval.State %s/%s" % (sid, key))
            _cmd("teleop.Eval.Shot %s" % os.path.join(out_dir, "%s_%s.png" % (sid, key)).replace("\\", "/"))
        steps.append((SHOT_WAIT_SEC, shot, "shot " + key))

    def end(_):
        _set({"teleop.Debug.Freeze": 0, "teleop.Debug.MaskView": 0, "teleop.Debug.MeshGTDepth": 0})
        unreal.log("EVAL| scene %s done" % sid)
    steps.append((0.3, end, "end"))
    return steps


def restore():
    _set({"teleop.Debug.Freeze": 0, "teleop.Debug.MaskView": 0, "teleop.Debug.MeshGTDepth": 0, "teleop.Debug.HideHud": 0,
          "teleop.Method": 5, "teleop.M4.Fill": 0, "teleop.M4.Sphere": 2})
    _head(0, 0, 0, 0, 0)
    _hands(None)


def run(out_dir, scenes=None, methods=METHODS, masks=MASKS, survey=False):
    """Run the scenes (default: SCENES); screenshots and scenes.json go to out_dir."""
    scenes = scenes if scenes is not None else SCENES
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "scenes.json"), "w") as f:
        json.dump({"scenes": scenes, "methods": [m[0] for m in methods], "masks": [m[0] for m in masks],
                   "sway_sec": SWAY_SEC, "settle_sec": SETTLE_SEC}, f, indent=1)
    steps = []
    for sc in scenes:
        steps += scene_steps(sc, out_dir, methods, masks, survey)

    def done():
        restore()
        unreal.log("EVAL| done")
    return _Seq(steps, done)
