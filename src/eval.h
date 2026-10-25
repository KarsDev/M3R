#pragma once
#ifndef M3R_EVAL_H
#define M3R_EVAL_H

#include "board.h"

// Material values for move ordering / static-exchange-evaluations (centipawns).
inline int PieceValue(const int piece) {
    constexpr int values[6] = {100, 320, 330, 500, 900, 0};

    if (piece < 0 || piece >= PIECE_NB)
        return 0;

    return values[piece % 6];
}


/*
 * PeSTO-style piece to square tables
 *
 * Each piece has a middlegame and an endgame tables.
 * The final score interpolates between them by how much material is left on the board.
 * Tables are written as printed from White's POV.
 */
inline constexpr int MG_VALUE[6] = {82, 337, 365, 477, 1025, 0};
inline constexpr int EG_VALUE[6] = {94, 281, 297, 512, 936, 0};
inline constexpr int PHASE_WEIGHT[6] = {0, 1, 1, 2, 4, 0};

inline constexpr int MG_PST[6][64] = {
    {
        // pawn
        0, 0, 0, 0, 0, 0, 0, 0,
        98, 134, 61, 95, 68, 126, 34, -11,
        -6, 7, 26, 31, 65, 56, 25, -20,
        -14, 13, 6, 21, 23, 12, 17, -23,
        -27, -2, -5, 12, 17, 6, 10, -25,
        -26, -4, -4, -10, 3, 3, 33, -12,
        -35, -1, -20, -23, -15, 24, 38, -22,
        0, 0, 0, 0, 0, 0, 0, 0
    },
    {
        // knight
        -167, -89, -34, -49, 61, -97, -15, -107,
        -73, -41, 72, 36, 23, 62, 7, -17,
        -47, 60, 37, 65, 84, 129, 73, 44,
        -9, 17, 19, 53, 37, 69, 18, 22,
        -13, 4, 16, 13, 28, 19, 21, -8,
        -23, -9, 12, 10, 19, 17, 25, -16,
        -29, -53, -12, -3, -1, 18, -14, -19,
        -105, -21, -58, -33, -17, -28, -19, -23
    },
    {
        // bishop
        -29, 4, -82, -37, -25, -42, 7, -8,
        -26, 16, -18, -13, 30, 59, 18, -47,
        -16, 37, 43, 40, 35, 50, 37, -2,
        -4, 5, 19, 50, 37, 37, 7, -2,
        -6, 13, 13, 26, 34, 12, 10, 4,
        0, 15, 15, 15, 14, 27, 18, 10,
        4, 15, 16, 0, 7, 21, 33, 1,
        -33, -3, -14, -21, -13, -12, -39, -21
    },
    {
        // rook
        32, 42, 32, 51, 63, 9, 31, 43,
        27, 32, 58, 62, 80, 67, 26, 44,
        -5, 19, 26, 36, 17, 45, 61, 16,
        -24, -11, 7, 26, 24, 35, -8, -20,
        -36, -26, -12, -1, 9, -7, 6, -23,
        -45, -25, -16, -17, 3, 0, -5, -33,
        -44, -16, -20, -9, -1, 11, -6, -71,
        -19, -13, 1, 17, 16, 7, -37, -26
    },
    {
        // queen
        -28, 0, 29, 12, 59, 44, 43, 45,
        -24, -39, -5, 1, -16, 57, 28, 54,
        -13, -17, 7, 8, 29, 56, 47, 57,
        -27, -27, -16, -16, -1, 17, -2, 1,
        -9, -26, -9, -10, -2, -4, 3, -3,
        -14, 2, -11, -2, -5, 2, 14, 5,
        -35, -8, 11, 2, 8, 15, -3, 1,
        -1, -18, -9, 10, -15, -25, -31, -50
    },
    {
        // king
        -65, 23, 16, -15, -56, -34, 2, 13,
        29, -1, -20, -7, -8, -4, -38, -29,
        -9, 24, 2, -16, -20, 6, 22, -22,
        -17, -20, -12, -27, -30, -25, -14, -36,
        -49, -1, -27, -39, -46, -44, -33, -51,
        -14, -14, -22, -46, -44, -30, -15, -27,
        1, 7, -8, -64, -43, -16, 9, 8,
        -15, 36, 12, -54, 8, -28, 24, 14
    }
};

