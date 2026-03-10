#!/usr/bin/env python3
"""
ELO league system: manages pools of bot versions, runs matches, persists to JSON.
Can run as CLI background runner or be used as a library by the web GUI.
"""

import json
import random
import time
import sys
from itertools import combinations
from pathlib import Path
from datetime import datetime

from elo import (
    DEFAULT_RATING, DEFAULT_RD, update_ratings_2p,
    update_ratings_multiplayer, update_rd_after_game,
)
from runner import run_match

ROOT = Path(__file__).parent.parent
LEAGUE_DIR = ROOT / "league_data"
BUILDS_DIR = ROOT / "builds"


class Pool:
    def __init__(self, name):
        self.name = name
        self.versions = []
        self.ratings = {}
        self.rd = {}
        self.matches = []
        self.config = {"num_players": 2}
        self.active = True
        self.path = LEAGUE_DIR / f"{name}.json"

    def save(self):
        LEAGUE_DIR.mkdir(parents=True, exist_ok=True)
        data = {
            "name": self.name,
            "versions": self.versions,
            "ratings": self.ratings,
            "rd": self.rd,
            "matches": self.matches,
            "config": self.config,
            "active": self.active,
        }
        self.path.write_text(json.dumps(data, indent=2))

    @classmethod
    def load(cls, name):
        pool = cls(name)
        if pool.path.exists():
            data = json.loads(pool.path.read_text())
            pool.versions = data.get("versions", [])
            pool.ratings = data.get("ratings", {})
            pool.rd = data.get("rd", {})
            pool.matches = data.get("matches", [])
            pool.config = data.get("config", {"num_players": 2})
            pool.active = data.get("active", True)
            # Backfill RD for versions missing it (legacy data)
            for v in pool.versions:
                if v not in pool.rd:
                    pool.rd[v] = DEFAULT_RD
        return pool

    @classmethod
    def list_pools(cls):
        LEAGUE_DIR.mkdir(parents=True, exist_ok=True)
        return [p.stem for p in LEAGUE_DIR.glob("*.json")]

    def add_version(self, version):
        if version not in self.versions:
            self.versions.append(version)
            self.ratings[version] = DEFAULT_RATING
            self.rd[version] = DEFAULT_RD
            self.save()

    def remove_version(self, version):
        if version in self.versions:
            self.versions.remove(version)
            self.ratings.pop(version, None)
            self.rd.pop(version, None)
            self.save()

    def get_executable(self, version):
        return str(BUILDS_DIR / f"{version}.out")

    def _select_players(self):
        """Select the most uncertain matchup based on RD and H2H coverage."""
        n = self.config.get("num_players", 2)
        if len(self.versions) < n:
            raise ValueError(f"Need at least {n} versions in pool, have {len(self.versions)}")

        if n != 2:
            return random.sample(self.versions, n)

        # Build H2H count for each pair
        h2h_counts = {}
        for m in self.matches:
            ps = tuple(sorted(m["players"][:2]))
            h2h_counts[ps] = h2h_counts.get(ps, 0) + 1

        # Score each pair: higher = more uncertain = should play next
        best_pair = None
        best_score = -1

        for a, b in combinations(self.versions, 2):
            pair_key = tuple(sorted([a, b]))
            rd_a = self.rd.get(a, DEFAULT_RD)
            rd_b = self.rd.get(b, DEFAULT_RD)
            h2h = h2h_counts.get(pair_key, 0)

            score = rd_a + rd_b + 200.0 / (1.0 + h2h)
            if score > best_score:
                best_score = score
                best_pair = [a, b]

        random.shuffle(best_pair)
        return best_pair

    def run_single_match(self):
        """Pick most uncertain matchup and run a match. Returns match result dict."""
        players = self._select_players()
        n = len(players)
        player_cmds = [self.get_executable(v) for v in players]

        game_log = run_match(player_cmds)

        winner_idx = game_log.get("winner", -1)
        winner = players[winner_idx] if winner_idx >= 0 else None

        # Update ELO
        if n == 2:
            r0 = self.ratings[players[0]]
            r1 = self.ratings[players[1]]
            new_r0, new_r1 = update_ratings_2p(r0, r1, winner_idx)
            self.ratings[players[0]] = new_r0
            self.ratings[players[1]] = new_r1
        else:
            ratings_list = [self.ratings[v] for v in players]
            scores = game_log.get("scores", [0] * n)
            ranking = sorted(range(n), key=lambda i: scores[i], reverse=True)
            new_ratings = update_ratings_multiplayer(ratings_list, ranking)
            for i, v in enumerate(players):
                self.ratings[v] = new_ratings[i]

        # Update RD
        for v in players:
            self.rd[v] = update_rd_after_game(self.rd.get(v, DEFAULT_RD))

        match_record = {
            "players": players,
            "winner": winner,
            "scores": game_log.get("scores", []),
            "timestamp": datetime.now().isoformat(),
        }
        self.matches.append(match_record)
        self.save()

        return match_record

    def get_rankings(self):
        """Return versions sorted by rating (highest first)."""
        return sorted(self.versions, key=lambda v: self.ratings.get(v, DEFAULT_RATING), reverse=True)

    def get_games_played(self):
        """Return dict of version -> number of games played."""
        counts = {v: 0 for v in self.versions}
        for m in self.matches:
            for p in m["players"]:
                if p in counts:
                    counts[p] += 1
        return counts

    def get_head_to_head(self, version):
        """Return H2H record for a version vs all others.
        Returns dict: {opponent: {wins, losses, draws}}
        """
        h2h = {}
        for other in self.versions:
            if other != version:
                h2h[other] = {"wins": 0, "losses": 0, "draws": 0}

        for m in self.matches:
            players = m["players"]
            if version not in players:
                continue
            for other in players:
                if other == version:
                    continue
                if other not in h2h:
                    h2h[other] = {"wins": 0, "losses": 0, "draws": 0}
                if m["winner"] == version:
                    h2h[other]["wins"] += 1
                elif m["winner"] is None:
                    h2h[other]["draws"] += 1
                else:
                    h2h[other]["losses"] += 1
        return h2h


def run_league(pool_name, interval=2.0):
    """Background runner: continuously runs matches for a pool."""
    pool = Pool.load(pool_name)
    print(f"Starting league runner for pool '{pool_name}'")
    print(f"Versions: {pool.versions}")
    print(f"Press Ctrl+C to stop")

    try:
        while True:
            try:
                result = pool.run_single_match()
                players = result["players"]
                winner = result["winner"] or "draw"
                ratings = {v: pool.ratings[v] for v in players}
                print(f"Match: {' vs '.join(players)} → {winner} | Ratings: {ratings}")
            except Exception as e:
                print(f"Match error: {e}")

            time.sleep(interval)
    except KeyboardInterrupt:
        print("\nLeague runner stopped.")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: league.py <pool_name>", file=sys.stderr)
        sys.exit(1)
    run_league(sys.argv[1])
