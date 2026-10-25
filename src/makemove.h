#pragma once
#ifndef M3R_MAKEMOVE_H
#define M3R_MAKEMOVE_H

#include "attacks.h"
#include "move.h"
#include "transposition.h"

struct UndoState {
    int sideToMove;
    int castlingRights;
    int enPassantSquare;
    int halfmoveClock;

    std::uint64_t zobristKey;
    bool zobristKeyValid;
};

// Find the given side's own king
inline int FindKingSquare(const Board &board, const int side) {
    const u64 kingBoard = board.bitboards[side == WHITE ? K : k];

    if (kingBoard == 0ULL)
        return NO_SQ;

    return __builtin_ctzll(kingBoard);
}

/*
 * Castling rights that survive a move touching each square.
 * A move that starts or ends on a king or a rook home square clears the matching rights.
 *
 */
inline constexpr int CASTLE_MASK[64] = {
    // rank 1
    ~CASTLE_WHITE_QUEEN & 15, 15, 15, 15,
    ~(CASTLE_WHITE_KING | CASTLE_WHITE_QUEEN) & 15, 15, 15, ~CASTLE_WHITE_KING & 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    // rank 8
    ~CASTLE_BLACK_QUEEN & 15, 15, 15, 15,
    ~(CASTLE_BLACK_KING | CASTLE_BLACK_QUEEN) & 15, 15, 15, ~CASTLE_BLACK_KING & 15
};

/*
 * Restore the exact previous position after a successful make.
 * Occupancies and the mailbox are restored incrementally.
 */
inline void UnmakeMove(
    Board &board,
    const Move move,
    const UndoState &undo
) {
    const int from = GET_MOVE_SOURCE(move);
    const int to = GET_MOVE_TARGET(move);
    const int piece = GET_MOVE_PIECE(move);
    const int captured = GET_MOVE_CAPTURED(move);
    const int promoted = GET_MOVE_PROMOTED(move);
    const int flag = GET_MOVE_FLAG(move);

    const int side = undo.sideToMove;
    const int enemy = side ^ 1;

    const bool isPromotion = flag == MOVE_PROMOTION || flag == MOVE_PROMOTION_CAPTURE;
    const int placed = isPromotion ? promoted : piece;

    const u64 fromBit = 1ULL << from;
    const u64 toBit = 1ULL << to;

    // Return the moving piece to its original square
    board.bitboards[placed] &= ~toBit;
    board.bitboards[piece] |= fromBit;
    board.mailbox[to] = static_cast<uint8_t>(NO_PIECE);
    board.mailbox[from] = static_cast<uint8_t>(piece);
    board.occupancies[side] ^= fromBit | toBit;

    // Restore the captured piece
    if (captured != NO_PIECE) {
        const int capturedSquare = flag == MOVE_EN_PASSANT
                                       ? to + (side == WHITE ? -8 : 8)
                                       : to;
        const u64 capturedBit = 1ULL << capturedSquare;

        board.bitboards[captured] |= capturedBit;
        board.mailbox[capturedSquare] = static_cast<uint8_t>(captured);
        board.occupancies[enemy] |= capturedBit;
    }

    // Reverse the rook movement when undoing castling
    if (flag == MOVE_KING_CASTLE || flag == MOVE_QUEEN_CASTLE) {
        const bool kingSide = flag == MOVE_KING_CASTLE;
        const int rook = side == WHITE ? R : r;
        const int rookFrom = side == WHITE ? (kingSide ? h1 : a1) : kingSide ? h8 : a8;
        const int rookTo = side == WHITE ? (kingSide ? f1 : d1) : kingSide ? f8 : d8;
        const u64 rookBits = (1ULL << rookFrom) | (1ULL << rookTo);

        board.bitboards[rook] ^= rookBits;
        board.occupancies[side] ^= rookBits;
        board.mailbox[rookTo] = static_cast<uint8_t>(NO_PIECE);
        board.mailbox[rookFrom] = static_cast<uint8_t>(rook);
    }

    board.occupancies[BOTH] = board.occupancies[WHITE] | board.occupancies[BLACK];

    // Restore the previous position metadata
    board.side = undo.sideToMove;
    board.castlingRights = undo.castlingRights;
    board.enPassantSquare = undo.enPassantSquare;
    board.halfmoveClock = undo.halfmoveClock;

    // Restore the exact previous Zobrist hash (and whether it was valid)
    board.zobristKey = undo.zobristKey;
    board.zobristKeyValid = undo.zobristKeyValid;
}