inline constexpr int EG_PST[6][64] = {
    {
        // pawn
        0, 0, 0, 0, 0, 0, 0, 0,
        178, 173, 158, 134, 147, 132, 165, 187,
        94, 100, 85, 67, 56, 53, 82, 84,
        32, 24, 13, 5, -2, 4, 17, 17,
        13, 9, -3, -7, -7, -8, 3, -1,
        4, 7, -6, 1, 0, -5, -1, -8,
        13, 8, 8, 10, 13, 0, 2, -7,
        0, 0, 0, 0, 0, 0, 0, 0
    },
    {
        // knight
        -58, -38, -13, -28, -31, -27, -63, -99,
        -25, -8, -25, -2, -9, -25, -24, -52,
        -24, -20, 10, 9, -1, -9, -19, -41,
        -17, 3, 22, 22, 22, 11, 8, -18,
        -18, -6, 16, 25, 16, 17, 4, -18,
        -23, -3, -1, 15, 10, -3, -20, -22,
        -42, -20, -10, -5, -2, -20, -23, -44,
        -29, -51, -23, -15, -22, -18, -50, -64
    },
    {
        // bishop
        -14, -21, -11, -8, -7, -9, -17, -24,
        -8, -4, 7, -12, -3, -13, -4, -14,
        2, -8, 0, -1, -2, 6, 0, 4,
        -3, 9, 12, 9, 14, 10, 3, 2,
        -6, 3, 13, 19, 7, 10, -3, -9,
        -12, -3, 8, 10, 13, 3, -7, -15,
        -14, -18, -7, -1, 4, -9, -15, -27,
        -23, -9, -23, -5, -9, -16, -5, -17
    },
    {
        // rook
        13, 10, 18, 15, 12, 12, 8, 5,
        11, 13, 13, 11, -3, 3, 8, 3,
        7, 7, 7, 5, 4, -3, -5, -3,
        4, 3, 13, 1, 2, 1, -1, 2,
        3, 5, 8, 4, -5, -6, -8, -11,
        -4, 0, -5, -1, -7, -12, -8, -16,
        -6, -6, 0, 2, -9, -9, -11, -3,
        -9, 2, 3, -1, -5, -13, 4, -20
    },
    {
        // queen
        -9, 22, 22, 27, 27, 19, 10, 20,
        -17, 20, 32, 41, 58, 25, 30, 0,
        -20, 6, 9, 49, 47, 35, 19, 9,
        3, 22, 24, 45, 57, 40, 57, 36,
        -18, 28, 19, 47, 31, 34, 39, 23,
        -16, -27, 15, 6, 9, 17, 10, 5,
        -22, -23, -30, -16, -16, -23, -36, -32,
        -33, -28, -22, -43, -5, -32, -20, -41
    },
    {
        // king
        -74, -35, -18, -18, -11, 15, 4, -17,
        -12, 17, 14, 17, 17, 38, 23, 11,
        10, 17, 23, 15, 20, 45, 44, 13,
        -8, 22, 24, 27, 26, 33, 26, 3,
        -18, -4, 21, 24, 27, 23, 9, -11,
        -19, -3, 11, 21, 23, 16, 7, -9,
        -27, -11, 4, 13, 14, 4, -5, -17,
        -53, -34, -21, -11, -28, -14, -24, -43
    }
};

/*
 * Combined material plus PST lookup.
 * It's signed, so the num is from White's prospective (black pieces store negative values)
 * Built at compile time.
 */
struct EvalTables {
    int mg[12][64];
    int eg[12][64];
};

constexpr EvalTables BuildEvalTables() {
    EvalTables t{};

    for (int type = 0; type < 6; ++type) {
        for (int sq = 0; sq < 64; ++sq) {
            t.mg[type][sq] = MG_VALUE[type] + MG_PST[type][sq ^ 56];
            t.eg[type][sq] = EG_VALUE[type] + EG_PST[type][sq ^ 56];

            t.mg[type + 6][sq] = -(MG_VALUE[type] + MG_PST[type][sq]);
            t.eg[type + 6][sq] = -(EG_VALUE[type] + EG_PST[type][sq]);
        }
    }

    return t;
}

inline constexpr EvalTables EVAL_TABLES = BuildEvalTables();

// File / passed-pawn masks
// built at compile time.
struct EvalMasks {
    u64 file[8];
    u64 adjacent[8];
    u64 passed[2][64]; // squares in front on this and adjacent files
};

