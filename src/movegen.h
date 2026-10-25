#pragma once
#ifndef M3R_MOVEGEN_H
#define M3R_MOVEGEN_H

#include "attacks.h"
#include "MoveList.h" // NOTE: exact case matters on Linux/macOS

// Returns and removes the least-significant set bit. Requires bb != 0.
inline int PopLSB(u64 &bb) {
    const int sq = __builtin_ctzll(bb);
    bb &= bb - 1;
    return sq;
}

inline int PieceOnSquare(const Board &board, const int sq) {
    if (sq < 0 || sq >= 64)
        return NO_PIECE;

    return board.mailbox[sq];
}

/*
 * Add a pawn move that lands on the last rank as promotions.
 * queenOnly is used by capture-only generation (quiescence search).
 */
inline void AddPromotions(
    MoveList &list,
    const int from,
    const int to,
    const int pawn,
    const int captured,
    const bool white,
    const bool queenOnly) {
    const int flag = captured == NO_PIECE ? MOVE_PROMOTION : MOVE_PROMOTION_CAPTURE;
    const int base = white ? 0 : 6;

    list.AddMove(EncodeMove(from, to, pawn, captured, Q + base, flag));

    if (queenOnly)
        return;

    list.AddMove(EncodeMove(from, to, pawn, captured, R + base, flag));
    list.AddMove(EncodeMove(from, to, pawn, captured, B + base, flag));
    list.AddMove(EncodeMove(from, to, pawn, captured, N + base, flag));
}

/*
 * Castling
 *
 * Added only if the right is present, king and rook are home, the squares in between are empty
 *  and the king does not cross or land on an attacked square.
 */
inline void GenerateCastlingMoves(const Board &board, MoveList &list) {
    const u64 occupied = board.occupancies[BOTH];

    if (board.side == WHITE) {
        if (board.castlingRights & CASTLE_WHITE_KING &&
            GET_BIT(board.bitboards[K], e1) &&
            GET_BIT(board.bitboards[R], h1) &&
            !(occupied & ((1ULL << f1) | (1ULL << g1))) &&
            !IsSquareAttacked(e1, BLACK, board) &&
            !IsSquareAttacked(f1, BLACK, board) &&
            !IsSquareAttacked(g1, BLACK, board)) {
            list.AddMove(EncodeMove(e1, g1, K, NO_PIECE, 0, MOVE_KING_CASTLE));
        }

        if (board.castlingRights & CASTLE_WHITE_QUEEN &&
            GET_BIT(board.bitboards[K], e1) &&
            GET_BIT(board.bitboards[R], a1) &&
            !(occupied & ((1ULL << b1) | (1ULL << c1) | (1ULL << d1))) &&
            !IsSquareAttacked(e1, BLACK, board) &&
            !IsSquareAttacked(d1, BLACK, board) &&
            !IsSquareAttacked(c1, BLACK, board)) {
            list.AddMove(EncodeMove(e1, c1, K, NO_PIECE, 0, MOVE_QUEEN_CASTLE));
        }
    } else if (board.side == BLACK) {
        if (board.castlingRights & CASTLE_BLACK_KING &&
            GET_BIT(board.bitboards[k], e8) &&
            GET_BIT(board.bitboards[r], h8) &&
            !(occupied & ((1ULL << f8) | (1ULL << g8))) &&
            !IsSquareAttacked(e8, WHITE, board) &&
            !IsSquareAttacked(f8, WHITE, board) &&
            !IsSquareAttacked(g8, WHITE, board)) {
            list.AddMove(EncodeMove(e8, g8, k, NO_PIECE, 0, MOVE_KING_CASTLE));
        }

        if (board.castlingRights & CASTLE_BLACK_QUEEN &&
            GET_BIT(board.bitboards[k], e8) &&
            GET_BIT(board.bitboards[r], a8) &&
            !(occupied & ((1ULL << b8) | (1ULL << c8) | (1ULL << d8))) &&
            !IsSquareAttacked(e8, WHITE, board) &&
            !IsSquareAttacked(d8, WHITE, board) &&
            !IsSquareAttacked(c8, WHITE, board)) {
            list.AddMove(EncodeMove(e8, c8, k, NO_PIECE, 0, MOVE_QUEEN_CASTLE));
        }
    }
}

/*
 * Generate pseudo-legal moves for 'board.side'
 *
 * CapturesOnly = true generates just what quiescence search needs:
 *  captures, en-passant and queen promotions
 *
 *  Pseudo-legal: movement rules are satisfied, but moves that leave our own king in check are not filtered:
 *    MakeMove*() rejects those.
 *
 */
