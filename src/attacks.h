#pragma once
#ifndef M3R_ATTACKS_H
#define M3R_ATTACKS_H

#include "board.h"

// precomputed attacks for non-sliding pieces
inline u64 knight_attacks[64]{};
inline u64 king_attacks[64]{};
inline u64 pawn_attacks[2][64]{};

inline void InitializeKnightAttacks() {
    constexpr int moves[8][2] = {
        {1, 2}, {2, 1},
        {2, -1}, {1, -2},
        {-1, -2}, {-2, -1},
        {-2, 1}, {-1, 2}
    };

    for (int sq = 0; sq < 64; ++sq) {
        const int file = SQ_TO_FILE(sq);
        const int rank = SQ_TO_RANK(sq);

        u64 attacks = 0ULL;

        for (const auto &move: moves) {
            const int targetFile = file + move[0];
            if (const int targetRank = rank + move[1]; targetFile >= 0 && targetFile < 8 &&
                                                       targetRank >= 0 && targetRank < 8) {
                SET_BIT(attacks, MAKE_SQ(targetFile, targetRank));
            }
        }

        knight_attacks[sq] = attacks;
    }
}

inline void InitializeKingAttacks() {
    for (int sq = 0; sq < 64; ++sq) {
        const int file = SQ_TO_FILE(sq);
        const int rank = SQ_TO_RANK(sq);

        u64 attacks = 0ULL;

        for (int df = -1; df <= 1; ++df) {
            for (int dr = -1; dr <= 1; ++dr) {
                if (df == 0 && dr == 0)
                    continue;

                const int targetFile = file + df;
                if (const int targetRank = rank + dr; targetFile >= 0 && targetFile < 8 &&
                                                      targetRank >= 0 && targetRank < 8) {
                    SET_BIT(attacks, MAKE_SQ(targetFile, targetRank));
                }
            }
        }

        king_attacks[sq] = attacks;
    }
}

inline void InitializePawnAttacks() {
    for (int sq = 0; sq < 64; ++sq) {
        const int file = SQ_TO_FILE(sq);
        const int rank = SQ_TO_RANK(sq);

        u64 whiteAttacks = 0ULL;
        u64 blackAttacks = 0ULL;

        if (rank < 7) {
            if (file > 0) SET_BIT(whiteAttacks, sq + 7);
            if (file < 7) SET_BIT(whiteAttacks, sq + 9);
        }

        if (rank > 0) {
            if (file > 0) SET_BIT(blackAttacks, sq - 9);
            if (file < 7) SET_BIT(blackAttacks, sq - 7);
        }

        pawn_attacks[WHITE][sq] = whiteAttacks;
        pawn_attacks[BLACK][sq] = blackAttacks;
    }
}

/*
 * Slow sliding attacks method used only to build the magic tables and to verify them in tests.
 * The first blocker is included.
*/
inline u64 SlidingAttacksSlow(
    const int sq,
    const u64 occupied,
    const int directions[][2],
    const int directionCount) {
    if (sq < 0 || sq >= 64)
        return 0ULL;

    const int file = SQ_TO_FILE(sq);
    const int rank = SQ_TO_RANK(sq);

    u64 attacks = 0ULL;

    for (int d = 0; d < directionCount; ++d) {
        int targetFile = file + directions[d][0];
        int targetRank = rank + directions[d][1];

        while (targetFile >= 0 && targetFile < 8 &&
               targetRank >= 0 && targetRank < 8) {
            const u64 targetBit = 1ULL << MAKE_SQ(targetFile, targetRank);

            attacks |= targetBit;

            if (occupied & targetBit)
                break;

            targetFile += directions[d][0];
            targetRank += directions[d][1];
        }
    }

    return attacks;
}

inline constexpr int ROOK_DIRECTIONS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
inline constexpr int BISHOP_DIRECTIONS[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};

inline u64 RookAttacksSlow(const int sq, const u64 occupied) {
    return SlidingAttacksSlow(sq, occupied, ROOK_DIRECTIONS, 4);
}

inline u64 BishopAttacksSlow(const int sq, const u64 occupied) {
    return SlidingAttacksSlow(sq, occupied, BISHOP_DIRECTIONS, 4);
}

/*
 * Magic bitboards
 *
 * For each square, (occupied * mask) * magic >> shift is a perfect hash of the relevant blockers into
 * a a small attack table. A lookup is one AND, one MUL, one SHIFT and one LOAD, instead of a loop per ray.
 */
struct Magic {
    u64 mask = 0;
    u64 magic = 0;
    u64 *table = nullptr;
    int shift = 0;
};

inline Magic rook_magics[64];
inline Magic bishop_magics[64];
inline u64 rook_table[102400];
inline u64 bishop_table[5248];

inline u64 NextRandom(u64 &state) {
    // xor-shift64*
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * 2685821657736338717ULL;
}

// few set bits make good magic candidates
inline u64 SparseRandom(u64 &state) {
    return NextRandom(state) & NextRandom(state) & NextRandom(state);
}

