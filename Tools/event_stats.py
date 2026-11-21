"""P06 AnimEventClock - timing error and missed / duplicate events, per path, from one benchmark run's CSV files.

Usage:
    python -I event_stats.py <run prefix>            e.g. ...\\Benchmarks\\Raw\\2026-12-01_101500_m1_NQ_uro4_f60_r1
    python -I event_stats.py --keys <run prefix> <path>   per key, band and play rate (a diagnostic for one run)
    python -I event_stats.py --net <server prefix> [<client prefix>]   one network run (S4)
    python -I event_stats.py --selftest
A bare run name (no folder) is looked up in Benchmarks/Raw.

A run writes these files (the benchmark director, UAECBenchSubsystem::WriteRunFiles):
    <prefix>_actions.csv   one line per action: character, band, montage, play rate, start, stop, in_capture, role
    <prefix>_expected.csv  one line per expected event: action, key, phase, due game time, interrupted (a window closed by a
                           stop). Not written by a network client: its expected events are the server's
    <prefix>_events.csv    one line per fire: action, key, path (NQ / NB / G / T / AEC), game time, wall clock, frame
    <prefix>_frames.csv    network runs only: one line per frame, its game time and the wall-clock moment it stands for
Each file starts with one "# key=value ..." line describing the run. In every file, "action" is the server's action id.

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

Network runs (S4, raw notes E0) run in real time: every fire is compared on the shared wall clock, each due time (server
game time) mapped to the wall-clock moment the server's frame log gives for it (linear interpolation between the two
frames around it). The server's own fires are also measured in its game time, like a single-process run. The groups are
roles instead of bands: npc and remote (a remote player's character) on the server, sim and auto on the client.
"""
from __future__ import annotations

import bisect
import collections
import csv
import json
import math
import os
import sys
import tempfile

FIXED_BUCKETS = ("all", "instant+begin", "end", "interrupted_end")   # every other bucket of measure() is a group
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
    group_name = {key: str(group_of(*key)) for key in due}
    action_group = {action: name for (action, _), name in group_name.items()}
    groups = sorted(set(group_name.values()))
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
                chosen = (buckets["all"], buckets[group_name[(action, key)]], buckets["end" if phase == "end" else "instant+begin"])
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
                if action in action_group:   # per action: right for bands and roles (keys() prints no unexpected count)
                    buckets[action_group[action]]["unexpected"] += len(times)
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
                "unexpected": bucket["unexpected"],
                "error_ms": stats,
            }
        if "all" in summary:
            summary["all"]["bunched_share"] = (bunched_fires / first_count) if first_count else 0.0
        result[path] = summary
    return result


def load_fires(events: list[dict], in_capture: dict, clock: str = "game") -> dict:
    """(path, action, key) -> [(fire time on this clock, frame)] for the actions in capture."""
    fires: dict[tuple, list[tuple[float, int]]] = {}
    for event in events:
        action = int(event["action"])
        if action in in_capture:
            fires.setdefault((event["path"], action, int(event["key"])), []).append((float(event[clock]), int(event["frame"])))
    return fires


def load_run(prefix: str):
    """(header, actions in capture by id, due, interrupted, phase_of, events) of one run that has an expected file."""
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
    return header, in_capture, due, interrupted, phase_of, events


def analyse(prefix: str) -> dict:
    """All metrics of one single-process run, per path and per (path, bucket)."""
    header, in_capture, due, interrupted, phase_of, events = load_run(prefix)
    fires = load_fires(events, in_capture)
    paths = set()
    if "config" in header:
        paths = {header["config"], "T"} | ({"AEC"} if header.get("clock") == "1" else set())   # AEC: a run with -AECClock
    return {"run": header, "expected": len(due) - len(interrupted), "interrupted_ends": len(interrupted),
            "actions_in_capture": len(in_capture),
            "paths": measure(due, interrupted, lambda action, key: in_capture[action]["band"], phase_of, fires, paths, frame_ms_of(header))}


