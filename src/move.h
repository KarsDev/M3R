#pragma once
#ifndef M3R_MOVE_H
#define M3R_MOVE_H

#include <cstdint>

using Move = uint32_t;

// Captured-piece sentinel: no captured piece
constexpr int NO_PIECE = 15;

enum MoveFlag : int {
    MOVE_QUIET = 0,
    MOVE_DOUBLE_PAWN_PUSH = 1,
    MOVE_KING_CASTLE = 2,
    MOVE_QUEEN_CASTLE = 3,
    MOVE_CAPTURE = 4,
    MOVE_EN_PASSANT = 5,
    MOVE_PROMOTION = 6,
    MOVE_PROMOTION_CAPTURE = 7
};

/*
 * Bits  0-5:  Source square
 * Bits  6-11: Target square
 * Bits 12-15: Moving piece
 * Bits 16-19: Captured piece (15 means none)
 * Bits 20-23: Promoted piece
 * Bits 24-31: Move flag
 */

inline Move EncodeMove(
    const int source,
    const int target,
    const int piece,
    const int captured,
    const int promoted,
    const int flag)
{
    return static_cast<Move>(source)
         | (static_cast<Move>(target)   << 6)
         | (static_cast<Move>(piece)    << 12)
         | (static_cast<Move>(captured) << 16)
         | (static_cast<Move>(promoted) << 20)
         | (static_cast<Move>(flag)     << 24);
}

#define GET_MOVE_SOURCE(move)   ((move) & 0x3F)
#define GET_MOVE_TARGET(move)   (((move) >> 6) & 0x3F)
#define GET_MOVE_PIECE(move)    (((move) >> 12) & 0xF)
#define GET_MOVE_CAPTURED(move) (((move) >> 16) & 0xF)
#define GET_MOVE_PROMOTED(move) (((move) >> 20) & 0xF)
#define GET_MOVE_FLAG(move)     (((move) >> 24) & 0xFF)

#endif // M3R_MOVE_H