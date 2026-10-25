#pragma once

#ifndef M3R_BITBOARD_H
#define M3R_BITBOARD_H

#include <cstdint>
#include <sstream>
#include <string>

#include "move.h"

typedef uint64_t u64;

enum Color { WHITE, BLACK, BOTH, COLOR_NB = 2 };

enum Piece { P, N, B, R, Q, K, p, n, b, r, q, k, PIECE_NB = 12 };

enum Square {
    a1, b1, c1, d1, e1, f1, g1, h1,
    a2, b2, c2, d2, e2, f2, g2, h2,
    a3, b3, c3, d3, e3, f3, g3, h3,
    a4, b4, c4, d4, e4, f4, g4, h4,
    a5, b5, c5, d5, e5, f5, g5, h5,
    a6, b6, c6, d6, e6, f6, g6, h6,
    a7, b7, c7, d7, e7, f7, g7, h7,
    a8, b8, c8, d8, e8, f8, g8, h8,
    NO_SQ
};

enum CastlingRight : int {
    CASTLE_WHITE_KING = 1,
    CASTLE_WHITE_QUEEN = 2,
    CASTLE_BLACK_KING = 4,
    CASTLE_BLACK_QUEEN = 8
};

// Rank and file masks
constexpr u64 FILE_A = 0x0101010101010101ULL;
constexpr u64 FILE_H = FILE_A << 7;
constexpr u64 RANK_1 = 0xFFULL;
constexpr u64 RANK_3 = RANK_1 << 16;
constexpr u64 RANK_6 = RANK_1 << 40;
constexpr u64 RANK_8 = RANK_1 << 56;

// Bit manipulation macros
#define SET_BIT(bitboard, sq)   ((bitboard) |= (1ULL << (sq)))
#define GET_BIT(bitboard, sq)   ((bitboard) & (1ULL << (sq)))
#define POP_BIT(bitboard, sq)   ((bitboard) &= ~(1ULL << (sq)))

// Board and square macros
#define SQ_TO_RANK(sq)          ((sq) >> 3)
#define SQ_TO_FILE(sq)          ((sq) & 7)
#define MAKE_SQ(file, rank)     (((rank) << 3) + (file))

inline int PopCount(const u64 bb) { return __builtin_popcountll(bb); }

// Board representation
struct Board {
    // Bitboards for each piece type:
    //  White: 0-5
    //  Black: 6-11
    u64 bitboards[12]{};

    // Occupancy bitboards: {WHITE, BLACK, BOTH}
    u64 occupancies[3]{};

    // Piece on every square (NO_PIECE when empty). O(1) efficiency
    uint8_t mailbox[64]{};

    // Side to move
    int side = WHITE;

    // Castling
    int castlingRights = 0;
    int enPassantSquare = NO_SQ;

    // Plies since the last capture or pawn move for the fifty move rule
    int halfmoveClock = 0;

    // Cached Zobrist position hash
    u64 zobristKey = 0;
    bool zobristKeyValid = false;

    Board() {
        for (uint8_t &m: mailbox)
            m = static_cast<uint8_t>(NO_PIECE);
    }
};

// Safely sets a bit on a specific bitboard using macros
inline bool SetBoardBit(u64 &bitboard, const int sq) {
    if (sq < 0 || sq > 63) return false;
    SET_BIT(bitboard, sq);
    return true;
}

// Full recomputation of occupancies and the mailbox from the piece bitboards.
// MakeMove()/UnmakeMove() update both incrementally; call this only after
// editing bitboards by hand (e.g. while parsing a FEN).
/*
 * Full recomputation of occupancies and the mailbox from the piece bitboards.
 * MakeMove() and UnmakeMove() update both incrementally. Call this only after editing bitboards by hand:
 *  E.G. while parsing a FEN.
 */
