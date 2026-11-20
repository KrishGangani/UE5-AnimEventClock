"""P06 AnimEventClock - timing error and missed / duplicate events, per path, from one benchmark run's CSV files.

Usage:
    python -I event_stats.py <run prefix>            e.g. ...\\Benchmarks\\Raw\\2026-12-01_101500_m1_NQ_uro4_f60_r1
    python -I event_stats.py --keys <run prefix> <path>   per key, band and play rate (a diagnostic for one run)
    python -I event_stats.py --selftest
A bare run name (no folder) is looked up in Benchmarks/Raw.

A run writes three files (the benchmark director, UAECBenchSubsystem::WriteRunFiles):
    <prefix>_actions.csv   one line per action: character, band, montage, play rate, start, stop, in_capture
    <prefix>_expected.csv  one line per expected event: action, key, phase, due game time, interrupted (a window closed by a stop)
    <prefix>_events.csv    one line per fire: action, key, path (NQ / NB / T / AEC), game time, wall clock, frame
Each file starts with one "# key=value ..." line describing the run.

What is measured (01 §6.3), over actions started inside the capture window only:
    error_ms           fire game time - due game time, for the FIRST fire of each expected event on each path
    missed             expected events a path never fired
    duplicates         fires of an expected event after its first, on the same path
    window_duplicates  the duplicates that are window begins or ends (what UE-308913 reports, 01 H7)
    unexpected         fires of an event that was not expected (for example after an interruption)
    bunched_share      share of first fires that landed in the same frame as another key of the same action
Statistics: median, p95, p99 with linear-interpolation percentiles (as UE5-UpdateBudget's csv_stats.py), plus min and max,
because "never early" (min >= 0) is a pass/fail property (SC1), and abs_p99 (p99 of |error|), the problem gate's number
(01 §6.4). Errors are also given in frames when the run used a fixed step.

Buckets, per path: "all"; one per band (0 near, 1 far, 2 behind the camera); "instant+begin" and "end", because Queued
window ends are 1-2 frames late even at defaults (09 S2-d) and would hide what a condition does to the other events; and
"interrupted_end" (window ends closed by a stop, a different question, never mixed into the others). A run is always
measured on its config's animation path and on T, so a path that fired nothing still appears, with every event missed.
"""
from __future__ import annotations

import collections
import csv
import json
import math
import os
import sys
import tempfile

RAW_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Benchmarks", "Raw")
WINDOW_PHASES = ("begin", "end")


def percentile(sorted_values: list[float], p: float) -> float:
    """Linear-interpolation percentile of an already sorted, non-empty list. p in [0, 100]."""
    if not sorted_values:
        raise ValueError("percentile of an empty list")
    if len(sorted_values) == 1:
        return sorted_values[0]
    position = (p / 100.0) * (len(sorted_values) - 1)
    lower = math.floor(position)
    upper = min(lower + 1, len(sorted_values) - 1)
    fraction = position - lower
    return sorted_values[lower] + (sorted_values[upper] - sorted_values[lower]) * fraction


def summarize(values: list[float]) -> dict:
    """Count, min, median, p95, p99, max and the p99 of |value| of a list of numbers (empty list: count 0 only)."""
    if not values:
        return {"count": 0}
    ordered = sorted(values)
    return {"count": len(ordered), "min": ordered[0], "median": percentile(ordered, 50), "p95": percentile(ordered, 95),
            "p99": percentile(ordered, 99), "max": ordered[-1], "abs_p99": percentile(sorted(abs(v) for v in values), 99)}


def read_csv(path: str) -> tuple[dict, list[dict]]:
    """Returns (the '# key=value' header as a dict, the rows as dicts)."""
    with open(path, encoding="utf-8", newline="") as handle:
        first = handle.readline()
        header = {}
        if first.startswith("#"):
            for item in first[1:].split():
                key, _, value = item.partition("=")
                header[key] = value
        else:
            handle.seek(0)
        return header, list(csv.DictReader(handle))


def run_prefix(name: str) -> str:
    """A run's file prefix: as given, or a bare run name looked up in Benchmarks/Raw."""
    return name if os.path.exists(name + "_actions.csv") else os.path.join(RAW_DIR, name)


def frame_ms_of(header: dict) -> float | None:
    """The fixed step in ms, or None for a run without one."""
    step = header.get("fixedstep", "off")
    return float(step) * 1000.0 if step not in ("off", "") else None