/*
 * Apply a move without validating its encoding.
 * The move must have come from GeneratePseudoLegalMoves() or GenerateCaptureMoves() for exactly this position.
 * Returns false and restores the board if the move leaves our own king in check.
 */
inline bool MakeMoveUnchecked(
    Board &board,
    const Move move,
    UndoState &undo
) {
    const int from = GET_MOVE_SOURCE(move);
    const int to = GET_MOVE_TARGET(move);
    const int piece = GET_MOVE_PIECE(move);
    const int captured = GET_MOVE_CAPTURED(move);
    const int promoted = GET_MOVE_PROMOTED(move);
    const int flag = GET_MOVE_FLAG(move);

    const int side = board.side;
    const int enemy = side ^ 1;

    // Save state for UnmakeMove() BEFORE touching the hash cache so that the "cache was invalid" state is restored exactly
    undo.sideToMove = side;
    undo.castlingRights = board.castlingRights;
    undo.enPassantSquare = board.enPassantSquare;
    undo.halfmoveClock = board.halfmoveClock;
    undo.zobristKey = board.zobristKey;
    undo.zobristKeyValid = board.zobristKeyValid;

    EnsurePositionKey(board);

    const auto &[zPieces, zSide, zCastling, zEnPassant] = GetZobristKeys();
    std::uint64_t key = board.zobristKey;

    // Remove old metadata. The side-to-move key toggles exactly once.
    key ^= zCastling[static_cast<unsigned int>(board.castlingRights) & 0xFU];

    if (board.enPassantSquare >= 0 && board.enPassantSquare < 64)
        key ^= zEnPassant[board.enPassantSquare];

    key ^= zSide;

    const u64 fromBit = 1ULL << from;
    const u64 toBit = 1ULL << to;

    const bool isPromotion = flag == MOVE_PROMOTION || flag == MOVE_PROMOTION_CAPTURE;
    const int placed = isPromotion ? promoted : piece;

    // Remove the captured piece, including en-passant captures
    if (captured != NO_PIECE) {
        const int capturedSquare = flag == MOVE_EN_PASSANT
                                       ? to + (side == WHITE ? -8 : 8)
                                       : to;
        const u64 capturedBit = 1ULL << capturedSquare;

        key ^= zPieces[captured][capturedSquare];
        board.bitboards[captured] &= ~capturedBit;
        board.mailbox[capturedSquare] = static_cast<uint8_t>(NO_PIECE);
        board.occupancies[enemy] &= ~capturedBit;
    }

    // Move the piece (or its promoted replacement)
    key ^= zPieces[piece][from] ^ zPieces[placed][to];

    board.bitboards[piece] &= ~fromBit;
    board.bitboards[placed] |= toBit;
    board.mailbox[from] = static_cast<uint8_t>(NO_PIECE);
    board.mailbox[to] = static_cast<uint8_t>(placed);
    board.occupancies[side] ^= fromBit | toBit;

    // Move the rook when castling
    if (flag == MOVE_KING_CASTLE || flag == MOVE_QUEEN_CASTLE) {
        const bool kingSide = flag == MOVE_KING_CASTLE;
        const int rook = side == WHITE ? R : r;
        const int rookFrom = side == WHITE ? (kingSide ? h1 : a1) : kingSide ? h8 : a8;
        const int rookTo = side == WHITE ? (kingSide ? f1 : d1) : kingSide ? f8 : d8;
        const u64 rookBits = (1ULL << rookFrom) | (1ULL << rookTo);

        key ^= zPieces[rook][rookFrom] ^ zPieces[rook][rookTo];

        board.bitboards[rook] ^= rookBits;
        board.occupancies[side] ^= rookBits;
        board.mailbox[rookFrom] = static_cast<uint8_t>(NO_PIECE);
        board.mailbox[rookTo] = static_cast<uint8_t>(rook);
    }

    board.occupancies[BOTH] = board.occupancies[WHITE] | board.occupancies[BLACK];

    // Update castling rights, en-passant square, clocks and side to move
    board.castlingRights &= CASTLE_MASK[from] & CASTLE_MASK[to];

    board.enPassantSquare = flag == MOVE_DOUBLE_PAWN_PUSH ? (from + to) / 2 : NO_SQ;

    board.halfmoveClock = piece == P || piece == p || captured != NO_PIECE
                              ? 0
                              : board.halfmoveClock + 1;

    board.side = enemy;

    // Add new metadata to the hash
    key ^= zCastling[static_cast<unsigned int>(board.castlingRights) & 0xFU];

    if (board.enPassantSquare >= 0 && board.enPassantSquare < 64)
        key ^= zEnPassant[board.enPassantSquare];

    board.zobristKey = key;
    board.zobristKeyValid = true;

    // Reject moves that leave our king in check
    if (const int kingSquare = FindKingSquare(board, side);
        kingSquare == NO_SQ || IsSquareAttacked(kingSquare, enemy, board)) {
        UnmakeMove(board, move, undo);
        return false;
    }

    return true;
}