inline void UpdateOccupancies(Board &board) {
    board.occupancies[WHITE] = 0ULL;
    board.occupancies[BLACK] = 0ULL;

    for (uint8_t &m: board.mailbox)
        m = static_cast<uint8_t>(NO_PIECE);

    for (int piece = 0; piece < PIECE_NB; ++piece) {
        u64 bb = board.bitboards[piece];
        board.occupancies[piece <= K ? WHITE : BLACK] |= bb;

        while (bb) {
            board.mailbox[__builtin_ctzll(bb)] = static_cast<uint8_t>(piece);
            bb &= bb - 1;
        }
    }

    board.occupancies[BOTH] = board.occupancies[WHITE] | board.occupancies[BLACK];
}

/*
 * ASCII characters for pieces
 *   Uppercase = White
 *   Lowercase = Black
 */
inline constexpr char PIECE_CHARS[12] = {
    'P', 'N', 'B', 'R', 'Q', 'K',
    'p', 'n', 'b', 'r', 'q', 'k'
};

//
inline void ResetBoard(Board &board) {
    for (u64 &bitboard: board.bitboards) bitboard = 0ULL;
    board.occupancies[WHITE] = 0ULL;
    board.occupancies[BLACK] = 0ULL;
    board.occupancies[BOTH] = 0ULL;
    for (uint8_t &m: board.mailbox) m = static_cast<uint8_t>(NO_PIECE);
    board.side = WHITE;
    board.castlingRights = 0;
    board.enPassantSquare = NO_SQ;
    board.halfmoveClock = 0;
    board.zobristKey = 0;
    board.zobristKeyValid = false;
}

// Parse a character to a piece index
inline int CharToPiece(const char c) {
    switch (c) {
        case 'P': return P;
        case 'N': return N;
        case 'B': return B;
        case 'R': return R;
        case 'Q': return Q;
        case 'K': return K;
        case 'p': return p;
        case 'n': return n;
        case 'b': return b;
        case 'r': return r;
        case 'q': return q;
        case 'k': return k;
        default: return -1;
    }
}

// Parse a FEN string and populates a Board's bitboards
inline void ParseFEN(Board &board, const std::string &fen) {
    ResetBoard(board);

    std::istringstream input(fen);

    std::string placement;

    if (!(input >> placement))
        return;

    int rank = 7;
    int file = 0;

    for (const char c: placement) {
        if (c == '/') {
            --rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
        } else {
            if (const int piece = CharToPiece(c); piece != -1 &&
                                                  rank >= 0 && rank < 8 &&
                                                  file >= 0 && file < 8) {
                SET_BIT(
                    board.bitboards[piece],
                    MAKE_SQ(file, rank)
                );

                ++file;
            }
        }
    }

    // Active color
    if (std::string activeColor; input >> activeColor)
        board.side = activeColor == "b" ? BLACK : WHITE;

    // Castling rights
    if (std::string castling; input >> castling) {
        for (const char c: castling) {
            switch (c) {
                case 'K': board.castlingRights |= CASTLE_WHITE_KING;
                    break;
                case 'Q': board.castlingRights |= CASTLE_WHITE_QUEEN;
                    break;
                case 'k': board.castlingRights |= CASTLE_BLACK_KING;
                    break;
                case 'q': board.castlingRights |= CASTLE_BLACK_QUEEN;
                    break;
                default: break;
            }
        }
    }

    // En-passant target square
    if (std::string enPassant; input >> enPassant && enPassant != "-") {
        if (enPassant.size() == 2 &&
            enPassant[0] >= 'a' && enPassant[0] <= 'h' &&
            enPassant[1] >= '1' && enPassant[1] <= '8') {
            const int epFile = enPassant[0] - 'a';
            const int epRank = enPassant[1] - '1';

            board.enPassantSquare = MAKE_SQ(epFile, epRank);
        }
    }

    // Halfmove clock counter
    if (int halfmove = 0; input >> halfmove && halfmove >= 0)
        board.halfmoveClock = halfmove;

    UpdateOccupancies(board);
}

#endif // M3R_BITBOARD_H