def measure(due: dict, interrupted: set, group_of, phase_of: dict, fires: dict, paths=(), frame_ms: float | None = None) -> dict:
    """Per path (every path in `paths` plus every path that fired), the metrics of the module docstring per bucket.

    due:         (action, key) -> due time, for the expected events of actions in capture
    interrupted: the (action, key) of window ends closed by an interruption
    group_of:    (action, key) -> a group name (band, role, or a per-key label)
    phase_of:    (action, key) -> "instant" / "begin" / "end"
    fires:       (path, action, key) -> [(fire time, frame)], on the same clock as due
    """
    groups = sorted({str(group_of(action, key)) for action, key in due})
    result = {}
    for path in sorted(set(paths) | {path for path, _, _ in fires}):
        buckets = {name: {"errors": [], "missed": 0, "duplicates": 0, "window_duplicates": 0, "unexpected": 0, "expected": 0}
                   for name in ("all", *groups, "instant+begin", "end", "interrupted_end")}
        first_frames: dict[int, list[int]] = {}   # action -> frames of its first fires
        for (action, key), due_time in due.items():
            hits = sorted(fires.get((path, action, key), []))
            phase = phase_of.get((action, key))
            if (action, key) in interrupted:
                chosen = (buckets["interrupted_end"],)
            else:
                chosen = (buckets["all"], buckets[str(group_of(action, key))], buckets["end" if phase == "end" else "instant+begin"])
            for bucket in chosen:
                bucket["expected"] += 1
                if not hits:
                    bucket["missed"] += 1
                else:
                    bucket["errors"].append((hits[0][0] - due_time) * 1000.0)
                    bucket["duplicates"] += len(hits) - 1
                    if phase in WINDOW_PHASES:
                        bucket["window_duplicates"] += len(hits) - 1
            if hits and (action, key) not in interrupted:
                first_frames.setdefault(action, []).append(hits[0][1])
        for (p, action, key), times in fires.items():
            if p == path and (action, key) not in due:
                buckets["all"]["unexpected"] += len(times)
        first_count = sum(len(frames) for frames in first_frames.values())
        bunched_fires = sum(count for frames in first_frames.values() for count in collections.Counter(frames).values() if count > 1)
        summary = {}
        for name, bucket in buckets.items():
            if bucket["expected"] == 0:
                continue
            stats = summarize(bucket["errors"])
            if frame_ms and stats["count"]:
                stats.update({f"{k}_frames": stats[k] / frame_ms for k in ("min", "median", "p95", "p99", "max", "abs_p99")})
            summary[name] = {
                "expected": bucket["expected"],
                "missed": bucket["missed"],
                "duplicates": bucket["duplicates"],
                "missed_per_1000": 1000.0 * bucket["missed"] / bucket["expected"],
                "duplicates_per_1000": 1000.0 * bucket["duplicates"] / bucket["expected"],
                "window_duplicates": bucket["window_duplicates"],
                "error_ms": stats,
            }
        if "all" in summary:
            summary["all"]["unexpected"] = buckets["all"]["unexpected"]
            summary["all"]["bunched_share"] = (bunched_fires / first_count) if first_count else 0.0
        result[path] = summary
    return result


def load_run(prefix: str):
    """(header, actions in capture by id, due, interrupted, phase_of, fires on game time) of one single-process run."""
    header, actions = read_csv(prefix + "_actions.csv")
    _, expected = read_csv(prefix + "_expected.csv")
    _, events = read_csv(prefix + "_events.csv")
    in_capture = {int(a["action"]): a for a in actions if a["in_capture"] == "1"}
    due, interrupted, phase_of = {}, set(), {}
    for row in expected:
        action = int(row["action"])
        if action in in_capture:
            key = (action, int(row["key"]))
            due[key] = float(row["due_game"])
            phase_of[key] = row["phase"]
            if row["interrupted"] == "1":
                interrupted.add(key)
    fires: dict[tuple, list[tuple[float, int]]] = {}
    for event in events:
        action = int(event["action"])
        if action in in_capture:
            fires.setdefault((event["path"], action, int(event["key"])), []).append((float(event["game"]), int(event["frame"])))
    return header, in_capture, due, interrupted, phase_of, fires


def analyse(prefix: str) -> dict:
    """All metrics of one single-process run, per path and per (path, bucket)."""
    header, in_capture, due, interrupted, phase_of, fires = load_run(prefix)
    paths = {header["config"], "T"} if "config" in header else ()
    return {"run": header, "expected": len(due) - len(interrupted), "interrupted_ends": len(interrupted),
            "actions_in_capture": len(in_capture),
            "paths": measure(due, interrupted, lambda action, key: in_capture[action]["band"], phase_of, fires, paths, frame_ms_of(header))}


def keys(prefix: str, path: str) -> list[str]:
    """Per (montage, key, phase, band, play rate): expected, missed and error min / median / max in ms, on one path."""
    header, in_capture, due, interrupted, phase_of, fires = load_run(prefix)
    _, expected = read_csv(prefix + "_expected.csv")
    name_of = {(int(row["action"]), int(row["key"])): row["name"] for row in expected}

    def label(action: int, key: int) -> str:
        a = in_capture[action]
        return f"{a['montage']} {name_of[(action, key)]} {phase_of[(action, key)]} band {a['band']} rate {a['rate']}"

    per_key = measure(due, interrupted, label, phase_of, fires, (path,)).get(path, {})
    lines = []
    for name, bucket in per_key.items():
        if name in ("all", "instant+begin", "end", "interrupted_end"):
            continue
        errors = bucket["error_ms"]
        spread = f"{errors['min']:.1f} / {errors['median']:.1f} / {errors['max']:.1f}" if errors["count"] else "-"
        lines.append(f"{name}: {bucket['expected']} expected, {bucket['missed']} missed, error min/median/max ms {spread}")
    return lines


