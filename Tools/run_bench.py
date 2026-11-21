"""P06 AnimEventClock - run a single-process timing batch on the packaged build and collect every artefact.

Adapted (copied, not imported) from UE5-UpdateBudget's Tools/run_bench.py: the same priming run, interleaved rotated order,
P-core pinning, real exit codes and manifest; the measurements are this project's per-event files instead of frame times.

Usage (from anywhere; run it through the machine-wide queue, tools\\ue_queue.py):
    python -I -u run_bench.py --configs NQ,NB --conds def,uro4 --fps 60 [--runs N] [--chars 64] [--warmup 5]
                              [--capture 60] [--seed 1] [--start pre] [--label NAME] [--no-prime] [--dry-run]

What one batch does:
  1. Pins this process to the performance cores (pin_self) BEFORE anything else, so every game it starts inherits the mask
     from its first instruction; records the environment, including applied_mask() read back from Windows.
  2. Runs one PRIMING run first (discarded): file, driver and PSO caches warm for every measured run.
  3. Runs every (condition x fps x config) combination --runs times, INTERLEAVED: the config order rotates every round.
     The default --runs is the number of configs, so every config sits in every order position exactly once.
  4. Launches the REAL executable with subprocess.Popen (never through "cmd /c start", which always returns 0), waits with a
     timeout, kills the process tree in a finally block. Deletes GameUserSettings.ini first (it can override VSync / caps).
  5. Moves the run's three CSV files and its log into Benchmarks/Raw as <batch>_<Config>_<Cond>_f<fps>_r<run>_*.csv/.log,
     where <batch> = YYYY-MM-DD_HHMMSS[_label]. Existing files are never overwritten. The manifest is rewritten after every
     run, so a crash loses nothing.
  6. Writes Benchmarks/Summaries/<batch>_summary.md: per combination and path, the median across runs of each run's statistic
     [min-max across runs], missed / duplicates per 1,000, and whether repeated runs gave the same fire hash; then the
     problem gate (01 §6.4) per combination, path and event class; and a warning for any condition whose fire hash equals
     the default condition's (a condition that changed nothing, as the budget conditions did before the S3 fix).

    python -I -u run_bench.py --summarize <batch>    rewrites only that batch's summary from its raw files and manifest,
                                                     with the current event_stats.py (the raw files are never touched)

Every run: fixed step (-deterministic -FPS=<fps>), real GPU at 1280x720 windowed, no VSync, no sound, -traceautostart=0.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import shutil
import statistics
import subprocess
import sys
import time

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)
import cpu_affinity  # noqa: E402  (same folder)
import event_stats   # noqa: E402

PROJECT_DIR = os.path.dirname(TOOLS_DIR)
BUILD_DIR = os.path.join(PROJECT_DIR, "Build", "Windows")
EXE = os.path.join(BUILD_DIR, "AnimEventClock", "Binaries", "Win64", "AnimEventClock.exe")   # the real game, not the bootstrap
SAVED = os.path.join(BUILD_DIR, "AnimEventClock", "Saved")
RAW_DIR = event_stats.RAW_DIR
SUMMARY_DIR = os.path.join(PROJECT_DIR, "Benchmarks", "Summaries")
ENGINE_DIR = r"C:\Program Files\Epic Games\UE_5.8\Engine"
ENGINE_BUILD_VERSION = os.path.join(ENGINE_DIR, "Build", "Build.version")
STARTUP_MARGIN_SECONDS = 180.0
RUN_FILES = ("actions", "expected", "events")

CONFIGS = ("NQ", "NB")
CONDITIONS = ("def", "uro", "uro2", "uro4", "la4", "rme", "aba2", "aba4", "vis")
GATE_TOLERANCE_FRAMES = 1e-3   # the CSV files store times to 1 microsecond


def powershell(command: str) -> str:
    result = subprocess.run(["powershell", "-NoProfile", "-Command", command], capture_output=True, text=True)
    return result.stdout.strip()


def environment_record() -> dict:
    """Facts that must accompany every result (Benchmarks/README.md, environment record)."""
    with open(ENGINE_BUILD_VERSION, encoding="utf-8") as handle:
        build = json.load(handle)
    return {
        "date": dt.datetime.now().isoformat(timespec="seconds"),
        "engine": f"{build['MajorVersion']}.{build['MinorVersion']}.{build['PatchVersion']} (CL {build['Changelist']})",
        "build_config": "Development (packaged)",
        "compiler": "MSVC 14.44.35229, Windows SDK 10.0.22621.0",
        "cpu": powershell("(Get-CimInstance Win32_Processor).Name"),
        "gpus": powershell("(Get-CimInstance Win32_VideoController | ForEach-Object { $_.Name + ' (driver ' + $_.DriverVersion + ')' }) -join '; '"),
        "ram_gb": powershell("[math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory/1GB,1)"),
        "os": powershell("$o=Get-CimInstance Win32_OperatingSystem; $o.Caption + ' build ' + $o.BuildNumber"),
        "power_plan": powershell("powercfg /getactivescheme"),
        "on_ac_power": powershell("(Get-CimInstance Win32_Battery | Select-Object -First 1).BatteryStatus") == "2",
        "resolution": "1280x720 windowed, no VSync, no sound",
        "cpu_affinity": cpu_affinity.applied_mask(),
    }


def refuse_overwrite(path: str) -> str:
    """Raw data is never overwritten (Benchmarks/README.md)."""
    if os.path.exists(path):
        raise FileExistsError(f"refusing to overwrite raw data: {path}")
    return path


# ---- The batch protocol, shared with run_net.py (one copy of each rule) ----

def split_list(text: str) -> list[str]:
    return [value.strip() for value in text.split(",") if value.strip()]


def check_allowed(parser, what: str, values: list[str], allowed: tuple) -> None:
    unknown = [v for v in values if v not in allowed]
    if unknown:
        parser.error(f"unknown {what}(s) {unknown}; available: {allowed}")


def default_runs(args, configs: list[str]) -> None:
    """Runs per combination: by default the number of configs, so every config sits in every order position once."""
    if args.runs <= 0:
        args.runs = len(configs)
    elif args.runs % len(configs):
        print(f"WARNING: --runs {args.runs} is not a multiple of {len(configs)} configs; the rotation is unbalanced")


def rotated(configs: list[str], run_index: int) -> list[str]:
    """Interleaving: the config order rotates every round."""
    rotation = (run_index - 1) % len(configs)
    return configs[rotation:] + configs[:rotation]


def batch_name(label: str) -> str:
    return dt.datetime.now().strftime("%Y-%m-%d_%H%M%S") + (f"_{label}" if label else "")


def pin_batch(parser) -> None:
    """Keeps the machine awake and pins this process to the performance cores, before anything is recorded or launched.
    Every process it starts inherits the mask. Refuses to measure without it."""
    if cpu_affinity.KERNEL32 is None:
        return
    # ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED: an occluded or locked desktop can throttle a windowed game.
    # Applies only while this process lives.
    cpu_affinity.KERNEL32.SetThreadExecutionState(0x80000000 | 0x00000001 | 0x00000002)
    mask = cpu_affinity.performance_core_mask()
    if mask and not cpu_affinity.pin_self(mask):
        parser.error(f"could not pin the batch to the performance cores (0x{mask:X}); refusing to measure without it")


def reset_game_user_settings() -> None:
    """GameUserSettings.ini can override VSync and frame caps: every run starts without it."""
    settings = os.path.join(SAVED, "Config", "Windows", "GameUserSettings.ini")
    if os.path.exists(settings):
        os.remove(settings)


def wait_or_none(process: subprocess.Popen, timeout: float) -> int | None:
    """The exit code, or None on timeout."""
    try:
        return process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        return None


def kill_tree(process: subprocess.Popen | None) -> None:
    """Never leave a process running into the next run (timeout, Ctrl+C or any other error)."""
    if process is not None and process.poll() is None:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)], capture_output=True)


def move_run_files(saved: str, tag: str, kinds: tuple[str, ...]) -> bool:
    """Moves <saved>/AECRuns/<tag>_<kind>.csv into Benchmarks/Raw (never overwriting). True if all of them existed."""
    os.makedirs(RAW_DIR, exist_ok=True)
    present = True
    for kind in kinds:
        source = os.path.join(saved, "AECRuns", f"{tag}_{kind}.csv")
        if os.path.exists(source):
            shutil.move(source, refuse_overwrite(os.path.join(RAW_DIR, f"{tag}_{kind}.csv")))
        else:
            present = False
    return present


def copy_log(path: str, started: float, destination: str) -> dict:
    """Error and warning counts of a log written by this run (never a stale one), and a copy in Benchmarks/Raw."""
    if not os.path.exists(path) or os.path.getmtime(path) < started:
        return {"log_missing": True}
    with open(path, encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    shutil.copy(path, refuse_overwrite(os.path.join(RAW_DIR, destination)))
    return {"log_errors": text.count("Error:"), "log_warnings": text.count("Warning:")}


def launch(name: str, config: str, cond: str, fps: int, args) -> tuple[str, int | None, float, float]:
    """Starts one run and waits for it. Returns (command, exit code or None on timeout, start time, wall seconds)."""
    command = (f'"{EXE}" -windowed -ResX=1280 -ResY=720 -nosound -unattended -traceautostart=0 -deterministic -FPS={fps} -novsync '
               f"-AECChars={args.chars} -AECSeed={args.seed} -AECWarmup={args.warmup} -AECCapture={args.capture} "
               f"-AECConfig={config} -AECCond={cond} -AECStart={args.start} -AECRunTag={name} -AECExit" + (" -AECClock" if args.clock else ""))
    if args.dry_run:
        print("DRY:", command)
        return command, 0, time.time(), 0.0

    reset_game_user_settings()
    started = time.time()
    process = subprocess.Popen(command)   # inherits this process's CPU affinity (main: pin_batch)
    try:
        # Fixed step runs faster than real time, so warm-up + capture is a generous bound; the margin covers start-up.
        exit_code = wait_or_none(process, args.warmup + args.capture + STARTUP_MARGIN_SECONDS)
    finally:
        kill_tree(process)
    return command, exit_code, started, time.time() - started


def collect(name: str, started: float) -> dict:
    """Moves this run's artefacts into Benchmarks/Raw and analyses them."""
    record: dict = {"run_files": move_run_files(SAVED, name, RUN_FILES)}
    if record["run_files"]:
        try:
            record["stats"] = event_stats.analyse(os.path.join(RAW_DIR, name))
        except (ValueError, KeyError, IndexError) as error:
            record["analysis_error"] = str(error)

    record.update(copy_log(os.path.join(SAVED, "Logs", "AnimEventClock.log"), started, name + ".log"))
    return record