/*
 * Apply a move after fully validating its encoding against the board.
 * Use this for moves from outside the engine.
 *
 * Returns true if the move is legal and has been applied.
 * Returns false if the move is invalid or leaves the king in check: the board is untouched.
 */
inline bool MakeMove(
    Board &board,
    const Move move,
    UndoState &undo
) {
    const int from = GET_MOVE_SOURCE(move);
    const int to = GET_MOVE_TARGET(move);
    const int piece = GET_MOVE_PIECE(move);
    const int captured = GET_MOVE_CAPTURED(move);
    const int promoted = GET_MOVE_PROMOTED(move);
    const int flag = GET_MOVE_FLAG(move);

    // 1. Validate basic move information
    if (board.side != WHITE && board.side != BLACK)
        return false;

    if (from < 0 || from >= 64 ||
        to < 0 || to >= 64 ||
        from == to ||
        piece < 0 || piece >= PIECE_NB ||
        flag > MOVE_PROMOTION_CAPTURE) {
        return false;
    }

    const int side = board.side;

    if (const bool movingWhite = piece <= K; movingWhite != (side == WHITE))
        return false;

    const u64 fromBit = 1ULL << from;
    const u64 toBit = 1ULL << to;

    if (!(board.bitboards[piece] & fromBit))
        return false;

    const bool isEnPassant = flag == MOVE_EN_PASSANT;
    const bool isPromotion = flag == MOVE_PROMOTION || flag == MOVE_PROMOTION_CAPTURE;
    const bool isCastling = flag == MOVE_KING_CASTLE || flag == MOVE_QUEEN_CASTLE;

    if (captured != NO_PIECE) {
        if (captured < 0 || captured >= PIECE_NB)
            return false;

        if (const bool capturedWhite = captured <= K; capturedWhite == (side == WHITE))
            return false;
    }

    if ((flag == MOVE_CAPTURE || flag == MOVE_PROMOTION_CAPTURE) && captured == NO_PIECE)
        return false;

    if ((flag == MOVE_QUIET || flag == MOVE_DOUBLE_PAWN_PUSH ||
         flag == MOVE_PROMOTION || isCastling) && captured != NO_PIECE)
        return false;

    // 2. Validate captures and en passant
    if (isEnPassant) {
        if (const int capturedSquare = to + (side == WHITE ? -8 : 8); board.enPassantSquare != to ||
                                                                      board.occupancies[BOTH] & toBit ||
                                                                      captured != (side == WHITE ? p : P) ||
                                                                      capturedSquare < 0 || capturedSquare >= 64 ||
                                                                      !(board.bitboards[captured] & (
                                                                            1ULL << capturedSquare))) {
            return false;
        }
    } else if (captured == NO_PIECE) {
        if (board.occupancies[BOTH] & toBit)
            return false;
    } else {
        if (!(board.bitboards[captured] & toBit))
            return false;
    }

    // 3. Validate promotions
    if (isPromotion) {
        const bool validPromotedPiece =
                (side == WHITE && promoted >= N && promoted <= Q) ||
                (side == BLACK && promoted >= n && promoted <= q);

        if (const int lastRank = side == WHITE ? 7 : 0;
            (piece != P && piece != p) || !validPromotedPiece || SQ_TO_RANK(to) != lastRank) {
            return false;
        }
    } else if (promoted != 0) {
        return false;
    }

    // 4. Validate castling
    if (isCastling) {
        const bool kingSide = flag == MOVE_KING_CASTLE;
        const bool white = side == WHITE;

        const int expectedKing = white ? K : k;
        const int expectedFrom = white ? e1 : e8;
        const int expectedTo = white ? (kingSide ? g1 : c1) : kingSide ? g8 : c8;
        const int rook = white ? R : r;
        const int rookFrom = white ? (kingSide ? h1 : a1) : kingSide ? h8 : a8;
        const int requiredRight = white
                                      ? (kingSide ? CASTLE_WHITE_KING : CASTLE_WHITE_QUEEN)
                                      : kingSide ? CASTLE_BLACK_KING : CASTLE_BLACK_QUEEN;

        if (piece != expectedKing || from != expectedFrom || to != expectedTo ||
            !(board.castlingRights & requiredRight) ||
            !(board.bitboards[rook] & (1ULL << rookFrom))) {
            return false;
        }
    }

    return MakeMoveUnchecked(board, move, undo);
}