template<bool CapturesOnly> void GenerateMoves(const Board &board, MoveList &list) {
    if (board.side != WHITE && board.side != BLACK)
        return;

    const int side = board.side;
    const bool white = side == WHITE;
    const int enemy = white ? BLACK : WHITE;

    const int pawn = white ? P : p;
    const int enemyPawn = white ? p : P;

    const u64 ownOccupancy = board.occupancies[side];
    const u64 enemyOccupancy = board.occupancies[enemy];
    const u64 occupied = board.occupancies[BOTH];
    const u64 enemyKingBoard = board.bitboards[white ? k : K];

    // Never generate a move that captures the enemy king
    const u64 capturable = enemyOccupancy & ~enemyKingBoard;

    const u64 pawns = board.bitboards[pawn];
    const u64 promoRank = white ? RANK_8 : RANK_1;
    const int up = white ? 8 : -8;

    // 1: PAWNS (set-wise)
    const u64 emptySquares = ~occupied;
    const u64 push1 = (white ? pawns << 8 : pawns >> 8) & emptySquares;

    if constexpr (!CapturesOnly) {
        u64 quiet = push1 & ~promoRank;
        u64 push2 = (white ? (push1 & RANK_3) << 8 : (push1 & RANK_6) >> 8) & emptySquares;

        while (quiet) {
            const int to = PopLSB(quiet);
            list.AddMove(EncodeMove(to - up, to, pawn, NO_PIECE, 0, MOVE_QUIET));
        }

        while (push2) {
            const int to = PopLSB(push2);
            list.AddMove(EncodeMove(to - 2 * up, to, pawn, NO_PIECE, 0, MOVE_DOUBLE_PAWN_PUSH));
        }
    }

    u64 promoPush = push1 & promoRank;

    while (promoPush) {
        const int to = PopLSB(promoPush);
        AddPromotions(list, to - up, to, pawn, NO_PIECE, white, CapturesOnly);
    }

    // Captures toward the a-side and h-side
    const u64 capLeft = (white ? (pawns & ~FILE_A) << 7 : (pawns & ~FILE_A) >> 9) & capturable;
    const u64 capRight = (white ? (pawns & ~FILE_H) << 9 : (pawns & ~FILE_H) >> 7) & capturable;

    const u64 captureSets[2] = {capLeft, capRight};
    const int captureDelta[2] = {white ? 7 : -9, white ? 9 : -7};

    for (int i = 0; i < 2; ++i) {
        u64 bb = captureSets[i];

        while (bb) {
            const int to = PopLSB(bb);
            const int from = to - captureDelta[i];
            const int captured = board.mailbox[to];

            if ((1ULL << to) & promoRank)
                AddPromotions(list, from, to, pawn, captured, white, CapturesOnly);
            else
                list.AddMove(EncodeMove(from, to, pawn, captured, 0, MOVE_CAPTURE));
        }
    }

    // En passant
    if (const int ep = board.enPassantSquare;
        ep >= 0 && ep < 64 && SQ_TO_RANK(ep) == (white ? 5 : 2) && !(occupied & (1ULL << ep))) {
        if (const int capturedSquare = ep + (white ? -8 : 8);
            GET_BIT(board.bitboards[enemyPawn], capturedSquare)) {
            u64 attackers = pawn_attacks[enemy][ep] & pawns;

            while (attackers) {
                const int from = PopLSB(attackers);
                list.AddMove(EncodeMove(from, ep, pawn, enemyPawn, 0, MOVE_EN_PASSANT));
            }
        }
    }

    // 2: K, B, R, Q and K
    const u64 targets = CapturesOnly ? capturable : ~ownOccupancy & ~enemyKingBoard;

    for (int type = N; type <= K; ++type) {
        const int piece = white ? type : type + 6;
        u64 pieceBoard = board.bitboards[piece];

        while (pieceBoard) {
            const int from = PopLSB(pieceBoard);
            u64 attacks;

            switch (type) {
                case N: attacks = knight_attacks[from];
                    break;
                case B: attacks = BishopAttacks(from, occupied);
                    break;
                case R: attacks = RookAttacks(from, occupied);
                    break;
                case Q: attacks = QueenAttacks(from, occupied);
                    break;
                default: attacks = king_attacks[from];
                    break;
            }

            attacks &= targets;

            while (attacks) {
                const int to = PopLSB(attacks);
                const int captured = board.mailbox[to];

                list.AddMove(EncodeMove(
                    from, to, piece, captured, 0,
                    captured == NO_PIECE ? MOVE_QUIET : MOVE_CAPTURE
                ));
            }
        }
    }

    // 3: CASTLING
    if constexpr (!CapturesOnly)
        GenerateCastlingMoves(board, list);
}

inline MoveList GeneratePseudoLegalMoves(const Board &board) {
    MoveList list;
    GenerateMoves<false>(board, list);
    return list;
}

// Captures, en passant and queen promotions only
inline MoveList GenerateCaptureMoves(const Board &board) {
    MoveList list;
    GenerateMoves<true>(board, list);
    return list;
}

#endif // M3R_MOVEGEN_H