def is_failure(record: dict) -> bool:
    # "stats" is present only if all three run files existed and were analysed without error.
    return record.get("exit_code") != 0 or "stats" not in record or record.get("log_errors") or record.get("log_missing")


def write_manifest(batch: str, environment: dict, records: list[dict]) -> None:
    os.makedirs(RAW_DIR, exist_ok=True)
    with open(os.path.join(RAW_DIR, batch + "_manifest.json"), "w", encoding="utf-8") as handle:
        json.dump({"environment": environment, "runs": records}, handle, indent=2)


def cell(values: list[float], digits: int = 2) -> str:
    """Median across runs [min-max across runs]."""
    if not values:
        return "n/a"
    return f"{statistics.median(values):.{digits}f} [{min(values):.{digits}f}-{max(values):.{digits}f}]"


def gate_lines(groups: dict[tuple, list[dict]]) -> list[str]:
    """The problem gate (01 §6.4): a real problem if p99 |error| > 1 frame, or any missed or duplicate event. Judged on
    "instant+begin" and "end" separately (event_stats buckets); median across runs of each run's abs p99. Plus a warning
    for every condition whose fire hash equals the default condition's at the same frame rate and config."""
    lines = ["## Problem gate (01 §6.4)", "",
             "p99 of |error| in frames (median across runs [min-max]); missed and duplicates summed over the runs.", "",
             "| Cond | FPS | Config | Path | Events | Runs | p99 abs error (frames) | min (frames) | max (frames) | Missed | Dup | Gate |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for (cond, fps, config), group in sorted(groups.items()):
        if event_stats.frame_ms_of(group[0]["stats"]["run"]) is None:
            continue   # no fixed step: no frame length to judge in
        for path in sorted({p for g in group for p in g["stats"]["paths"]}):
            for events in ("instant+begin", "end"):
                stats = [g["stats"]["paths"][path][events] for g in group if events in g["stats"]["paths"].get(path, {})]
                if not stats:
                    continue
                errors = [s["error_ms"] for s in stats if s["error_ms"]["count"]]
                p99 = [e["abs_p99_frames"] for e in errors]
                missed, duplicates = sum(s["missed"] for s in stats), sum(s["duplicates"] for s in stats)
                problem = (bool(p99) and statistics.median(p99) > 1.0 + GATE_TOLERANCE_FRAMES) or missed or duplicates
                lines.append(f"| {cond} | {fps} | {config} | {path} | {events} | {len(stats)} | {cell(p99)} | "
                             f"{cell([e['min_frames'] for e in errors])} | {cell([e['max_frames'] for e in errors])} | {missed} | {duplicates} | "
                             f"{'**problem**' if problem else 'pass'} |")
    default_hashes = {(fps, config): {g["stats"]["run"].get("hash") for g in group} for (cond, fps, config), group in groups.items() if cond == "def"}
    for (cond, fps, config), group in sorted(groups.items()):
        hashes = {g["stats"]["run"].get("hash") for g in group}
        if cond != "def" and hashes == default_hashes.get((fps, config)):
            lines += ["", f"**WARNING: `{cond}` gave the default condition's fire hash at {fps} fps, {config}: it changed nothing.**"]
    return lines


def write_summary(batch: str, environment: dict, records: list[dict]) -> str:
    os.makedirs(SUMMARY_DIR, exist_ok=True)
    groups: dict[tuple, list[dict]] = {}
    for record in records:
        if "stats" in record and not record.get("priming"):
            groups.setdefault((record["cond"], record["fps"], record["config"]), []).append(record)

    lines = [f"# Benchmark batch {batch}", "", "## Environment", "", "```json", json.dumps(environment, indent=2), "```", "",
             "## Timing error per path (all bands), over actions started inside the capture window", "",
             "Each cell: median across runs of each run's statistic, then [min-max across runs]. Error = fire - due, in ms;",
             "negative = early. Missed / duplicates per 1,000 expected events.", "",
             "| Cond | FPS | Config | Path | Runs | Expected/run | Error median | p95 | p99 | min | max | Missed/1000 | Dup/1000 | Bunched share | Same hash |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for (cond, fps, config), group in sorted(groups.items()):
        hashes = {g["stats"]["run"].get("hash") for g in group}
        for path in sorted({p for g in group for p in g["stats"]["paths"]}):
            stats = [g["stats"]["paths"][path]["all"] for g in group if path in g["stats"]["paths"]]
            errors = [s["error_ms"] for s in stats if s["error_ms"]["count"]]
            lines.append(f"| {cond} | {fps} | {config} | {path} | {len(stats)} | {cell([s['expected'] for s in stats], 0)} | "
                         + " | ".join(cell([e[k] for e in errors]) for k in ("median", "p95", "p99", "min", "max"))
                         + f" | {cell([s['missed_per_1000'] for s in stats], 1)} | {cell([s['duplicates_per_1000'] for s in stats], 1)}"
                         + f" | {cell([s['bunched_share'] for s in stats], 3)} | {'yes' if len(hashes) == 1 else 'NO'} |")
    lines += ["", "Per-band numbers (0 near, 1 far, 2 behind the camera) are in each run's `stats` in the manifest.", ""]
    lines += gate_lines(groups)
    lines += ["", f"Raw files: `Benchmarks/Raw/{batch}_*` (three CSV files and a log per run) and `{batch}_manifest.json`."]
    path = os.path.join(SUMMARY_DIR, batch + "_summary.md")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    return path


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--configs", default="NQ")
    parser.add_argument("--conds", default="def")
    parser.add_argument("--fps", default="60")
    parser.add_argument("--runs", type=int, default=0, help="runs per combination; 0 (default) = the number of configs")
    parser.add_argument("--chars", type=int, default=64)
    parser.add_argument("--warmup", type=float, default=5.0)
    parser.add_argument("--capture", type=float, default=60.0)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--start", choices=("post", "pre"), default="post")
    parser.add_argument("--clock", action="store_true", help="also record the event clock's path (AEC, S6)")
    parser.add_argument("--cooldown", type=float, default=2.0)
    parser.add_argument("--label", default="")
    parser.add_argument("--no-prime", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--summarize", metavar="BATCH", help="only rewrite BATCH's summary from its raw files and manifest")
    args = parser.parse_args(argv)

    if args.summarize:
        with open(os.path.join(RAW_DIR, args.summarize + "_manifest.json"), encoding="utf-8") as handle:
            manifest = json.load(handle)
        for record in manifest["runs"]:
            if "stats" in record:
                record["stats"] = event_stats.analyse(os.path.join(RAW_DIR, record["name"]))
        print("summary:", write_summary(args.summarize, manifest["environment"], manifest["runs"]))
        return 0

    configs, conds = split_list(args.configs), split_list(args.conds)
    rates = [int(f) for f in split_list(args.fps)]
    check_allowed(parser, "config", configs, CONFIGS)
    check_allowed(parser, "condition", conds, CONDITIONS)
    default_runs(args, configs)
    if not os.path.exists(EXE) and not args.dry_run:
        parser.error(f"packaged build not found: {EXE} (run Tools\\package.bat first)")
    if not args.dry_run:
        pin_batch(parser)

    batch = batch_name(args.label)
    environment = environment_record() if not args.dry_run else {}
    environment.update({"chars": args.chars, "seed": args.seed, "warmup": args.warmup, "capture": args.capture, "start": args.start,
                        "clock": args.clock})
    records: list[dict] = []

    def run(config: str, cond: str, fps: int, run_index: int, priming: bool = False) -> None:
        name = f"{batch}_{config}_{cond}_f{fps}_r{run_index}" + ("_prime" if priming else "")
        command, exit_code, started, wall = launch(name, config, cond, fps, args)
        if args.dry_run:
            return
        record = {"name": name, "config": config, "cond": cond, "fps": fps, "run": run_index, "priming": priming,
                  "exit_code": exit_code, "wall_seconds": round(wall, 1), "command": command}
        record.update(collect(name, started))
        records.append(record)
        write_manifest(batch, environment, records)
        anim = record.get("stats", {}).get("paths", {}).get(config, {}).get("all", {})
        error = anim.get("error_ms", {})
        print(f"  {name}: exit {exit_code}, {wall:.0f} s, {config} error median {error.get('median', float('nan')):.2f} ms "
              f"p99 {error.get('p99', float('nan')):.2f} ms, missed/1000 {anim.get('missed_per_1000', float('nan')):.1f}, "
              f"log errors {record.get('log_errors')}" + ("  [PRIMING, discarded]" if priming else ""), flush=True)
        time.sleep(args.cooldown)

    if not args.no_prime:
        run(configs[0], conds[0], rates[0], 0, priming=True)

    for run_index in range(1, args.runs + 1):
        for cond in conds:
            for fps in rates:
                for config in rotated(configs, run_index):
                    run(config, cond, fps, run_index)

    if args.dry_run:
        return 0
    print("summary:", write_summary(batch, environment, records))
    failures = [r["name"] for r in records if not r.get("priming") and is_failure(r)]
    if failures:
        print("ATTENTION - runs with a non-zero exit, timeout, missing run files, missing log or log errors:", failures)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
