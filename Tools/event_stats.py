"""P06 AnimEventClock - timing error and missed / duplicate events, per path, from one benchmark run's CSV files.

Usage:
    python -I event_stats.py <run prefix>            e.g. ...\\Benchmarks\\Raw\\2026-12-01_101500_m1_NQ_uro4_f60_r1
    python -I event_stats.py --selftest

A run writes three files (the benchmark director, UAECBenchSubsystem::WriteRunFiles):
    <prefix>_actions.csv   one line per action: character, band, montage, play rate, start, stop, in_capture
    <prefix>_expected.csv  one line per expected event: action, key, due game time, interrupted (a window closed by a stop)
    <prefix>_events.csv    one line per fire: action, key, path (NQ / NB / T / AEC), game time, wall clock, frame
Each file starts with one "# key=value ..." line describing the run.

What is measured (01 §6.3), over actions started inside the capture window only:
    error_ms        fire game time - due game time, for the FIRST fire of each expected event on each path
    missed          expected events a path never fired
    duplicates      fires of an expected event after its first, on the same path
    unexpected      fires of an event that was not expected (for example after an interruption)
    bunched_share   share of first fires that landed in the same frame as another key of the same action
Statistics: median, p95, p99 with linear-interpolation percentiles (as UE5-UpdateBudget's csv_stats.py), plus min and max,
because "never early" (min >= 0) is a pass/fail property (SC1). Errors are also given in frames when the run used a fixed step.
"""
from __future__ import annotations

import collections
import csv
import json
import math
import os
import sys
import tempfile


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
    """Count, min, median, p95, p99 and max of a list of numbers (empty list: count 0 only)."""
    if not values:
        return {"count": 0}
    ordered = sorted(values)
    return {"count": len(ordered), "min": ordered[0], "median": percentile(ordered, 50), "p95": percentile(ordered, 95),
            "p99": percentile(ordered, 99), "max": ordered[-1]}


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


def analyse(prefix: str) -> dict:
    """All metrics of one run, per path and per (path, band)."""
    header, actions = read_csv(prefix + "_actions.csv")
    _, expected = read_csv(prefix + "_expected.csv")
    _, events = read_csv(prefix + "_events.csv")

    in_capture = {int(a["action"]): int(a["band"]) for a in actions if a["in_capture"] == "1"}
    due = {(int(e["action"]), int(e["key"])): float(e["due_game"]) for e in expected if int(e["action"]) in in_capture}
    # Window ends closed by an interruption are due at the stop; the animation closes them only after its blend-out.
    # They are a different question (how a stop is handled), so they get their own bucket and never mix into the bands.
    interrupted = {(int(e["action"]), int(e["key"])) for e in expected if int(e["action"]) in in_capture and e["interrupted"] == "1"}

    fires: dict[tuple, list[tuple[float, int]]] = {}   # (path, action, key) -> [(game time, frame)]
    paths = set()
    for event in events:
        action = int(event["action"])
        if action not in in_capture:
            continue
        paths.add(event["path"])
        fires.setdefault((event["path"], action, int(event["key"])), []).append((float(event["game"]), int(event["frame"])))

    step = header.get("fixedstep", "off")
    frame_ms = float(step) * 1000.0 if step not in ("off", "") else None

    result = {"run": header, "expected": len(due) - len(interrupted), "interrupted_ends": len(interrupted),
              "actions_in_capture": len(in_capture), "paths": {}}
    for path in sorted(paths):
        per_band: dict[str, dict] = {}
        first_frames: dict[int, list[int]] = {}   # action -> frames of its first fires
        for band_key in ("all", "0", "1", "2", "interrupted_end"):
            per_band[band_key] = {"errors": [], "missed": 0, "duplicates": 0, "expected": 0}
        for (action, key), due_time in due.items():
            hits = sorted(fires.get((path, action, key), []))
            buckets = (per_band["interrupted_end"],) if (action, key) in interrupted else (per_band["all"], per_band[str(in_capture[action])])
            for bucket in buckets:
                bucket["expected"] += 1
                if not hits:
                    bucket["missed"] += 1
                else:
                    bucket["errors"].append((hits[0][0] - due_time) * 1000.0)
                    bucket["duplicates"] += len(hits) - 1
            if hits and (action, key) not in interrupted:
                first_frames.setdefault(action, []).append(hits[0][1])
        unexpected = sum(len(times) for (p, action, key), times in fires.items() if p == path and (action, key) not in due)
        first_count = sum(len(frames) for frames in first_frames.values())
        bunched_fires = sum(count for frames in first_frames.values() for count in collections.Counter(frames).values() if count > 1)
        summary = {}
        for band_key, bucket in per_band.items():
            if bucket["expected"] == 0:
                continue
            stats = summarize(bucket["errors"])
            if frame_ms and stats["count"]:
                stats.update({f"{k}_frames": stats[k] / frame_ms for k in ("min", "median", "p95", "p99", "max")})
            summary[band_key] = {
                "expected": bucket["expected"],
                "missed": bucket["missed"],
                "duplicates": bucket["duplicates"],
                "missed_per_1000": 1000.0 * bucket["missed"] / bucket["expected"],
                "duplicates_per_1000": 1000.0 * bucket["duplicates"] / bucket["expected"],
                "error_ms": stats,
            }
        summary["all"]["unexpected"] = unexpected
        summary["all"]["bunched_share"] = (bunched_fires / first_count) if first_count else 0.0
        result["paths"][path] = summary
    return result