constexpr EvalMasks BuildEvalMasks() {
    EvalMasks m{};

    for (int f = 0; f < 8; ++f) {
        m.file[f] = FILE_A << f;
        m.adjacent[f] = (f > 0 ? FILE_A << (f - 1) : 0ULL) |
                        (f < 7 ? FILE_A << (f + 1) : 0ULL);
    }

    for (int sq = 0; sq < 64; ++sq) {
        const int f = sq & 7;
        const int rk = sq >> 3;
        const u64 span = m.file[f] | m.adjacent[f];

        u64 ahead = 0ULL;
        for (int x = rk + 1; x < 8; ++x) ahead |= RANK_1 << (8 * x);
        m.passed[WHITE][sq] = span & ahead;

        u64 behind = 0ULL;
        for (int x = 0; x < rk; ++x) behind |= RANK_1 << (8 * x);
        m.passed[BLACK][sq] = span & behind;
    }

    return m;
}

inline constexpr EvalMasks EVAL_MASKS = BuildEvalMasks();

inline constexpr int PASSED_MG[8] = {0, 2, 5, 10, 20, 35, 55, 0};
inline constexpr int PASSED_EG[8] = {0, 8, 15, 30, 55, 90, 140, 0};

/*
 * Pawn structure for one side, added to the mg and eg accumulators from that side's POV
 */
inline void EvalPawnStructure(const Board &board, const int side, int &mg, int &eg) {
    const u64 own = board.bitboards[side == WHITE ? P : p];
    const u64 enemy = board.bitboards[side == WHITE ? p : P];

    u64 bb = own;

    while (bb) {
        const int sq = __builtin_ctzll(bb);
        bb &= bb - 1;

        if (const int file = sq & 7; !(EVAL_MASKS.adjacent[file] & own)) {
            mg -= 8; // isolated
            eg -= 14;
        }

        if (!(EVAL_MASKS.passed[side][sq] & enemy)) {
            const int relRank = side == WHITE ? sq >> 3 : 7 - (sq >> 3);
            mg += PASSED_MG[relRank];
            eg += PASSED_EG[relRank];
        }
    }

    for (const auto f: EVAL_MASKS.file) {
        if (const int count = PopCount(own & f); count > 1) {
            mg -= 8 * (count - 1); // doubled
            eg -= 12 * (count - 1);
        }
    }
}

/*
 * Returns true if 'bySide' attacks 'targetSq'
 */
inline bool EvalSquareAttackedBy(const Board &board, const int sq, const int bySide) {
    return (AttackersTo(board, sq, board.occupancies[BOTH]) & board.occupancies[bySide]) != 0;
}

inline u64 PawnAttacksBB(const u64 pawns, const int side) {
    return side == WHITE
               ? ((pawns & ~FILE_A) << 7) | ((pawns & ~FILE_H) << 9)
               : ((pawns & ~FILE_H) >> 7) | ((pawns & ~FILE_A) >> 9);
}

/*
 * Conservative middlegame king-safety evaluation.
 * Positive score benefit 'side'.
 */
inline void EvalKingSafety(
    const Board &board,
    const int side,
    int &mg
) {
    const int kingPiece = side == WHITE ? K : k;
    const u64 kingBB = board.bitboards[kingPiece];

    if (!kingBB) return;

    const int kingSq = __builtin_ctzll(kingBB);
    const int kingFile = kingSq & 7;
    const int kingRank = kingSq >> 3;
    const int relativeRank =
            side == WHITE ? kingRank : 7 - kingRank;

    const int pawnPiece = side == WHITE ? P : p;
    const u64 ownPawns = board.bitboards[pawnPiece];

    /*
     * Reward a pawn shield near a king on it's home two ranks.
     * The second row receives a smaller bonus.
     */
    if (relativeRank <= 1) {
        const int forward = side == WHITE ? 1 : -1;

        for (int df = -1; df <= 1; ++df) {
            const int f = kingFile + df;

            if (f < 0 || f >= 8)
                continue;

            if (const int shieldRank = kingRank + forward; shieldRank >= 0 && shieldRank < 8) {
                if (ownPawns & (1ULL << (shieldRank * 8 + f)))
                    mg += 8;
            }

            if (const int secondRank = kingRank + 2 * forward; secondRank >= 0 && secondRank < 8) {
                if (ownPawns & (1ULL << (secondRank * 8 + f)))
                    mg += 3;
            }
        }
    }

    const int enemySide = side == WHITE ? BLACK : WHITE;
    const int enemyKnight = enemySide == WHITE ? N : n;
    const int enemyBishop = enemySide == WHITE ? B : b;
    const int enemyRook = enemySide == WHITE ? R : r;
    const int enemyQueen = enemySide == WHITE ? Q : q;

    // Only apply attack-pressure penalties if the opponent has pieces beyond pawns and the king
    const int attackUnits =
            PopCount(board.bitboards[enemyKnight]) +
            PopCount(board.bitboards[enemyBishop]) +
            2 * PopCount(board.bitboards[enemyRook]) +
            3 * PopCount(board.bitboards[enemyQueen]);

    if (attackUnits == 0)
        return;

    // Count attacked squares immediately surrounding the king
    int attackedRingSquares = 0;

    for (int df = -1; df <= 1; ++df) {
        for (int dr = -1; dr <= 1; ++dr) {
            if (df == 0 && dr == 0)
                continue;

            const int f = kingFile + df;
            const int r = kingRank + dr;

            if (f < 0 || f >= 8 || r < 0 || r >= 8)
                continue;

            if (EvalSquareAttackedBy(board, r * 8 + f, enemySide))
                ++attackedRingSquares;
        }
    }

    const int cappedUnits = attackUnits < 8 ? attackUnits : 8;

    /*
     * More attacking material makes more significant pressure around the king.
     * The penalty is capped.
     */
    mg -= attackedRingSquares * (4 + cappedUnits);

    // Additional penalty if the king's square itself is attacked
    if (EvalSquareAttackedBy(board, kingSq, enemySide))
        mg -= 20 + 2 * cappedUnits;
}


