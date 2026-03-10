#!/usr/bin/env python3
"""
Match runner: launches referee with player executables, captures JSON game log.
"""

import sys
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).parent.parent
REFEREE = ROOT / "referee.out"


def run_match(player_cmds, referee_path=None):
    """Run a match and return the game log as parsed JSON."""
    ref = referee_path or str(REFEREE)

    if not Path(ref).exists():
        raise FileNotFoundError(f"Referee not found: {ref}. Run 'make referee' first.")

    cmd = [ref] + list(player_cmds)
    result = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=None,  # inherit parent stderr so referee logs are visible
        text=True,
        timeout=120,
    )

    if result.returncode != 0:
        raise RuntimeError(f"Referee exited with code {result.returncode}")

    try:
        game_log = json.loads(result.stdout)
    except json.JSONDecodeError as e:
        raise RuntimeError(f"Failed to parse referee output as JSON: {e}\nOutput: {result.stdout[:500]}")

    return game_log


def save_match(game_log, path=None):
    """Save game log to a file, returns the path."""
    if path is None:
        fd, path = tempfile.mkstemp(suffix=".json", prefix="match_")
        with open(fd, "w") as f:
            json.dump(game_log, f, indent=2)
    else:
        with open(path, "w") as f:
            json.dump(game_log, f, indent=2)
    return path


def main():
    if len(sys.argv) < 2:
        print("Usage: runner.py <player1_cmd> [player2_cmd] ...", file=sys.stderr)
        sys.exit(1)

    player_cmds = sys.argv[1:]

    try:
        game_log = run_match(player_cmds)
        path = save_match(game_log)
        print(json.dumps(game_log, indent=2))
        print(f"\nGame log saved to: {path}", file=sys.stderr)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