def selftest() -> int:
    """Hand-made run with known answers: 2 actions in capture (1 in band 0, 1 in band 2) + 1 outside, 60 fps."""
    assert percentile([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], 50) == 5.5
    assert abs(percentile([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], 95) - 9.55) < 1e-9
    header = "# run=self fixedstep=0.016667\n"
    with tempfile.TemporaryDirectory() as folder:
        prefix = os.path.join(folder, "self")
        with open(prefix + "_actions.csv", "w", encoding="utf-8") as handle:
            handle.write(header + "action,char,band,montage,rate,start_game,start_wall,start_frame,stop_game,in_capture\n"
                         "0,0,0,AM_Attack_Q,1.000,10.0,0,600,-1,1\n1,1,2,AM_Attack_Q,1.000,10.0,0,600,-1,1\n2,2,1,AM_Attack_Q,1.000,1.0,0,60,-1,0\n")
        with open(prefix + "_expected.csv", "w", encoding="utf-8") as handle:
            handle.write(header + "action,key,name,phase,position,due_game,interrupted\n"
                         "0,0,Hit,instant,0.5,10.5,0\n0,1,Hit2,instant,0.7,10.7,0\n1,0,Hit,instant,0.5,10.5,0\n1,1,Hit2,instant,0.7,10.7,0\n"
                         "2,0,Hit,instant,0.5,1.5,0\n0,2,Win,end,0.9,10.6,1\n")
        with open(prefix + "_events.csv", "w", encoding="utf-8") as handle:
            handle.write(header + "action,key,name,phase,path,game,wall,frame\n"
                         # action 0: on time (+10 ms) and late by 50 ms, same frame -> bunched
                         "0,0,Hit,instant,NQ,10.510,0,631\n0,1,Hit2,instant,NQ,10.750,0,631\n"
                         # action 1: key 0 twice (one duplicate), key 1 missed
                         "1,0,Hit,instant,NQ,10.490,0,630\n1,0,Hit,instant,NQ,10.520,0,632\n"
                         # action 2 is outside the capture window: ignored
                         "2,0,Hit,instant,NQ,1.5,0,90\n"
                         # action 0: a window end closed by an interruption, fired 100 ms after the stop: own bucket only
                         "0,2,Win,end,NQ,10.700,0,642\n")
        result = analyse(prefix)
    nq = result["paths"]["NQ"]["all"]
    assert result["expected"] == 4 and result["actions_in_capture"] == 2, result
    assert nq["missed"] == 1 and nq["duplicates"] == 1, nq
    assert nq["missed_per_1000"] == 250.0 and nq["duplicates_per_1000"] == 250.0, nq
    errors = nq["error_ms"]
    assert errors["count"] == 3 and abs(errors["min"] - (-10.0)) < 1e-6 and abs(errors["max"] - 50.0) < 1e-6, errors
    assert abs(errors["median"] - 10.0) < 1e-6, errors
    assert abs(nq["bunched_share"] - 2.0 / 3.0) < 1e-9, nq
    assert result["paths"]["NQ"]["2"]["missed"] == 1 and result["paths"]["NQ"]["0"]["missed"] == 0
    assert abs(errors["max_frames"] - 50.0 / 16.667) < 1e-3, errors
    stopped = result["paths"]["NQ"]["interrupted_end"]
    assert result["interrupted_ends"] == 1 and stopped["expected"] == 1, result
    assert abs(stopped["error_ms"]["max"] - 100.0) < 1e-6, stopped
    print("event_stats self-test passed")
    return 0


def main(argv: list[str]) -> int:
    if argv == ["--selftest"]:
        return selftest()
    if len(argv) != 1:
        print(__doc__)
        return 2
    print(json.dumps(analyse(argv[0]), indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
