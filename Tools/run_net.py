"""P06 AnimEventClock - run a network timing batch (M2): one server and one client process per run, real time, emulated latency.

Usage (from anywhere; run it through the machine-wide queue, tools\\ue_queue.py):
    python -I -u run_net.py --servers dedicated,listen --configs NQ,G --rtts 0,100,200,100j [--conds def] [--runs N]
                            [--chars 16] [--warmup 5] [--capture 60] [--seed 1] [--fps 60] [--label NAME] [--no-prime] [--dry-run]

One run (01 §6.2, §7; raw notes E0, E5, E6):
  server   dedicated: the editor binary with -server (the launcher engine has no Server target, 01b #14), capped at its 30 Hz
           NetServerMaxTickRate; listen: the packaged game hosting with ?listen, rendering, capped at --fps by t.MaxFPS.
           Both open Template_Default with the demo game mode, spawn --chars NPCs and wait for one remote player.
  client   the packaged game connecting to 127.0.0.1, rendering, capped at --fps; it records every replicated character
           (simulated proxies, and its own character as the autonomous proxy) and quits when the server says the run is over.
  latency  --rtts are ADDED ROUND TRIPS in ms: -PktLag=<rtt/2> on both processes, so each direction carries half (the lag
           applies to a process's outgoing packets). A "j" suffix adds -PktLagVariance=10 on both (the jitter run).
  clocks   real time, never the fixed step (emulated latency runs on real time, raw notes C7); every fire is compared on the
           shared wall clock through the server's frame log (event_stats.analyse_net).

What one batch does (the same protocol as run_bench.py, whose helpers it reuses):
  1. Pins this process to the performance cores (pin_self) BEFORE anything else, so both processes of every run inherit the
     mask from their first instruction; records the environment with applied_mask().
  2. One PRIMING run first (discarded).
  3. Every (server x condition x rtt x config) combination --runs times, interleaved, the config order rotated every round.
  4. Launches the REAL executables with subprocess.Popen: the server first; the client only once the server's log says it
     is waiting for its player. Waits for the client, then the server, each with a timeout; kills both process trees in a
     finally block. Child output goes nowhere (the editor server would otherwise flood the queue log); each process writes
     its own log with -abslog.
  5. Moves the files into Benchmarks/Raw as <batch>_<server>_<Config>_<cond>_rtt<rtt>_r<run>_srv_*.csv / _cli_*.csv / .log;
     never overwrites; rewrites the manifest after every run.
  6. Writes Benchmarks/Summaries/<batch>_summary.md: per combination, process, role and path, the median across runs of
     each run's statistic [min-max across runs], missed / duplicates per 1,000, window duplicates (H7), and frame intervals.
"""
from __future__ import annotations

import argparse
import json
import os
import statistics
import subprocess
import sys
import time

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)
import event_stats   # noqa: E402
import run_bench     # noqa: E402  (the single-process runner: the batch protocol, raw-data rules, environment, summary cells)

PROJECT_DIR = run_bench.PROJECT_DIR
UPROJECT = os.path.join(PROJECT_DIR, "AnimEventClock", "AnimEventClock.uproject")
EDITOR_CMD = os.path.join(run_bench.ENGINE_DIR, "Binaries", "Win64", "UnrealEditor-Cmd.exe")
EDITOR_SAVED = os.path.join(PROJECT_DIR, "AnimEventClock", "Saved")
MAP_URL = "/Engine/Maps/Templates/Template_Default?game=/Script/AnimEventClock.AECDemoGameMode"
READY_TEXT = "remote player(s)"            # the server's "Waiting for N remote player(s)" log line
SERVER_READY_TIMEOUT = 120.0
CLIENT_MARGIN_SECONDS = 240.0               # client start-up + joining + drain, on top of warm-up + capture
SERVER_FINISH_TIMEOUT = 60.0
JITTER_MS = 10