// Rooks on open and semi-open files, for one side
inline void EvalRooks(const Board &board, const int side, int &mg, int &eg) {
    const u64 own = board.bitboards[side == WHITE ? P : p];
    const u64 enemy = board.bitboards[side == WHITE ? p : P];

    u64 rooks = board.bitboards[side == WHITE ? R : r];

    while (rooks) {
        const int sq = __builtin_ctzll(rooks);
        rooks &= rooks - 1;

        if (const u64 fileMask = EVAL_MASKS.file[sq & 7]; !(own & fileMask)) {
            if (!(enemy & fileMask)) {
                mg += 20; // open file
                eg += 10;
            } else {
                mg += 10; // semi-open file
                eg += 5;
            }
        }
    }
}


/*
 * Count a king's reachable destinations, excluding own pieces.
 * This is pseudo-legal mobility.
 */
inline int EvalKnightMobility(
    const int sq,
    const u64 ownOccupancy
) {
    constexpr int deltas[8][2] = {
        {1, 2}, {2, 1}, {2, -1}, {1, -2},
        {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}
    };

    const int file = sq & 7;
    const int rank = sq >> 3;
    int mobility = 0;

    for (const auto &d: deltas) {
        const int f = file + d[0];
        const int r = rank + d[1];

        if (f < 0 || f >= 8 || r < 0 || r >= 8)
            continue;

        if (!(ownOccupancy & (1ULL << (r * 8 + f))))
            ++mobility;
    }

    return mobility;
}


inline int EvalSlidingMobility(
    const Board &board,
    const int sq,
    const u64 ownOccupancy,
    const bool diagonal,
    const bool orthogonal
) {
    constexpr int directions[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    };

    const u64 occupied = board.occupancies[BOTH];
    const int file = sq & 7;
    const int rank = sq >> 3;
    int mobility = 0;

    for (const auto &d: directions) {
        const bool isDiagonal = d[0] != 0 && d[1] != 0;

        if (isDiagonal && !diagonal)
            continue;

        if (!isDiagonal && !orthogonal)
            continue;

        int f = file + d[0];
        int r = rank + d[1];

        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
            const int target = r * 8 + f;
            const u64 bit = 1ULL << target;

            // Friendly pieces block the ray
            if (ownOccupancy & bit)
                break;

            // Empty squares and potential captures count
            ++mobility;

            // Stop after the first occupied square
            if (occupied & bit)
                break;

            f += d[0];
            r += d[1];
        }
    }

    return mobility;
}


