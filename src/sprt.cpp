/*
 *  c-gomoku-cli, a command line interface for Gomocup engines. Copyright 2021 Chao Ma.
 *  c-gomoku-cli is derived from c-chess-cli, originally authored by lucasart 2020.
 *
 *  c-gomoku-cli is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 *  c-gomoku-cli is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with this
 * program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "sprt.h"

#include <cmath>

static double elo_to_score(double elo)
{
    return 1 / (1 + exp(-elo * log(10) / 400));
}

// Uses asymptotic LLR approximation in the trinomial GSPRT model. See:
// http://hardy.uhasselt.be/Toga/GSPRT_approximation.pdf
static double sprt_llr(int wldCount[NB_RESULT], double elo0, double elo1)
{
    if (!!wldCount[0] + !!wldCount[1] + !!wldCount[2]
        < 2)  // at least 2 among 3 must be non zero
        return 0;

    const int    n = wldCount[RESULT_WIN] + wldCount[RESULT_LOSS] + wldCount[RESULT_DRAW];
    const double w = (double)wldCount[RESULT_WIN] / n,
                 l = (double)wldCount[RESULT_LOSS] / n, d = 1 - w - l;
    const double s = w + d / 2, var = (w + d / 4) - s * s;
    const double s0 = elo_to_score(elo0), s1 = elo_to_score(elo1);

    return (s1 - s0) * (2 * s - s0 - s1) / (2 * var / n);
}

bool SPRTParam::validate() const
{
    return 0 < alpha && alpha < 1 && 0 < beta && beta < 1 && elo0 < elo1;
}

bool SPRTParam::done(int wldCount[NB_RESULT]) const
{
    const double lbound = log(beta / (1 - alpha));
    const double ubound = log((1 - beta) / alpha);
    const double llr    = sprt_llr(wldCount, elo0, elo1);

    if (llr > ubound) {
        printf("SPRT: LLR = %.3f [%.3f,%.3f]. H1 accepted.\n", llr, lbound, ubound);
        return true;
    }
    else if (llr < lbound) {
        printf("SPRT: LLR = %.3f [%.3f,%.3f]. H0 accepted.\n", llr, lbound, ubound);
        return true;
    }
    else
        printf("SPRT: LLR = %.3f [%.3f,%.3f]\n", llr, lbound, ubound);

    return false;
}