inline void InitMagics(const bool rook, Magic *magics, u64 *table) {
    static u64 occ[4096], ref[4096], seen[4096];
    static int epoch[4096];

    u64 seed = rook ? 0x9E3779B97F4A7C15ULL : 0xD1B54A32D192ED03ULL;
    int offset = 0;

    for (int sq = 0; sq < 64; ++sq) {
        const int file = SQ_TO_FILE(sq);
        const int rank = SQ_TO_RANK(sq);

        // board edges never matter as blockers except on the piece's own rank or file
        const u64 edges = ((RANK_1 | RANK_8) & ~(RANK_1 << (8 * rank))) |
                          ((FILE_A | FILE_H) & ~(FILE_A << file));

        const u64 mask = (rook ? RookAttacksSlow(sq, 0) : BishopAttacksSlow(sq, 0)) & ~edges;
        const int bits = PopCount(mask);
        const int size = 1 << bits;
        const int shift = 64 - bits;

        // Carry-Rippler:
        //  enumerate every subset of the mask
        int n = 0;
        u64 subset = 0;
        do {
            occ[n] = subset;
            ref[n] = rook ? RookAttacksSlow(sq, subset) : BishopAttacksSlow(sq, subset);
            ++n;
            subset = subset - mask & mask;
        } while (subset);

        for (int i = 0; i < size; ++i) epoch[i] = 0;

        int attempt = 0;
        u64 magic = 0;

        for (;;) {
            magic = SparseRandom(seed);

            if (PopCount((mask * magic) >> 56) < 6)
                continue;

            ++attempt;
            bool ok = true;

            for (int i = 0; i < n; ++i) {
                if (const int idx = static_cast<int>((occ[i] * magic) >> shift); epoch[idx] != attempt) {
                    epoch[idx] = attempt;
                    seen[idx] = ref[i];
                } else if (seen[idx] != ref[i]) {
                    ok = false;
                    break;
                }
            }

            if (ok)
                break;
        }

        auto &[sqMask, sqMagic, sqTable, sqShift] = magics[sq];
        sqMask = mask;
        sqMagic = magic;
        sqShift = shift;
        sqTable = table + offset;

        for (int i = 0; i < n; ++i)
            sqTable[(occ[i] * magic) >> shift] = ref[i];

        offset += size;
    }
}

inline u64 RookAttacks(const int sq, const u64 occupied) {
    const auto &[mask, magic, table, shift] = rook_magics[sq];
    return table[((occupied & mask) * magic) >> shift];
}

inline u64 BishopAttacks(const int sq, const u64 occupied) {
    const auto &[mask, magic, table, shift] = bishop_magics[sq];
    return table[((occupied & mask) * magic) >> shift];
}

inline u64 QueenAttacks(const int sq, const u64 occupied) {
    return RookAttacks(sq, occupied) | BishopAttacks(sq, occupied);
}

// Initialize all precomputed attack tables. Must be called once at startup.
inline void InitAllAttacks() {
    InitializeKnightAttacks();
    InitializeKingAttacks();
    InitializePawnAttacks();
    InitMagics(true, rook_magics, rook_table);
    InitMagics(false, bishop_magics, bishop_table);
}

/*
 * Every piece of both colors attacking 'sq', given an arbitrary occupancy.
 * Passing a modifier occupancy lets static exchange evaluation "x-ray".
 */
inline u64 AttackersTo(const Board &board, const int sq, const u64 occupied) {
    return (pawn_attacks[BLACK][sq] & board.bitboards[P]) |
           (pawn_attacks[WHITE][sq] & board.bitboards[p]) |
           (knight_attacks[sq] & (board.bitboards[N] | board.bitboards[n])) |
           (king_attacks[sq] & (board.bitboards[K] | board.bitboards[k])) |
           (BishopAttacks(sq, occupied) &
            (board.bitboards[B] | board.bitboards[b] | board.bitboards[Q] | board.bitboards[q])) |
           (RookAttacks(sq, occupied) &
            (board.bitboards[R] | board.bitboards[r] | board.bitboards[Q] | board.bitboards[q]));
}

/*
 * Checks whether a square is attacked by a given side.
 * 'bySide' must be WHITE or BLACK.
 */
inline bool IsSquareAttacked(
    const int sq,
    const int bySide,
    const Board &board) {
    if (sq < 0 || sq >= 64)
        return false;

    if (bySide != WHITE && bySide != BLACK)
        return false;

    const u64 occupied = board.occupancies[BOTH];
    const int base = bySide == WHITE ? 0 : 6;

    if (pawn_attacks[bySide ^ 1][sq] & board.bitboards[P + base])
        return true;

    if (knight_attacks[sq] & board.bitboards[N + base])
        return true;

    if (king_attacks[sq] & board.bitboards[K + base])
        return true;

    const u64 queens = board.bitboards[Q + base];

    if (BishopAttacks(sq, occupied) & (board.bitboards[B + base] | queens))
        return true;

    if (RookAttacks(sq, occupied) & (board.bitboards[R + base] | queens))
        return true;

    return false;
}

#endif // M3R_ATTACKS_H