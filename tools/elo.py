#!/usr/bin/env python3
"""
ELO calculation helpers.
Supports 2-player and multiplayer (pairwise) ELO.
"""

import math

DEFAULT_RATING = 1500
K_FACTOR = 32

# Glicko-like rating deviation
DEFAULT_RD = 350.0
MIN_RD = 30.0
Q = math.log(10) / 400.0


def update_rd_after_game(rd):
    """Decrease rating deviation after a game (Glicko-inspired)."""
    new_rd = 1.0 / math.sqrt(1.0 / (rd * rd) + 1.0 / (Q * Q))
    return max(MIN_RD, round(new_rd, 1))


def expected_score(rating_a, rating_b):
    """Expected score of A in a match against B."""
    return 1.0 / (1.0 + math.pow(10, (rating_b - rating_a) / 400.0))


def update_ratings_2p(rating_a, rating_b, winner):
    """
    Update ELO ratings for a 2-player match.
    winner: 0 = player A wins, 1 = player B wins, -1 = draw
    Returns (new_rating_a, new_rating_b)
    """
    ea = expected_score(rating_a, rating_b)
    eb = 1.0 - ea

    if winner == 0:
        sa, sb = 1.0, 0.0
    elif winner == 1:
        sa, sb = 0.0, 1.0
    else:
        sa, sb = 0.5, 0.5

    new_a = rating_a + K_FACTOR * (sa - ea)
    new_b = rating_b + K_FACTOR * (sb - eb)
    return round(new_a, 1), round(new_b, 1)


def update_ratings_multiplayer(ratings, ranking):
    """
    Update ELO for a multiplayer match using pairwise comparisons.
    ratings: list of current ratings [r0, r1, r2, ...]
    ranking: list of player indices from 1st place to last (e.g., [2, 0, 1] means player 2 won)
    Returns list of new ratings.
    """
    n = len(ratings)
    new_ratings = list(ratings)

    for i in range(n):
        for j in range(i + 1, n):
            pi = ranking[i]  # higher rank
            pj = ranking[j]  # lower rank

            ea = expected_score(ratings[pi], ratings[pj])
            # Player at ranking[i] beat player at ranking[j]
            delta = K_FACTOR * (1.0 - ea) / (n - 1)
            new_ratings[pi] += delta
            new_ratings[pj] -= delta

    return [round(r, 1) for r in new_ratings]
