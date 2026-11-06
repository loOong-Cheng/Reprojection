"""Motion x robot location x latency x method matrix of deterministic replays (FTeleopReplay).

For every configuration (motion, station, latency) it replays the motion once per method in standalone desktop
processes (like replay_run.ps1, several at a time), then scores the configuration with replay_metrics.py against its
own GT run. Frames are kept (640x360, ~1 GB per configuration) unless --delete-frames; --keep configurations also get
observer frames for GIFs. Rerunning skips configurations that already have metrics.json and only scores those whose frames
are all there, so an interrupted matrix resumes and a changed metric can be rerun without the game runs.

Layout: <out>/<motion>/<station>/<latency>/{trajectory.csv, metrics.json, metrics_frames.csv, <method>/frames.csv, *.log}.
Summary tables: Tools/replay_matrix_report.py.

Usage: python replay_matrix.py [--out DIR] [--motions turn tilt ...] [--stations desk ...] [--latencies wifi ...]
       [--parallel 2] [--metrics-python PY] [--keep desk/wifi ...]
"""

import argparse
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import replay_trajectory  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
ENGINE = r"C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe"
PROJECT = os.path.join(ROOT, "Reprojection", "Reprojection.uproject")

# Robot stations (x, y, z cm, yaw deg); the props stay by the desk. Chosen from probe runs:
STATIONS = {
    "desk": "60,0,0,0",         # default: 40 cm in front of the desk, near clutter 0.5-5 m
    "side": "150,-170,0,90",    # beside the desk, looking along it: layered mid-range objects
    "room": "-250,0,0,0",       # 3 m back, the desk 3.5 m away: mid to far
    "far": "-60,0,0,180",       # facing away from the desk into the open courtyard: far, dark walls 4-10 m, sky
}
# teleop.LatencyPreset (measured p50 / p95 end-to-end ms: Ideal 28 / 30, WiFi 54 / 89, Remote 94 / 190, Bad 281 / 365).
LATENCIES = {"ideal": 0, "wifi": 2, "remote": 3, "bad": 4}
METHODS = [
    ("GT", "teleop.Method=5"), ("M0", "teleop.Method=0"), ("M1", "teleop.Method=1"), ("M2", "teleop.Method=2"),
    ("M3", "teleop.Method=3"), ("M4f0", "teleop.Method=4;teleop.M4.Fill=0"), ("M4f1", "teleop.Method=4;teleop.M4.Fill=1"),
    ("M4f2", "teleop.Method=4;teleop.M4.Fill=2"),
]
COMMON = "teleop.Debug.HideHud=1;teleop.DepthPreset=1;teleop.NeckMode=4;teleop.M4.Sphere=2"
MOTIONS = ["turn", "fastturn", "biglean", "standup", "tilt", "strafe", "drive", "desk"]


def ue_args(traj, out_dir, station, latency, cvars, log, scale, obs_every):
    return [ENGINE, PROJECT, "/Game/VRTemplate/VRTemplateMap", "-game", "-nohmd", "-windowed", "-ResX=1280", "-ResY=720",
            "-nosplash", "-nosound", "-UseFixedTimeStep", "-FPS=72", "-TeleopReplay=%s" % traj, "-TeleopReplayOut=%s" % out_dir,
            "-TeleopReplayFrom=1.0", "-TeleopReplayObsEvery=%d" % obs_every, "-TeleopReplayScale=%d" % scale,
            "-TeleopStation=%s" % station, '-TeleopSet=%s;teleop.LatencyPreset=%d;%s' % (COMMON, latency, cvars),
            "-abslog=%s" % log]


def log_done(log):
    try:
        with open(log, "rb") as f:
            f.seek(0, 2)
            f.seek(max(0, f.tell() - 8192))
            tail = f.read().decode("utf-8", "ignore")
        return "[replay] done" in tail
    except OSError:
        return False


def frames_complete(d):
    """Every frame of a finished run is there (frames.csv rows = eye PNGs)."""
    try:
        with open(os.path.join(d, "frames.csv")) as f:
            rows = sum(1 for _ in f) - 1
        return rows > 0 and len([x for x in os.listdir(d) if x.startswith("eye_") and x.endswith(".png")]) == rows
    except OSError:
        return False