SERVERS = ("dedicated", "listen")
CONFIGS = ("NQ", "NB", "G")
SERVER_FILES = ("actions", "expected", "events", "frames")
CLIENT_FILES = ("actions", "events", "frames")


def parse_rtt(text: str) -> tuple[int, bool]:
    """'100j' -> (100, True): added round trip in ms, and whether jitter is added."""
    jitter = text.endswith("j")
    return int(text[:-1] if jitter else text), jitter


def lag_args(rtt: str) -> str:
    milliseconds, jitter = parse_rtt(rtt)
    return f"-PktLag={milliseconds // 2}" + (f" -PktLagVariance={JITTER_MS}" if jitter else "")


def window_args(x: int, y: int, log: str, args) -> str:
    """The switches of a rendering process (listen server or client): window, no sound or VSync, frame cap, own log."""
    return (f'-windowed -ResX=1280 -ResY=720 -WinX={x} -WinY={y} -nosound -unattended -traceautostart=0 -novsync '
            f'-ExecCmds="t.MaxFPS {args.fps}" -abslog="{log}"')


def sizing_args(args) -> str:
    """The workload switches: the server spawns and schedules from them; the client only sizes its buffers from them."""
    return f"-AECChars={args.chars} -AECPlayers=1 -AECWarmup={args.warmup} -AECCapture={args.capture}"


def server_command(server: str, name: str, config: str, cond: str, rtt: str, args) -> tuple[str, str, str]:
    """(command, its Saved folder, its log path)."""
    saved = EDITOR_SAVED if server == "dedicated" else run_bench.SAVED
    log = os.path.join(saved, "Logs", f"{name}_srv.log")
    workload = (f"{sizing_args(args)} -AECSeed={args.seed} -AECConfig={config} -AECCond={cond} -AECRunTag={name}_srv -AECExit "
                f"{lag_args(rtt)}")
    if server == "dedicated":
        command = (f'"{EDITOR_CMD}" "{UPROJECT}" "{MAP_URL}" -server -unattended -nosound -nosplash -traceautostart=0 '
                   f'-abslog="{log}" {workload}')
    else:
        command = f'"{run_bench.EXE}" "{MAP_URL}?listen" {window_args(0, 40, log, args)} {workload}'
    return command, saved, log


def client_command(name: str, config: str, rtt: str, args) -> tuple[str, str]:
    """(command, its log path)."""
    log = os.path.join(run_bench.SAVED, "Logs", f"{name}_cli.log")
    command = (f'"{run_bench.EXE}" 127.0.0.1 {window_args(640, 120, log, args)} '
               f"-AECClient {sizing_args(args)} -AECConfig={config} -AECRunTag={name}_cli -AECExit {lag_args(rtt)}")
    return command, log


def wait_for_text(path: str, text: str, process: subprocess.Popen, timeout: float) -> bool:
    """True once the log at path contains text; False if the process exits first or the timeout passes. Reads only what
    the log gained since the last look (an editor server's start-up log is large)."""
    deadline = time.time() + timeout
    seen, handle = "", None
    try:
        while time.time() < deadline and process.poll() is None:
            if handle is None and os.path.exists(path):
                handle = open(path, encoding="utf-8", errors="replace")
            if handle is not None:
                seen = seen[-len(text):] + handle.read()   # keep a tail, in case the text spans two reads
                if text in seen:
                    return True
            time.sleep(0.5)
        return False
    finally:
        if handle is not None:
            handle.close()


