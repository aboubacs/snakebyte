#!/usr/bin/env python3
"""
Flask web server for the CodinGame bot competition GUI.
"""

import json
import os
import sys
import queue
import threading
import time
from pathlib import Path
from datetime import datetime

from flask import Flask, render_template, request, jsonify, Response

# Add tools/ to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))
from runner import run_match, save_match
from league import Pool

ROOT = Path(__file__).parent.parent
BUILDS_DIR = ROOT / "builds"

app = Flask(__name__)

# In-memory match storage
match_results = {}
match_counter = 0

# SSE event queues per league
league_events = {}

# Per-pool locks to prevent concurrent load/save races
_pool_locks = {}
_pool_locks_lock = threading.Lock()

def get_pool_lock(name):
    with _pool_locks_lock:
        if name not in _pool_locks:
            _pool_locks[name] = threading.Lock()
        return _pool_locks[name]


# ===== Background match runner =====

def background_runner():
    """Continuously run matches across all eligible pools."""
    while True:
        try:
            pool_names = Pool.list_pools()
            ran_any = False
            for name in pool_names:
                lock = get_pool_lock(name)
                with lock:
                    pool = Pool.load(name)
                    if not pool.active:
                        continue
                    n = pool.config.get("num_players", 2)
                    if len(pool.versions) < n:
                        continue
                    # Grab players before releasing lock for the match
                    players_to_run = pool._select_players()

                try:
                    # Run match outside lock (slow I/O)
                    player_cmds = [pool.get_executable(v) for v in players_to_run]
                    from runner import run_match as _run_match
                    game_log = _run_match(player_cmds)

                    # Re-acquire lock to update state
                    with lock:
                        pool = Pool.load(name)  # reload fresh state
                        winner_idx = game_log.get("winner", -1)
                        winner = players_to_run[winner_idx] if winner_idx >= 0 else None

                        from elo import update_ratings_2p, DEFAULT_RD
                        r0 = pool.ratings[players_to_run[0]]
                        r1 = pool.ratings[players_to_run[1]]
                        rd0 = pool.rd.get(players_to_run[0], DEFAULT_RD)
                        rd1 = pool.rd.get(players_to_run[1], DEFAULT_RD)
                        new_r0, new_r1, new_rd0, new_rd1 = update_ratings_2p(r0, r1, rd0, rd1, winner_idx)
                        pool.ratings[players_to_run[0]] = new_r0
                        pool.ratings[players_to_run[1]] = new_r1
                        pool.rd[players_to_run[0]] = new_rd0
                        pool.rd[players_to_run[1]] = new_rd1

                        result = {
                            "players": players_to_run,
                            "winner": winner,
                            "scores": game_log.get("scores", []),
                            "timestamp": datetime.now().isoformat(),
                        }
                        pool.matches.append(result)
                        pool.save()

                    ran_any = True
                    players = result["players"]
                    winner_str = result.get("winner") or "draw"
                    print(f"[runner] {name}: {' vs '.join(players)} -> {winner_str}", flush=True)
                    if name in league_events:
                        for q in league_events[name]:
                            q.put(result)
                except Exception as e:
                    print(f"[runner] Error in pool {name}: {e}", flush=True)
                time.sleep(0.5)
            if not ran_any:
                time.sleep(2.0)
        except Exception as e:
            print(f"[runner] Top-level error: {e}")
            time.sleep(5.0)


# ===== Routes =====

@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/versions")
def api_versions():
    BUILDS_DIR.mkdir(parents=True, exist_ok=True)
    versions = []
    for f in sorted(BUILDS_DIR.glob("*.out")):
        stat = f.stat()
        versions.append({
            "name": f.stem,
            "file": f.name,
            "size": stat.st_size,
            "modified": datetime.fromtimestamp(stat.st_mtime).isoformat(),
        })
    return jsonify(versions)


@app.route("/api/match", methods=["POST"])
def api_run_match():
    global match_counter
    data = request.json
    players = data.get("players", [])

    if len(players) < 2:
        return jsonify({"error": "Need at least 2 players"}), 400

    player_cmds = []
    for p in players:
        path = BUILDS_DIR / f"{p}.out"
        if not path.exists():
            return jsonify({"error": f"Version not found: {p}"}), 404
        player_cmds.append(str(path))

    try:
        game_log = run_match(player_cmds)
    except Exception as e:
        return jsonify({"error": str(e)}), 500

    match_counter += 1
    match_id = match_counter
    match_results[match_id] = {
        "id": match_id,
        "players": players,
        "log": game_log,
        "timestamp": datetime.now().isoformat(),
    }

    return jsonify(match_results[match_id])


@app.route("/api/match/<int:match_id>")
def api_get_match(match_id):
    result = match_results.get(match_id)
    if not result:
        return jsonify({"error": "Match not found"}), 404
    return jsonify(result)


