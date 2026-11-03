"""PIE test helpers for the teleop rig (run while Play-In-Editor is active).

    import importlib, teleop_tests; importlib.reload(teleop_tests)
    teleop_tests.grab_begin()      # put the right proxy hand on the nearest Template cube and grab
    teleop_tests.lift(30)          # move the hand (and cube) up 30 cm
    teleop_tests.throw()           # move the hand forward over a few frames, then release (throw)
    teleop_tests.report()          # print hand / cube state

Output lines start with "[test]" (LogPython).
"""

import time
import unreal

_state = {}


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def _actors(cls):
    return unreal.GameplayStatics.get_all_actors_of_class(_world(), cls)


def _hand(left=False):
    for h in _actors(unreal.RemoteHand):
        if h.is_left() == left:
            return h
    raise RuntimeError("no RemoteHand in the PIE world")


def _cube_class():
    return unreal.load_class(None, "/Game/XRFramework/Blueprints/BP_Grabbable_SmallCube.BP_Grabbable_SmallCube_C")


def _log(msg):
    unreal.log(f"[test] {msg}")


def _vec(v):
    return f"({v.x:.1f}, {v.y:.1f}, {v.z:.1f})"


def grab_begin(left=False):
    hand = _hand(left)
    station = hand.get_actor_location()
    cubes = sorted(_actors(_cube_class()), key=lambda c: (c.get_actor_location() - station).length())
    cube = cubes[0]
    _state["cube"] = cube
    _state["hand"] = hand
    pos = cube.get_actor_location()
    hand.debug_set_pose_override(unreal.Transform(pos, unreal.Rotator(0, 0, 0), unreal.Vector(1, 1, 1)))
    _log(f"hand override at cube {cube.get_name()} {_vec(pos)}")
    # The override applies on the hand's next tick; grab on the next call (grab_now) or after a frame.
    _state["pending_grab"] = True


def grab_now():
    hand = _state["hand"]
    ok = hand.debug_grab()
    held = hand.get_held_actor()
    _log(f"grab ok={ok} held={held.get_name() if held else None}")


def lift(dz=30.0):
    hand = _state["hand"]
    pos = hand.get_actor_location() + unreal.Vector(0, 0, dz)
    hand.debug_set_pose_override(unreal.Transform(pos, unreal.Rotator(0, 0, 0), unreal.Vector(1, 1, 1)))
    _log(f"hand -> {_vec(pos)}")


def throw(velocity=(250.0, 0.0, 150.0), duration=0.25):
    """Moves the hand at a constant velocity (cm/s) for `duration` s, frame by frame, then releases."""
    hand = _state["hand"]
    start = hand.get_actor_location()
    v = unreal.Vector(*velocity)
    t0 = time.time()

    def tick(dt):
        t = time.time() - t0
        pos = start + v * min(t, duration)
        hand.debug_set_pose_override(unreal.Transform(pos, unreal.Rotator(0, 0, 0), unreal.Vector(1, 1, 1)))
        if t >= duration and "throw_handle" in _state:
            ok = hand.debug_release()
            unreal.unregister_slate_post_tick_callback(_state.pop("throw_handle"))
            cube = _state["cube"]
            root = cube.get_editor_property("root_component")
            vel = root.get_physics_linear_velocity() if root.is_simulating_physics() else None
            _log(f"released ok={ok} at {_vec(pos)}; cube {_vec(cube.get_actor_location())} simulating={root.is_simulating_physics()} vel={_vec(vel) if vel else None}")

    _state["throw_handle"] = unreal.register_slate_post_tick_callback(tick)
    _log(f"throwing from {_vec(start)} with v={_vec(v)} for {duration}s")


def report():
    hand = _state.get("hand") or _hand(False)
    held = hand.get_held_actor()
    _log(f"hand at {_vec(hand.get_actor_location())} held={held.get_name() if held else None}")
    cube = _state.get("cube")
    if cube:
        root = cube.get_editor_property("root_component")
        _log(f"cube at {_vec(cube.get_actor_location())} simulating={root.is_simulating_physics()} attach_parent={root.get_attach_parent().get_name() if root.get_attach_parent() else None}")


def release_override():
    hand = _state.get("hand") or _hand(False)
    hand.debug_clear_pose_override()
    _log("hand override cleared")
