#pragma once
#ifndef M3R_LEGALMOVES_H
#define M3R_LEGALMOVES_H

#include <cstdint>

#include "movegen.h"
#include "makemove.h"

inline bool IsKingInCheck(const Board &board, const int side) {
    const int kingSquare = FindKingSquare(board, side);

    if (kingSquare == NO_SQ)
        return false;

    return IsSquareAttacked(kingSquare, side == WHITE ? BLACK : WHITE, board);
}

// Generate only moves that leave our own king safe
inline MoveList GenerateLegalMoves(Board &board) {
    MoveList legal;
    const auto [moves, count] = GeneratePseudoLegalMoves(board);

    for (int i = 0; i < count; ++i) {
        const Move move = moves[i];
        UndoState undo{};

        if (!MakeMoveUnchecked(board, move, undo))continue;

        legal.AddMove(move);
        UnmakeMove(board, move, undo);
    }

    return legal;
}

// Check whether at least one legal move exists
inline bool HasAnyLegalMove(Board &board) {
    const auto [moves, count] = GeneratePseudoLegalMoves(board);

    for (int i = 0; i < count; ++i) {
        if (UndoState undo{}; MakeMoveUnchecked(board, moves[i], undo)) {
            UnmakeMove(board, moves[i], undo);
            return true;
        }
    }

    return false;
}

// Count legal move sequences to the requested depth
inline uint64_t Perft(Board &board, const int depth) {
    if (depth <= 0)
        return 1;

    uint64_t nodes = 0;
    const auto [moves, count] = GeneratePseudoLegalMoves(board);

    for (int i = 0; i < count; ++i) {
        const Move move = moves[i];
        UndoState undo{};

        if (!MakeMoveUnchecked(board, move, undo))
            continue;

        // Bulk counting
        // At depth 1 the legal child count is the answer
        nodes += depth == 1 ? 1 : Perft(board, depth - 1);

        UnmakeMove(board, move, undo);
    }

    return nodes;
}

#endif // M3R_LEGALMOVES_H
