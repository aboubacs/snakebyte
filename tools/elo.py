#!/usr/bin/env python3
"""
Glicko-based rating system.
RD (rating deviation) modulates update magnitude:
  - High RD (new bot, few games) → large updates, fast convergence
  - Low RD (established bot, many games) → small updates, stable ranking
"""

import math

DEFAULT_RATING = 1500
DEFAULT_RD = 350.0
MIN_RD = 30.0
Q = math.log(10) / 400.0


def _g(rd):
    """Glicko g-function: attenuates based on opponent's uncertainty."""
    return 1.0 / math.sqrt(1.0 + 3.0 * Q * Q * rd * rd / (math.pi * math.pi))


def _expected(rating, opp_rating, opp_rd):
    """Expected score accounting for opponent's RD."""
    g_val = _g(opp_rd)
    return 1.0 / (1.0 + math.pow(10, -g_val * (rating - opp_rating) / 400.0))


def update_rd_after_game(rd, opp_rd, rating, opp_rating):
    """Decrease RD after a game (Glicko formula)."""
    g_val = _g(opp_rd)
    e_val = _expected(rating, opp_rating, opp_rd)
    d_sq = 1.0 / (Q * Q * g_val * g_val * e_val * (1.0 - e_val))
    new_rd = 1.0 / math.sqrt(1.0 / (rd * rd) + 1.0 / d_sq)
    return max(MIN_RD, round(new_rd, 1))


def update_ratings_2p(rating_a, rating_b, rd_a, rd_b, winner):
    """
    Glicko rating update for a 2-player match.
    winner: 0 = player A wins, 1 = player B wins, -1 = draw
    Returns (new_rating_a, new_rating_b, new_rd_a, new_rd_b)
    """
    g_b = _g(rd_b)
    g_a = _g(rd_a)
    e_a = _expected(rating_a, rating_b, rd_b)
    e_b = _expected(rating_b, rating_a, rd_a)

    if winner == 0:
        sa, sb = 1.0, 0.0
    elif winner == 1:
        sa, sb = 0.0, 1.0
    else:
        sa, sb = 0.5, 0.5

    # Glicko update: delta = q / (1/rd^2 + 1/d^2) * g * (s - E)
    d_sq_a = 1.0 / (Q * Q * g_b * g_b * e_a * (1.0 - e_a))
    d_sq_b = 1.0 / (Q * Q * g_a * g_a * e_b * (1.0 - e_b))

    delta_a = Q / (1.0 / (rd_a * rd_a) + 1.0 / d_sq_a) * g_b * (sa - e_a)
    delta_b = Q / (1.0 / (rd_b * rd_b) + 1.0 / d_sq_b) * g_a * (sb - e_b)

    new_a = rating_a + delta_a
    new_b = rating_b + delta_b

    new_rd_a = update_rd_after_game(rd_a, rd_b, rating_a, rating_b)
    new_rd_b = update_rd_after_game(rd_b, rd_a, rating_b, rating_a)

    return round(new_a, 1), round(new_b, 1), new_rd_a, new_rd_b
