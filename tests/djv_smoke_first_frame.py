#!/usr/bin/env python3
"""Smoke test DJV playback delivery without screenshot permissions.

The DJV app prints a fixed READY line when the requested viewport roles
receive the expected frames, then exits automatically.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

if not sys.platform.startswith("win"):
    import resource


def run_quiet(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, text=True, capture_output=True, check=False)


def kill_existing(exe: Path) -> None:
    if sys.platform.startswith("win"):
        run_quiet(["taskkill", "/IM", "djv.exe", "/F"])
        run_quiet(["taskkill", "/IM", "ffmpeg.exe", "/F"])
        return

    print("Checking existing DJV processes...")
    existing = run_quiet(["pgrep", "-af", str(exe)])
    if existing.stdout.strip():
        print(existing.stdout.strip())
    run_quiet(["pkill", "-f", str(exe)])
    run_quiet(["pkill", "-x", "djv"])
    run_quiet(["pkill", "-x", "ffmpeg"])


def set_limits() -> None:
    if not sys.platform.startswith("win"):
        resource.setrlimit(resource.RLIMIT_NOFILE, (4096, 4096))


def get_metrics(pid: int) -> tuple[str, int]:
    ffmpeg_count = 0
    if sys.platform.startswith("win"):
        tasklist = run_quiet(["tasklist", "/FI", f"PID eq {pid}"]).stdout.strip()
        ffmpeg = run_quiet(["tasklist", "/FI", "IMAGENAME eq ffmpeg.exe"]).stdout
        ffmpeg_count = ffmpeg.lower().count("ffmpeg.exe")
        return tasklist.replace("\n", " | "), ffmpeg_count

    ps = run_quiet(["ps", "-p", str(pid), "-o", "rss=,pcpu=,etime="]).stdout.strip()
    ffmpeg = run_quiet(["pgrep", "-x", "ffmpeg"]).stdout.splitlines()
    ffmpeg_count = len([line for line in ffmpeg if line.strip()])
    return ps, ffmpeg_count


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", default=str(Path(__file__).resolve().parents[1]))
    parser.add_argument("--exe", default=None)
    parser.add_argument("--video", required=True)
    parser.add_argument("--video-b", default=None)
    parser.add_argument("--mode", choices=("single", "dual"), default="single")
    parser.add_argument(
        "--test",
        choices=("first-frame", "forward", "reverse", "reverse-start-guard", "seek", "speed", "dual-total"),
        default="first-frame")
    parser.add_argument("--forward-frames", type=int, default=5)
    parser.add_argument("--forward-max-step", type=int, default=4)
    parser.add_argument("--forward-max-interval", type=float, default=0.15)
    parser.add_argument("--reverse-frames", type=int, default=5)
    parser.add_argument("--reverse-start-frame", type=int, default=60)
    parser.add_argument("--seek-frame", type=int, default=45)
    parser.add_argument("--seek-tolerance", type=int, default=2)
    parser.add_argument("--speed-mult", type=float, default=1.0)
    parser.add_argument("--speed-duration", type=float, default=3.0)
    parser.add_argument("--speed-tolerance", type=float, default=0.35)
    parser.add_argument(
        "--dual-total-mode",
        choices=("forward", "reverse", "offset-forward", "offset-reverse"),
        default="forward")
    parser.add_argument("--dual-total-frames", type=int, default=12)
    parser.add_argument("--dual-total-speed-mult", type=float, default=1.0)
    parser.add_argument("--dual-total-start-frame", type=float, default=120.0)
    parser.add_argument("--dual-total-max-diff", type=float, default=None)
    parser.add_argument("--dual-total-offset-a-seconds", type=float, default=10.0)
    parser.add_argument("--dual-total-offset-b-seconds", type=float, default=0.0)
    parser.add_argument("--dual-total-offset-tolerance", type=float, default=0.35)
    parser.add_argument("--timeout", type=float, default=25.0)
    args = parser.parse_args()

    repo = Path(args.repo).resolve()
    exe = Path(args.exe).resolve() if args.exe else repo / "build-Release" / "bin" / "djv" / ("djv.exe" if sys.platform.startswith("win") else "djv")
    video = Path(args.video).resolve()
    video_b = Path(args.video_b).resolve() if args.video_b else video

    if not exe.exists():
        print(f"ERROR: executable not found: {exe}")
        return 2
    if not video.exists():
        print(f"ERROR: video not found: {video}")
        return 2
    if args.mode == "dual" and not video_b.exists():
        print(f"ERROR: B video not found: {video_b}")
        return 2

    kill_existing(exe)

    env = os.environ.copy()
    if args.mode == "dual":
        command = [str(exe), str(video), "-b", str(video_b), "-c", "horizontal"]
    else:
        command = [str(exe), str(video)]

    if args.test == "first-frame":
        env["DJV_SMOKE_FIRST_FRAME_EXIT"] = "1"
        if args.mode == "dual":
            env["DJV_SMOKE_FIRST_FRAME_COUNT"] = "2"
            env["DJV_SMOKE_FIRST_FRAME_ROLES"] = "SplitA,SplitB"
        else:
            env["DJV_SMOKE_FIRST_FRAME_COUNT"] = "1"
            env["DJV_SMOKE_FIRST_FRAME_ROLES"] = "Primary"
        ready_line = "DJV_SMOKE_FIRST_FRAME_READY"
    elif args.test == "forward":
        if args.mode != "single":
            print("ERROR: forward smoke test currently supports single mode only")
            return 2
        env["DJV_SMOKE_FORWARD_EXIT"] = "1"
        env["DJV_SMOKE_FORWARD_FRAMES"] = str(max(1, args.forward_frames))
        env["DJV_SMOKE_FORWARD_MAX_STEP"] = str(max(1, args.forward_max_step))
        env["DJV_SMOKE_FORWARD_MAX_INTERVAL"] = str(max(0.01, args.forward_max_interval))
        env["DJV_SMOKE_FORWARD_ROLES"] = "Primary"
        env["DJV_SMOKE_FIRST_FRAME_COUNT"] = "1"
        env["DJV_SMOKE_FIRST_FRAME_ROLES"] = "Primary"
        ready_line = "DJV_SMOKE_FORWARD_READY"
    elif args.test == "reverse":
        if args.mode != "single":
            print("ERROR: reverse smoke test currently supports single mode only")
            return 2
        env["DJV_SMOKE_REVERSE_EXIT"] = "1"
        env["DJV_SMOKE_REVERSE_FRAMES"] = str(max(1, args.reverse_frames))
        env["DJV_SMOKE_REVERSE_START_FRAME"] = str(max(1, args.reverse_start_frame))
        env["DJV_SMOKE_REVERSE_ROLES"] = "Primary"
        env["DJV_SMOKE_FIRST_FRAME_COUNT"] = "1"
        env["DJV_SMOKE_FIRST_FRAME_ROLES"] = "Primary"
        ready_line = "DJV_SMOKE_REVERSE_READY"
    elif args.test == "reverse-start-guard":
        if args.mode != "single":
            print("ERROR: reverse start guard smoke test currently supports single mode only")
            return 2
        env["DJV_SMOKE_REVERSE_GUARD_EXIT"] = "1"
        env["DJV_SMOKE_REVERSE_GUARD_ROLES"] = "Primary"
        ready_line = "DJV_SMOKE_REVERSE_GUARD_READY blocked=true"
    elif args.test == "seek":
        if args.mode != "single":
            print("ERROR: seek smoke test currently supports single mode only")
            return 2
        env["DJV_SMOKE_SEEK_EXIT"] = "1"
        env["DJV_SMOKE_SEEK_ROLES"] = "Primary"
        env["DJV_SMOKE_SEEK_FRAME"] = str(max(1, args.seek_frame))
        env["DJV_SMOKE_SEEK_TOLERANCE"] = str(max(0, args.seek_tolerance))
        ready_line = "DJV_SMOKE_SEEK_READY passed=true"
    elif args.test == "speed":
        if args.mode != "single":
            print("ERROR: speed smoke test currently supports single mode only")
            return 2
        env["DJV_SMOKE_SPEED_EXIT"] = "1"
        env["DJV_SMOKE_SPEED_ROLES"] = "Primary"
        env["DJV_SMOKE_SPEED_MULT"] = str(min(3.0, max(1.0 / 3.0, args.speed_mult)))
        env["DJV_SMOKE_SPEED_DURATION"] = str(max(0.5, args.speed_duration))
        env["DJV_SMOKE_SPEED_TOLERANCE"] = str(max(0.01, args.speed_tolerance))
        env["DJV_SMOKE_FIRST_FRAME_COUNT"] = "1"
        env["DJV_SMOKE_FIRST_FRAME_ROLES"] = "Primary"
        ready_line = "DJV_SMOKE_SPEED_READY"
    else:
        if args.mode != "dual":
            print("ERROR: dual total smoke test requires dual mode")
            return 2
        env["DJV_SMOKE_DUAL_TOTAL_EXIT"] = "1"
        env["DJV_SMOKE_DUAL_TOTAL_ROLES"] = "SplitA,SplitB"
        env["DJV_SMOKE_DUAL_TOTAL_MODE"] = args.dual_total_mode
        env["DJV_SMOKE_DUAL_TOTAL_FRAMES"] = str(max(1, args.dual_total_frames))
        env["DJV_SMOKE_DUAL_TOTAL_SPEED_MULT"] = str(min(3.0, max(1.0 / 3.0, args.dual_total_speed_mult)))
        env["DJV_SMOKE_DUAL_TOTAL_START_FRAME"] = str(max(1.0, args.dual_total_start_frame))
        env["DJV_SMOKE_DUAL_TOTAL_OFFSET_A_SECONDS"] = str(max(0.0, args.dual_total_offset_a_seconds))
        env["DJV_SMOKE_DUAL_TOTAL_OFFSET_B_SECONDS"] = str(max(0.0, args.dual_total_offset_b_seconds))
        env["DJV_SMOKE_DUAL_TOTAL_OFFSET_TOLERANCE"] = str(max(0.0, args.dual_total_offset_tolerance))
        if args.dual_total_max_diff is not None:
            env["DJV_SMOKE_DUAL_TOTAL_MAX_DIFF"] = str(max(0.0, args.dual_total_max_diff))
        env["DJV_SMOKE_FIRST_FRAME_COUNT"] = "2"
        env["DJV_SMOKE_FIRST_FRAME_ROLES"] = "SplitA,SplitB"
        ready_line = "DJV_SMOKE_DUAL_TOTAL_READY"

    print("Launching:", " ".join(command))
    process = subprocess.Popen(
        command,
        cwd=str(repo),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        preexec_fn=None if sys.platform.startswith("win") else set_limits,
    )
    output_lines: list[str] = []

    def read_output() -> None:
        assert process.stdout is not None
        for line in process.stdout:
            output_lines.append(line)

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()

    start = time.time()
    metrics: list[tuple[float, int, str]] = []
    while time.time() - start < args.timeout:
        if process.poll() is not None:
            break
        time.sleep(1.0)
        ps, ffmpeg_count = get_metrics(process.pid)
        metrics.append((round(time.time() - start, 1), ffmpeg_count, ps))

    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3.0)
    reader.join(timeout=3.0)
    output = "".join(output_lines)

    passed = ready_line in output
    if args.test == "forward":
        passed = passed and "DJV_SMOKE_FORWARD_EMPTY" not in output
        passed = passed and "DJV_SMOKE_FORWARD_INVALID" not in output
        passed = passed and "DJV_SMOKE_FORWARD_BLACK" not in output
        passed = passed and "DJV_SMOKE_FORWARD_JUMP" not in output
        passed = passed and "DJV_SMOKE_FORWARD_STALL" not in output
    if args.test == "reverse":
        passed = passed and "DJV_SMOKE_REVERSE_EMPTY" not in output
        passed = passed and "DJV_SMOKE_REVERSE_INVALID" not in output
    if args.test == "speed":
        passed = passed and "DJV_SMOKE_SPEED_READY" in output
        passed = passed and "passed=true" in output
    if args.test == "dual-total":
        passed = passed and "DJV_SMOKE_DUAL_TOTAL_READY" in output
        passed = passed and "readyPanes=2" in output
        if args.dual_total_max_diff is not None:
            passed = passed and "syncPassed=true" in output
        if args.dual_total_mode.startswith("offset-"):
            passed = passed and "offsetPassed=true" in output
    print("PASS:", passed)
    print("exit_code:", process.returncode)
    print("metrics:")
    for t, ffmpeg_count, ps in metrics:
        print(f"  t={t}s ffmpeg={ffmpeg_count} ps={ps}")
    issue_tokens = (
        "DJV_SMOKE_FORWARD_EMPTY",
        "DJV_SMOKE_FORWARD_INVALID",
        "DJV_SMOKE_FORWARD_BLACK",
        "DJV_SMOKE_FORWARD_JUMP",
        "DJV_SMOKE_FORWARD_STALL",
        "DJV_SMOKE_REVERSE_EMPTY",
        "DJV_SMOKE_REVERSE_INVALID",
        "DJV_SMOKE_SPEED_READY role=Primary passed=false",
        "DJV_SMOKE_DUAL_TOTAL_OFFSET_DIFF",
        "DJV_SMOKE_DUAL_TOTAL_SYNC_DIFF",
    )
    issue_lines = [
        line for line in output.splitlines()
        if any(token in line for token in issue_tokens)
    ]
    if issue_lines:
        print("issue_lines:")
        for line in issue_lines[-40:]:
            print(line)
    print("stdout_tail:")
    for line in output.splitlines()[-40:]:
        print(line)

    kill_existing(exe)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
