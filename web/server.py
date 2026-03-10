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


# ===== Background match runner =====

def background_runner():
    """Continuously run matches across all eligible pools."""
    while True:
        try:
            pool_names = Pool.list_pools()
            ran_any = False
            for name in pool_names:
                pool = Pool.load(name)
                if not pool.active:
                    continue
                n = pool.config.get("num_players", 2)
                if len(pool.versions) >= n:
                    try:
                        result = pool.run_single_match()
                        ran_any = True
                        players = result["players"]
                        winner = result.get("winner") or "draw"
                        print(f"[runner] {name}: {' vs '.join(players)} -> {winner}", flush=True)
                        # Push SSE event
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
        "matches": pool.matches[-50:],
        "config": pool.config,
        "active": pool.active,
        "total_matches": len(pool.matches),
    })


@app.route("/api/leagues/<name>/versions", methods=["POST"])
def api_add_version_to_league(name):
    pool = Pool.load(name)
    if not pool.path.exists():
        return jsonify({"error": "Pool not found"}), 404

    data = request.json
    version = data.get("version", "").strip()
    if not version:
        return jsonify({"error": "Version required"}), 400

    path = BUILDS_DIR / f"{version}.out"
    if not path.exists():
        return jsonify({"error": f"Build not found: {version}"}), 404

    pool.add_version(version)
    return jsonify({"added": version})


@app.route("/api/leagues/<name>/run", methods=["POST"])
def api_run_league_matches(name):
    pool = Pool.load(name)
    if not pool.path.exists():
        return jsonify({"error": "Pool not found"}), 404

    data = request.json or {}
    count = data.get("count", 10)

    results = []
    for _ in range(min(count, 100)):
        try:
            result = pool.run_single_match()
            results.append(result)

            if name in league_events:
                for q in league_events[name]:
                    q.put(result)
        except Exception as e:
            results.append({"error": str(e)})
            break

    return jsonify({
        "matches_run": len(results),
        "results": results,
        "ratings": pool.ratings,
        "rankings": pool.get_rankings(),
    })


@app.route("/api/leagues/<name>/active", methods=["POST"])
def api_toggle_pool_active(name):
    pool = Pool.load(name)
    if not pool.path.exists():
        return jsonify({"error": "Pool not found"}), 404
    data = request.json or {}
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