struct NullUndoState {
    int sideToMove;
    int enPassantSquare;
    int halfmoveClock;

    std::uint64_t zobristKey;
    bool zobristKeyValid;
};

// Apply a search-only null move. No piece moves.
inline void MakeNullMove(
    Board &board,
    NullUndoState &undo
) {
    undo.sideToMove = board.side;
    undo.enPassantSquare = board.enPassantSquare;
    undo.halfmoveClock = board.halfmoveClock;
    undo.zobristKey = board.zobristKey;
    undo.zobristKeyValid = board.zobristKeyValid;

    EnsurePositionKey(board);

    const ZobristKeys &zobrist = GetZobristKeys();

    std::uint64_t nextKey = board.zobristKey;

    if (board.enPassantSquare >= 0 && board.enPassantSquare < 64)
        nextKey ^= zobrist.enPassant[board.enPassantSquare];

    nextKey ^= zobrist.side;

    board.side = board.side == WHITE ? BLACK : WHITE;

    /*
     * A null move forfeits the current en-passant opportunity.
     * Resetting the halfmove clock also stops repetition detection from looking across it.
     */
    board.enPassantSquare = NO_SQ;
    board.halfmoveClock = 0;

    board.zobristKey = nextKey;
    board.zobristKeyValid = true;
}

inline void UnmakeNullMove(
    Board &board,
    const NullUndoState &undo
) {
    board.side = undo.sideToMove;
    board.enPassantSquare = undo.enPassantSquare;
    board.halfmoveClock = undo.halfmoveClock;

    board.zobristKey = undo.zobristKey;
    board.zobristKeyValid = undo.zobristKeyValid;
}

#endif // M3R_MAKEMOVE_H