def launch(server: str, name: str, config: str, cond: str, rtt: str, args) -> dict:
    """Runs one server + client pair. Returns the commands, exit codes (None = timeout or never started) and timings."""
    server_cmd, server_saved, server_log = server_command(server, name, config, cond, rtt, args)
    client_cmd, client_log = client_command(name, config, rtt, args)
    record = {"server_command": server_cmd, "client_command": client_cmd, "server_saved": server_saved,
              "server_log": server_log, "client_log": client_log}
    if args.dry_run:
        print("DRY server:", server_cmd)
        print("DRY client:", client_cmd)
        return record

    run_bench.reset_game_user_settings()
    for log in (server_log, client_log):
        if os.path.exists(log):
            os.remove(log)

    started = time.time()
    server_process = client_process = None
    server_exit = client_exit = None
    try:
        # Both inherit this process's CPU affinity (main: pin_batch). Output to nowhere: each writes its own -abslog.
        server_process = subprocess.Popen(server_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if wait_for_text(server_log, READY_TEXT, server_process, SERVER_READY_TIMEOUT):
            record["server_ready_seconds"] = round(time.time() - started, 1)
            client_process = subprocess.Popen(client_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            client_exit = run_bench.wait_or_none(client_process, args.warmup + args.capture + CLIENT_MARGIN_SECONDS)
        server_exit = run_bench.wait_or_none(server_process, SERVER_FINISH_TIMEOUT)
    finally:
        run_bench.kill_tree(client_process)
        run_bench.kill_tree(server_process)
    record.update({"server_exit": server_exit, "client_exit": client_exit, "started": started,
                   "wall_seconds": round(time.time() - started, 1)})
    return record


def collect(name: str, record: dict) -> dict:
    result = {"server_files": run_bench.move_run_files(record["server_saved"], f"{name}_srv", SERVER_FILES),
              "client_files": run_bench.move_run_files(run_bench.SAVED, f"{name}_cli", CLIENT_FILES)}
    for process in ("server", "client"):
        log = record[f"{process}_log"]
        result.update({f"{process}_{key}": value for key, value in run_bench.copy_log(log, record["started"], os.path.basename(log)).items()})
    if result["server_files"] and result["client_files"]:
        prefix = os.path.join(run_bench.RAW_DIR, name)
        try:
            result["stats"] = event_stats.analyse_net(prefix + "_srv", prefix + "_cli")
        except (ValueError, KeyError, IndexError) as error:
            result["analysis_error"] = str(error)
    return result


def is_failure(record: dict) -> bool:
    # "stats" is present only if both processes' files existed and were analysed without error.
    return (record.get("server_exit") != 0 or record.get("client_exit") != 0 or "stats" not in record
            or any(record.get(f"{p}_log_errors") or record.get(f"{p}_log_missing") for p in ("server", "client")))


ROLE_ORDER = {"npc": 0, "remote": 1, "sim": 2, "auto": 3, "unreceived": 4}   # the roles the director writes, in table order
ERROR_KEYS = ("median", "p95", "p99", "min", "max", "abs_p99")


def role_stats(group: list[dict], process: str, clock: str):
    """(path, role, [that role's bucket in each run]) for one combination, roles in a fixed order."""
    for path in sorted({p for g in group for p in g["stats"][process][clock]}):
        per_run = [g["stats"][process][clock].get(path, {}) for g in group]
        for role in sorted({r for buckets in per_run for r in buckets if r in ROLE_ORDER}, key=ROLE_ORDER.get):
            yield path, role, [buckets[role] for buckets in per_run if role in buckets]


def error_cells(stats: list[dict]) -> str:
    errors = [s["error_ms"] for s in stats if s["error_ms"]["count"]]
    return " | ".join(run_bench.cell([e[k] for e in errors]) for k in ERROR_KEYS)


def write_summary(batch: str, environment: dict, records: list[dict]) -> str:
    os.makedirs(run_bench.SUMMARY_DIR, exist_ok=True)
    groups: dict[tuple, list[dict]] = {}
    for record in records:
        if "stats" in record and not record.get("priming"):
            groups.setdefault((record["server"], record["cond"], record["rtt"], record["config"]), []).append(record)

    cell = run_bench.cell
    columns = ("| Server | Cond | RTT ms | Config | Process | Role | Path | Runs | Expected/run | Error median | p95 | p99 | min | max "
               "| p99 abs | Frame | Missed/1000 | Dup/1000 | Window dup/run | Unexpected/run | Gate |")
    lines = [f"# Network benchmark batch {batch}", "", "## Environment", "", "```json", json.dumps(environment, indent=2), "```", "",
             "## Timing error on the wall clock, per process and role, over actions started inside the capture window", "",
             "Each cell: median across runs of each run's statistic, then [min-max across runs]. Error = fire - due, in ms, where",
             "due is the wall-clock moment of the event's due time (server game time), interpolated between the two server frames",
             "around it in the server's frame log; negative = early.",
             "Server roles: npc, remote (a remote player's character). Client roles: sim (simulated proxy), auto (its own character).",
             "Gate (01 §6.4): **problem** if p99 |error| is above one frame of that process (its median frame interval), or any",
             "event is missed or duplicated. On a client the error includes the one-way delay: that is the problem S7 addresses.", "",
             columns, "|" + "---|" * (columns.count("|") - 1)]
    game_lines = ["", "## The server's own fires in server game time (as in a single-process run)", "",
                  "| Server | Cond | RTT ms | Config | Role | Path | Runs | Error median | p95 | p99 | min | max | p99 abs |",
                  "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    frame_lines = ["", "## Frame intervals (wall clock, ms): median across runs of each run's statistic [min-max]", "",
                   "| Server | Cond | RTT ms | Config | Process | Median | p99 | max |", "|---|---|---|---|---|---|---|---|"]

    for (server, cond, rtt, config), group in sorted(groups.items()):
        key = f"| {server} | {cond} | {rtt} | {config} |"
        for process in ("server", "client"):
            intervals = [g["stats"]["frames"][process] for g in group if g["stats"]["frames"][process].get("count")]
            frame = statistics.median([i["median"] for i in intervals]) if intervals else float("nan")
            frame_lines.append(f"{key} {process} | " + " | ".join(cell([i[k] for i in intervals]) for k in ("median", "p99", "max")) + " |")
            for path, role, stats in role_stats(group, process, "wall"):
                p99 = [s["error_ms"]["abs_p99"] for s in stats if s["error_ms"]["count"]]
                problem = (bool(p99) and statistics.median(p99) > frame) or any(s["missed"] or s["duplicates"] for s in stats)
                lines.append(f"{key} {process} | {role} | {path} | {len(stats)} | {cell([s['expected'] for s in stats], 0)} | {error_cells(stats)}"
                             + f" | {frame:.1f} | {cell([s['missed_per_1000'] for s in stats], 1)} | {cell([s['duplicates_per_1000'] for s in stats], 1)}"
                             + f" | {cell([s['window_duplicates'] for s in stats], 0)} | {cell([s['unexpected'] for s in stats], 0)}"
                             + f" | {'**problem**' if problem else 'pass'} |")
        for path, role, stats in role_stats(group, "server", "game"):
            game_lines.append(f"{key} {role} | {path} | {sum(1 for s in stats if s['error_ms']['count'])} | {error_cells(stats)} |")

    lines += game_lines + frame_lines + ["", f"Raw files: `Benchmarks/Raw/{batch}_*` (per run: the server's four and the client's three CSV files,",
                                         f"both logs) and `{batch}_manifest.json` (every run's full statistics, per role and per bucket)."]
    path = os.path.join(run_bench.SUMMARY_DIR, batch + "_summary.md")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    return path


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--servers", default="dedicated")
    parser.add_argument("--configs", default="NQ")
    parser.add_argument("--rtts", default="0")
    parser.add_argument("--conds", default="def")
    parser.add_argument("--runs", type=int, default=0, help="runs per combination; 0 (default) = the number of configs")
    parser.add_argument("--chars", type=int, default=16)
    parser.add_argument("--warmup", type=float, default=5.0)
    parser.add_argument("--capture", type=float, default=60.0)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--fps", type=int, default=60, help="frame cap (t.MaxFPS) of the client and of a listen server")
    parser.add_argument("--cooldown", type=float, default=3.0)
    parser.add_argument("--label", default="")
    parser.add_argument("--no-prime", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)

    servers, configs = run_bench.split_list(args.servers), run_bench.split_list(args.configs)
    rtts, conds = run_bench.split_list(args.rtts), run_bench.split_list(args.conds)
    run_bench.check_allowed(parser, "server", servers, SERVERS)
    run_bench.check_allowed(parser, "config", configs, CONFIGS)
    run_bench.check_allowed(parser, "condition", conds, run_bench.CONDITIONS)
    for rtt in rtts:
        try:
            milliseconds, _ = parse_rtt(rtt)
        except ValueError:
            parser.error(f"bad round trip {rtt!r}: use milliseconds, optionally with a 'j' suffix for jitter")
        if milliseconds % 2:
            parser.error(f"round trip {rtt} must be even (each direction carries half)")
    if args.chars < 1:
        parser.error("--chars must be at least 1 (a server without NPCs never becomes the director)")
    run_bench.default_runs(args, configs)
    if not args.dry_run:
        for path, what in ((run_bench.EXE, "packaged build (run Tools\\package.bat)"), (EDITOR_CMD, "editor binary")):
            if not os.path.exists(path):
                parser.error(f"{what} not found: {path}")

    if not args.dry_run:
        run_bench.pin_batch(parser)

    batch = run_bench.batch_name(args.label)
    environment = run_bench.environment_record() if not args.dry_run else {}
    environment.update({"build_config": "Development: packaged game (client, listen server); editor binary with -server (dedicated server)",
                        "resolution": "1280x720 windowed per rendering process, no VSync, no sound",
                        "clock": "real time (no fixed step); client and listen-server frame cap t.MaxFPS",
                        "chars": args.chars, "players": 1, "seed": args.seed, "warmup": args.warmup, "capture": args.capture,
                        "fps_cap": args.fps, "jitter_ms": JITTER_MS})
    records: list[dict] = []

    def run(server: str, config: str, cond: str, rtt: str, run_index: int, priming: bool = False) -> None:
        name = f"{batch}_{server}_{config}_{cond}_rtt{rtt}_r{run_index}" + ("_prime" if priming else "")
        launched = launch(server, name, config, cond, rtt, args)
        if args.dry_run:
            return
        record = {"name": name, "server": server, "config": config, "cond": cond, "rtt": rtt, "run": run_index, "priming": priming}
        record.update(launched)
        record.update(collect(name, launched))
        records.append(record)
        run_bench.write_manifest(batch, environment, records)
        client = record.get("stats", {}).get("client", {}).get("wall", {}).get(config, {}).get("all", {})
        error = client.get("error_ms", {})
        print(f"  {name}: exit server {record['server_exit']} client {record['client_exit']}, {record['wall_seconds']:.0f} s, "
              f"client {config} error median {error.get('median', float('nan')):.1f} ms p99 {error.get('p99', float('nan')):.1f} ms, "
              f"missed/1000 {client.get('missed_per_1000', float('nan')):.1f}, log errors {record.get('server_log_errors')}/{record.get('client_log_errors')}"
              + ("  [PRIMING, discarded]" if priming else ""), flush=True)
        time.sleep(args.cooldown)

    if not args.no_prime:
        run(servers[0], configs[0], conds[0], rtts[0], 0, priming=True)

    for run_index in range(1, args.runs + 1):
        for server in servers:
            for cond in conds:
                for rtt in rtts:
                    for config in run_bench.rotated(configs, run_index):
                        run(server, config, cond, rtt, run_index)

    if args.dry_run:
        return 0
    print("summary:", write_summary(batch, environment, records))
    failures = [r["name"] for r in records if not r.get("priming") and is_failure(r)]
    if failures:
        print("ATTENTION - runs with a non-zero exit, timeout, missing files, missing logs or log errors:", failures)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