def write(path: str, text: str) -> None:
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


def selftest() -> int:
    """Hand-made run with known answers: 2 actions in capture (1 in band 0, 1 in band 2) + 1 outside, 60 fps."""
    assert percentile([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], 50) == 5.5
    assert abs(percentile([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], 95) - 9.55) < 1e-9
    assert summarize([-3.0, 1.0, 2.0])["abs_p99"] == percentile([1.0, 2.0, 3.0], 99)
    header = "# run=self config=NQ fixedstep=0.016667\n"
    with tempfile.TemporaryDirectory() as folder:
        prefix = os.path.join(folder, "self")
        write(prefix + "_actions.csv", header + "action,char,band,montage,rate,start_game,start_wall,start_frame,stop_game,in_capture\n"
              "0,0,0,AM_Attack_Q,1.000,10.0,0,600,-1,1\n1,1,2,AM_Attack_Q,1.000,10.0,0,600,-1,1\n2,2,1,AM_Attack_Q,1.000,1.0,0,60,-1,0\n")
        write(prefix + "_expected.csv", header + "action,key,name,phase,position,due_game,interrupted\n"
              "0,0,Hit,instant,0.5,10.5,0\n0,1,Hit2,instant,0.7,10.7,0\n1,0,Hit,instant,0.5,10.5,0\n1,1,Hit2,instant,0.7,10.7,0\n"
              "2,0,Hit,instant,0.5,1.5,0\n0,2,Win,end,0.9,10.6,1\n")
        write(prefix + "_events.csv", header + "action,key,name,phase,path,game,wall,frame\n"
              # action 0: on time (+10 ms) and late by 50 ms, same frame -> bunched
              "0,0,Hit,instant,NQ,10.510,0,631\n0,1,Hit2,instant,NQ,10.750,0,631\n"
              # action 1: key 0 twice (one duplicate), key 1 missed
              "1,0,Hit,instant,NQ,10.490,0,630\n1,0,Hit,instant,NQ,10.520,0,632\n"
              # action 2 is outside the capture window: ignored
              "2,0,Hit,instant,NQ,1.5,0,90\n"
              # action 0: a window end closed by an interruption, fired 100 ms after the stop: own bucket only
              "0,2,Win,end,NQ,10.700,0,642\n")
        result = analyse(prefix)
        key_lines = keys(prefix, "NQ")
    nq = result["paths"]["NQ"]["all"]
    assert result["expected"] == 4 and result["actions_in_capture"] == 2, result
    assert nq["missed"] == 1 and nq["duplicates"] == 1 and nq["window_duplicates"] == 0, nq
    assert nq["missed_per_1000"] == 250.0 and nq["duplicates_per_1000"] == 250.0, nq
    errors = nq["error_ms"]
    assert errors["count"] == 3 and abs(errors["min"] - (-10.0)) < 1e-6 and abs(errors["max"] - 50.0) < 1e-6, errors
    assert abs(errors["median"] - 10.0) < 1e-6 and abs(errors["abs_p99"] - percentile([10.0, 10.0, 50.0], 99)) < 1e-6, errors
    assert abs(nq["bunched_share"] - 2.0 / 3.0) < 1e-9, nq
    assert result["paths"]["NQ"]["2"]["missed"] == 1 and result["paths"]["NQ"]["0"]["missed"] == 0
    assert result["paths"]["NQ"]["instant+begin"]["expected"] == 4 and "end" not in result["paths"]["NQ"], result
    assert abs(errors["max_frames"] - 50.0 / 16.667) < 1e-3, errors
    stopped = result["paths"]["NQ"]["interrupted_end"]
    assert result["interrupted_ends"] == 1 and stopped["expected"] == 1, result
    assert abs(stopped["error_ms"]["max"] - 100.0) < 1e-6, stopped
    # T never fired in this run, but it is the run's second path: every expected event counts as missed there.
    assert result["paths"]["T"]["all"]["missed"] == 4 and result["paths"]["T"]["all"]["error_ms"]["count"] == 0, result
    assert any(line.startswith("AM_Attack_Q Hit2 instant band 2 rate 1.000: 1 expected, 1 missed") for line in key_lines), key_lines
    print("event_stats self-test passed")
    return 0


def main(argv: list[str]) -> int:
    if argv == ["--selftest"]:
        return selftest()
    if argv[:1] == ["--keys"] and len(argv) == 3:
        print("\n".join(keys(run_prefix(argv[1]), argv[2])))
        return 0
    if len(argv) != 1:
        print(__doc__)
        return 2
    print(json.dumps(analyse(run_prefix(argv[0])), indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