def keys(prefix: str, path: str) -> list[str]:
    """Per (montage, key, phase, band, play rate): expected, missed and error min / median / max in ms, on one path."""
    header, in_capture, due, interrupted, phase_of, events = load_run(prefix)
    fires = load_fires(events, in_capture)
    name_of = {(int(row["action"]), int(row["key"])): row["name"] for row in read_csv(prefix + "_expected.csv")[1]}

    def label(action: int, key: int) -> str:
        a = in_capture[action]
        return f"{a['montage']} {name_of[(action, key)]} {phase_of[(action, key)]} band {a['band']} rate {a['rate']}"

    per_key = measure(due, interrupted, label, phase_of, fires, (path,)).get(path, {})
    lines = []
    for name, bucket in per_key.items():
        if name in FIXED_BUCKETS:
            continue
        errors = bucket["error_ms"]
        spread = f"{errors['min']:.1f} / {errors['median']:.1f} / {errors['max']:.1f}" if errors["count"] else "-"
        lines.append(f"{name}: {bucket['expected']} expected, {bucket['missed']} missed, error min/median/max ms {spread}")
    return lines


def wall_mapper(frames: list[dict]):
    """A function: server game time -> the wall-clock moment the server's frame log gives for it (linear interpolation)."""
    games = [float(f["game"]) for f in frames]
    walls = [float(f["wall"]) for f in frames]
    if len(games) < 2:
        raise ValueError("a frame log needs at least two frames")

    def to_wall(game: float) -> float:
        index = min(max(bisect.bisect_left(games, game), 1), len(games) - 1)
        g0, g1, w0, w1 = games[index - 1], games[index], walls[index - 1], walls[index]
        return w0 + (w1 - w0) * (game - g0) / (g1 - g0) if g1 > g0 else w1
    return to_wall


def frame_intervals(frames: list[dict]) -> dict:
    """Frame-to-frame wall-clock intervals of one process, in ms (a server below its tick rate shows here)."""
    walls = [float(f["wall"]) for f in frames]
    return summarize([(b - a) * 1000.0 for a, b in zip(walls, walls[1:])])


def analyse_net(server_prefix: str, client_prefix: str | None = None) -> dict:
    """All metrics of one network run: the server's fires per role (game time and wall clock), the client's per role
    (wall clock), and each process's frame intervals."""
    header, in_capture, due_game, interrupted, phase_of, events = load_run(server_prefix)
    server_frames = read_csv(server_prefix + "_frames.csv")[1]
    to_wall = wall_mapper(server_frames)
    due_wall = {key: to_wall(time) for key, time in due_game.items()}
    paths = {header["config"], "T"}

    def server_role(action: int, key: int) -> str:
        return in_capture[action]["role"]

    result = {"run": header, "expected": len(due_game) - len(interrupted), "interrupted_ends": len(interrupted),
              "actions_in_capture": len(in_capture), "frames": {"server": frame_intervals(server_frames)},
              "server": {"game": measure(due_game, interrupted, server_role, phase_of, load_fires(events, in_capture), paths),
                         "wall": measure(due_wall, interrupted, server_role, phase_of, load_fires(events, in_capture, "wall"), paths)}}
    if client_prefix:
        client_header, client_actions = read_csv(client_prefix + "_actions.csv")
        client_fires = load_fires(read_csv(client_prefix + "_events.csv")[1], in_capture, "wall")
        # The client's role of each character (sim or auto); an action it never received is still expected of it.
        char_role = {a["char"]: a["role"] for a in client_actions}

        def client_role(action: int, key: int) -> str:
            return char_role.get(in_capture[action]["char"], "unreceived")

        result["client_run"] = client_header
        result["actions_received"] = sum(1 for a in client_actions if int(a["action"]) in in_capture)
        result["frames"]["client"] = frame_intervals(read_csv(client_prefix + "_frames.csv")[1])
        result["client"] = {"wall": measure(due_wall, interrupted, client_role, phase_of, client_fires, paths)}
    return result


def write(path: str, text: str) -> None:
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