// Mobility score from this side's perspective
inline void EvalMobility(
    const Board &board,
    const int side,
    int &mg,
    int &eg
) {
    const int knight = side == WHITE ? N : n;
    const int bishop = side == WHITE ? B : b;
    const int rook = side == WHITE ? R : r;
    const int queen = side == WHITE ? Q : q;

    const u64 ownOccupancy = board.occupancies[side];

    // knights
    u64 knights = board.bitboards[knight];

    while (knights) {
        const int sq = __builtin_ctzll(knights);
        knights &= knights - 1;

        const int mobility =
                EvalKnightMobility(sq, ownOccupancy);

        mg += (mobility - 4) * 4;
        eg += (mobility - 4) * 3;
    }

    // bishops
    u64 bishops = board.bitboards[bishop];

    while (bishops) {
        const int sq = __builtin_ctzll(bishops);
        bishops &= bishops - 1;

        const int mobility = EvalSlidingMobility(
            board, sq, ownOccupancy, true, false
        );

        mg += (mobility - 7) * 3;
        eg += (mobility - 7) * 4;
    }

    // rooks
    u64 rooks = board.bitboards[rook];

    while (rooks) {
        const int sq = __builtin_ctzll(rooks);
        rooks &= rooks - 1;

        const int mobility = EvalSlidingMobility(
            board, sq, ownOccupancy, false, true
        );

        mg += (mobility - 7) * 2;
        eg += (mobility - 7) * 3;
    }

    // queens
    u64 queens = board.bitboards[queen];

    while (queens) {
        const int sq = __builtin_ctzll(queens);
        queens &= queens - 1;

        const int mobility = EvalSlidingMobility(
            board, sq, ownOccupancy, true, true
        );

        mg += mobility - 14;
        eg += (mobility - 14) * 2;
    }

    const u64 occ = board.occupancies[BOTH];
    const u64 area = ~(board.bitboards[side == WHITE ? P : p] |
                       board.bitboards[side == WHITE ? K : k] |
                       PawnAttacksBB(board.bitboards[side == WHITE ? p : P], side ^ 1));

    for (u64 bb = board.bitboards[bishop]; bb; bb &= bb - 1) {
        const int m = PopCount(BishopAttacks(__builtin_ctzll(bb), occ) & area);
        mg += (m - 7) * 3;
        eg += (m - 7) * 4;
    }
}

// K vs K or K+minor vs K: neither side can force mate.
inline bool IsInsufficientMaterial(const Board &board) {
    if (board.bitboards[P] | board.bitboards[p] |
        board.bitboards[R] | board.bitboards[r] |
        board.bitboards[Q] | board.bitboards[q]) {
        return false;
    }

    return PopCount(board.bitboards[N] | board.bitboards[n] | board.bitboards[B] | board.bitboards[b]) <= 1;
}

inline int MopUp(const Board &b, const int strong) {
    const int sk = __builtin_ctzll(b.bitboards[strong == WHITE ? K : k]);
    const int wk = __builtin_ctzll(b.bitboards[strong == WHITE ? k : K]);
    const int wf = wk & 7, wr = wk >> 3;
    const int centerDist = std::max(3 - wf, wf - 4) + std::max(3 - wr, wr - 4);
    const int kingDist = std::abs((sk & 7) - wf) + std::abs((sk >> 3) - wr);
    return 10 * centerDist + 4 * (14 - kingDist); // drive the king to the edge, bring ours close
}

// Evaluation from White's perspective
inline int EvaluateWhite(const Board &board) {
    int mg = 0;
    int eg = 0;
    int phase = 0;

    for (int piece = 0; piece < PIECE_NB; ++piece) {
        u64 bb = board.bitboards[piece];
        phase += PHASE_WEIGHT[piece % 6] * PopCount(bb);

        while (bb) {
            const int sq = __builtin_ctzll(bb);
            bb &= bb - 1;

            mg += EVAL_TABLES.mg[piece][sq];
            eg += EVAL_TABLES.eg[piece][sq];
        }
    }

    int wmg = 0, weg = 0, bmg = 0, beg = 0;

    EvalPawnStructure(board, WHITE, wmg, weg);
    EvalPawnStructure(board, BLACK, bmg, beg);
    EvalRooks(board, WHITE, wmg, weg);
    EvalRooks(board, BLACK, bmg, beg);

    EvalMobility(board, WHITE, wmg, weg);
    EvalMobility(board, BLACK, bmg, beg);

    EvalKingSafety(board, WHITE, wmg);
    EvalKingSafety(board, BLACK, bmg);

    if (PopCount(board.bitboards[B]) >= 2) {
        wmg += 25;
        weg += 35;
    }

    if (PopCount(board.bitboards[b]) >= 2) {
        bmg += 25;
        beg += 35;
    }

    mg += wmg - bmg;
    eg += weg - beg;

    // Mod-Up drives the lone king to the edge when the bot is clearly winning
    if (eg > 300 && !board.bitboards[p])
        eg += MopUp(board, WHITE);
    else if (eg < -300 && !board.bitboards[P])
        eg -= MopUp(board, BLACK);

    phase = phase > 24 ? 24 : phase;

    return (mg * phase + eg * (24 - phase)) / 24;
}

// Evaluation from the side to move's perspective
inline int Evaluate(const Board &board) {
    if (IsInsufficientMaterial(board))
        return 0;

    constexpr int TEMPO = 10;
    const int score = EvaluateWhite(board);

    return (board.side == WHITE ? score : -score) + TEMPO;
}

#endif // M3R_EVAL_H