def log_started(log):
    try:
        return "[replay]" in open(log, encoding="utf-8", errors="ignore").read()
    except OSError:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "Reprojection", "Saved", "TeleopReplay", "matrix"))
    ap.add_argument("--motions", nargs="*", default=MOTIONS)
    ap.add_argument("--stations", nargs="*", default=list(STATIONS))
    ap.add_argument("--latencies", nargs="*", default=list(LATENCIES))
    ap.add_argument("--methods", nargs="*", default=[m for m, _ in METHODS])
    ap.add_argument("--parallel", type=int, default=2, help="game processes at a time")
    ap.add_argument("--metrics-jobs", type=int, default=2, help="metrics processes at a time")
    ap.add_argument("--metrics-python", default=sys.executable)
    ap.add_argument("--scale", type=int, default=2, help="save frames downsampled n x n (1280x720 / 2 = 640x360)")
    ap.add_argument("--keep", nargs="*", default=[], help="station/latency pairs that also get observer frames (for GIFs)")
    ap.add_argument("--delete-frames", action="store_true", help="delete the PNGs after scoring (except --keep pairs)")
    a = ap.parse_args()

    configs = [(m, s, l) for m in a.motions for s in a.stations for l in a.latencies]
    todo, score_only = [], []
    for m, s, l in configs:
        d = os.path.join(a.out, m, s, l)
        if os.path.exists(os.path.join(d, "metrics.json")):
            continue
        if all(frames_complete(os.path.join(d, name)) for name in a.methods):
            score_only.append(d)
            continue
        os.makedirs(d, exist_ok=True)
        replay_trajectory.write(os.path.join(d, "trajectory.csv"), m)
        todo.append((m, s, l, d))
    print("%d configurations, %d to score from existing frames, %d to run, %d game runs" % (
        len(configs), len(score_only), len(todo), len(todo) * len(a.methods)), flush=True)

    jobs = [(c, name, cv) for c in todo for name, cv in METHODS if name in a.methods]
    running, metrics_running, pending = [], [], {}
    remaining = {c[3]: len([1 for name, _ in METHODS if name in a.methods]) for c in todo}
    metrics_queue = list(score_only)
    failed = []
    finished = {}
    attempts = {}
    t0 = time.time()
    done_runs = 0
    while jobs or running or metrics_queue or metrics_running:
        while jobs and len(running) < a.parallel:
            (m, s, l, d), name, cv = jobs.pop(0)
            keep = ("%s/%s" % (s, l)) in a.keep
            out_dir = os.path.join(d, name)
            shutil.rmtree(out_dir, ignore_errors=True)
            log = os.path.join(d, name + ".log")
            p = subprocess.Popen(ue_args(os.path.join(d, "trajectory.csv"), out_dir, STATIONS[s], LATENCIES[l], cv, log,
                                         a.scale, 6 if keep else 0))
            running.append((p, d, name, log, time.time(), (m, s, l, d), cv))
        for item in list(running):
            p, d, name, log, start, cfg, cv = item
            if p.poll() is None:
                # Some runs finish ("[replay] done", every frame written) and then hang in the engine's shutdown: kill
                # those after a grace period. Some hang at startup (while mounting plugins, before the replay starts):
                # kill those after 2 min and retry once. Anything else is killed after 10 min.
                done_at = finished.get(log)
                if done_at is None and time.time() - start > 15 and log_done(log):
                    finished[log] = done_at = time.time()
                stuck = done_at is None and time.time() - start > 120 and not log_started(log)
                if (done_at is not None and time.time() - done_at > 20) or stuck or time.time() - start > 600:
                    p.kill()
                    p.wait()
                else:
                    continue
            running.remove(item)
            ok = log_done(log) or (os.path.exists(log) and "[replay] done" in open(log, errors="ignore").read())
            if not ok and attempts.get(log, 0) < 1:
                attempts[log] = attempts.get(log, 0) + 1
                print("RETRY", d, name, flush=True)
                jobs.insert(0, (cfg, name, cv))
                continue
            done_runs += 1
            if not ok:
                failed.append((d, name))
                print("FAILED", d, name, flush=True)
            remaining[d] -= 1
            if remaining[d] == 0:
                metrics_queue.append(d)
        while metrics_queue and len(metrics_running) < a.metrics_jobs:
            d = metrics_queue.pop(0)
            names = [n for n in a.methods if n != "GT"]
            cmd = [a.metrics_python, os.path.join(ROOT, "Tools", "replay_metrics.py"), d, "--methods", *names,
                   "--lpips-every", "6", "--threads", "4", "--workers", "4"]
            mlog = open(os.path.join(d, "metrics.log"), "w")
            metrics_running.append((subprocess.Popen(cmd, stdout=mlog, stderr=subprocess.STDOUT), d, mlog))
        for item in list(metrics_running):
            p, d, mlog = item
            if p.poll() is None:
                continue
            metrics_running.remove(item)
            mlog.close()
            rel = os.path.relpath(d, a.out).replace("\\", "/")
            keep = "/".join(rel.split("/")[1:]) in a.keep
            if p.returncode == 0 and a.delete_frames and not keep:
                for name in a.methods:  # keep frames.csv and logs, drop the images
                    md = os.path.join(d, name)
                    for f in os.listdir(md) if os.path.isdir(md) else []:
                        if f.endswith(".png"):
                            os.remove(os.path.join(md, f))
            el = time.time() - t0
            print("[%5.1f min] %s metrics %s (%d/%d game runs)" % (el / 60, rel, "ok" if p.returncode == 0 else "FAILED",
                                                                  done_runs, len(todo) * len(a.methods)), flush=True)
        time.sleep(1.0)
    print("done in %.1f min; failed runs: %s" % ((time.time() - t0) / 60, failed or "none"), flush=True)


if __name__ == "__main__":
    main()