def selftest() -> int:
    """Hand-made runs with known answers: a single-process run (2 actions in capture, 1 in band 0 and 1 in band 2, + 1
    outside, 60 fps) and a network run (server + client)."""
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

        # Network: server frames every 0.1 s (game 10.0 + 0.1 i at wall 100.0 + 0.1 i); one NPC action, one remote player's.
        actions_head = "action,char,band,montage,rate,start_game,start_wall,start_frame,stop_game,in_capture,role\n"
        expected_head = "action,key,name,phase,position,due_game,interrupted\n"
        events_head = "action,key,name,phase,path,game,wall,frame\n"
        server_header = "# run=srv config=NQ net=dedicated fixedstep=off\n"
        server = os.path.join(folder, "srv")
        write(server + "_actions.csv", server_header + actions_head
              + "0,0,0,AM_Attack_Q,1.000,10.0,100.0,300,-1,1,npc\n1,16,3,AM_Attack_Q,1.000,10.0,100.0,300,-1,1,remote\n")
        write(server + "_expected.csv", server_header + expected_head
              + "0,0,Hit,instant,0.5,10.5,0\n0,1,Hit2,instant,0.7,10.7,0\n1,0,Win,begin,0.5,10.5,0\n")
        write(server + "_events.csv", server_header + events_head
              + "0,0,Hit,instant,NQ,10.500,100.510,315\n0,1,Hit2,instant,NQ,10.700,100.705,321\n"
              # the remote player's window begins 100 ms late (game time), twice: a window duplicate (H7)
              + "1,0,Win,begin,NQ,10.600,100.620,318\n1,0,Win,begin,NQ,10.700,100.720,321\n")
        write(server + "_frames.csv", server_header + "frame,game,wall\n"
              + "".join(f"{300 + i},{10.0 + 0.1 * i:.6f},{100.0 + 0.1 * i:.6f}\n" for i in range(12)))
        client = os.path.join(folder, "cli")
        client_header = "# run=cli config=NQ net=client fixedstep=off\n"
        write(client + "_actions.csv", client_header + actions_head
              + "0,0,0,AM_Attack_Q,1.000,0.05,100.05,5,-1,0,sim\n1,16,3,AM_Attack_Q,1.000,0.05,100.05,5,-1,0,auto\n")
        write(client + "_events.csv", client_header + events_head
              # the simulated proxy fires 60 ms after the due moment and misses key 1; the autonomous proxy fires 50 ms late
              + "0,0,Hit,instant,NQ,0.55,100.560,20\n1,0,Win,begin,NQ,0.55,100.550,20\n")
        write(client + "_frames.csv", client_header + "frame,game,wall\n0,0.0,100.0\n1,0.016,100.016\n2,0.033,100.033\n")
        net = analyse_net(server, client)
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
    game, wall = net["server"]["game"]["NQ"], net["server"]["wall"]["NQ"]
    assert net["expected"] == 3 and net["actions_received"] == 2, net
    assert abs(game["npc"]["error_ms"]["max"]) < 1e-6 and abs(wall["npc"]["error_ms"]["max"] - 10.0) < 1e-6, (game, wall)
    assert abs(wall["npc"]["error_ms"]["min"] - 5.0) < 1e-6, wall
    assert abs(game["remote"]["error_ms"]["max"] - 100.0) < 1e-6 and game["remote"]["duplicates"] == 1, game
    assert game["remote"]["window_duplicates"] == 1 and game["npc"]["window_duplicates"] == 0, game
    sim, auto = net["client"]["wall"]["NQ"]["sim"], net["client"]["wall"]["NQ"]["auto"]
    assert sim["missed"] == 1 and abs(sim["error_ms"]["max"] - 60.0) < 1e-6, sim
    assert auto["missed"] == 0 and abs(auto["error_ms"]["max"] - 50.0) < 1e-6, auto
    assert net["client"]["wall"]["T"]["all"]["missed"] == 3, net["client"]["wall"]["T"]
    assert abs(net["frames"]["server"]["median"] - 100.0) < 1e-6 and net["frames"]["client"]["count"] == 2, net["frames"]
    print("event_stats self-test passed")
    return 0


def main(argv: list[str]) -> int:
    if argv == ["--selftest"]:
        return selftest()
    if argv[:1] == ["--net"] and len(argv) in (2, 3):
        print(json.dumps(analyse_net(*(run_prefix(name) for name in argv[1:])), indent=2))
        return 0
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