@app.route("/api/leagues")
def api_list_leagues():
    pool_names = Pool.list_pools()
    result = []
    for name in pool_names:
        pool = Pool.load(name)
        result.append({"name": name, "active": pool.active})
    return jsonify(result)


@app.route("/api/leagues", methods=["POST"])
def api_create_league():
    data = request.json
    name = data.get("name", "").strip()
    if not name:
        return jsonify({"error": "Name required"}), 400

    num_players = data.get("num_players", 2)
    pool = Pool(name)
    pool.config["num_players"] = num_players
    pool.save()
    return jsonify({"name": name, "created": True})


@app.route("/api/leagues/<name>")
def api_get_league(name):
    pool = Pool.load(name)
    if not pool.path.exists():
        return jsonify({"error": "Pool not found"}), 404

    rankings = pool.get_rankings()
    return jsonify({
        "name": pool.name,
        "versions": pool.versions,
        "ratings": pool.ratings,
        "rd": pool.rd,
        "rankings": rankings,
        "games_played": pool.get_games_played(),
        "win_rates": pool.get_win_rates(),
        "matches": pool.matches[-50:],
        "config": pool.config,
        "active": pool.active,
        "total_matches": len(pool.matches),
    })


@app.route("/api/leagues/<name>/versions", methods=["POST"])
def api_add_version_to_league(name):
    data = request.json
    version = data.get("version", "").strip()
    if not version:
        return jsonify({"error": "Version required"}), 400

    path = BUILDS_DIR / f"{version}.out"
    if not path.exists():
        return jsonify({"error": f"Build not found: {version}"}), 404

    with get_pool_lock(name):
        pool = Pool.load(name)
        if not pool.path.exists():
            return jsonify({"error": "Pool not found"}), 404
        pool.add_version(version)
    return jsonify({"added": version})


@app.route("/api/leagues/<name>/versions/<version>", methods=["DELETE"])
def api_remove_version_from_league(name, version):
    with get_pool_lock(name):
        pool = Pool.load(name)
        if not pool.path.exists():
            return jsonify({"error": "Pool not found"}), 404
        if version not in pool.versions:
            return jsonify({"error": "Version not in pool"}), 404
        pool.remove_version(version)
    return jsonify({"removed": version})


@app.route("/api/leagues/<name>/run", methods=["POST"])
def api_run_league_matches(name):
    lock = get_pool_lock(name)

    with lock:
        pool = Pool.load(name)
        if not pool.path.exists():
            return jsonify({"error": "Pool not found"}), 404

    data = request.json or {}
    count = data.get("count", 10)

    results = []
    for _ in range(min(count, 100)):
        try:
            with lock:
                pool = Pool.load(name)
                result = pool.run_single_match()
            results.append(result)

            if name in league_events:
                for q in league_events[name]:
                    q.put(result)
        except Exception as e:
            results.append({"error": str(e)})
            break

    with lock:
        pool = Pool.load(name)
    return jsonify({
        "matches_run": len(results),
        "results": results,
        "ratings": pool.ratings,
        "rankings": pool.get_rankings(),
    })


@app.route("/api/leagues/<name>/active", methods=["POST"])
def api_toggle_pool_active(name):
    data = request.json or {}
    with get_pool_lock(name):
        pool = Pool.load(name)
        if not pool.path.exists():
            return jsonify({"error": "Pool not found"}), 404
        pool.active = data.get("active", not pool.active)
        pool.save()
    return jsonify({"name": name, "active": pool.active})


@app.route("/api/leagues/<name>/h2h/<version>")
def api_head_to_head(name, version):
    pool = Pool.load(name)
    if not pool.path.exists():
        return jsonify({"error": "Pool not found"}), 404
    if version not in pool.versions:
        return jsonify({"error": "Version not found"}), 404
    h2h = pool.get_head_to_head(version)
    return jsonify({"version": version, "h2h": h2h})


@app.route("/api/leagues/<name>/stream")
def api_league_stream(name):
    def generate():
        q = queue.Queue()
        if name not in league_events:
            league_events[name] = []
        league_events[name].append(q)

        try:
            while True:
                try:
                    event = q.get(timeout=30)
                    yield f"data: {json.dumps(event)}\n\n"
                except queue.Empty:
                    yield f": keepalive\n\n"
        finally:
            league_events[name].remove(q)

    return Response(generate(), mimetype="text/event-stream")


# ===== Startup =====

if __name__ == "__main__":
    port = int(os.environ.get("PORT", 5050))

    # Start background runner (avoid double-start in debug reloader)
    if os.environ.get("WERKZEUG_RUN_MAIN") == "true" or not app.debug:
        runner_thread = threading.Thread(target=background_runner, daemon=True)
        runner_thread.start()
        print("[runner] Background match runner started")

    print(f"Starting GUI server on http://localhost:{port}")
    app.run(host="0.0.0.0", port=port, debug=True)
