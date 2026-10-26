#pragma once
#ifndef M3R_SEARCH_H
#define M3R_SEARCH_H

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <functional>

#include "eval.h"
#include "legalmoves.h"
#include "transposition.h"

constexpr int SCORE_INFINITY = 32000;
constexpr int SCORE_MATE = 30000;

constexpr int MAX_PLY = 128;
constexpr int SCORE_MATE_BOUND = SCORE_MATE - MAX_PLY; // abs(score) >= SCORE_MATE_BOUND means MATE
constexpr int HISTORY_MAX = 16384;

// Move ordering score bands (see ScoreMoves)
constexpr int ORDER_TT = 2000000;
constexpr int ORDER_GOOD_CAPTURE = 1000000;
constexpr int ORDER_KILLER_1 = 900000;
constexpr int ORDER_KILLER_2 = 800000;
constexpr int ORDER_BAD = -100000; // anything below this is a losing capture / underpromotion

// Limits and shared state

struct SearchLimits {
    int maxDepth = 64;
    std::int64_t timeMs = 0; // 0 = no time limit
    std::uint64_t maxNodes = 0; // 0 = no node limit
    const std::atomic<bool> *stop = nullptr; // optional external stop

    /*
     * Zobrist keys of the position played before the root from the oldest, so repetitions of
     *  earlier game positions are detected. See BuildGameHistory().
     */
    std::vector<std::uint64_t> gameHistory;

    bool printInfo = false; // print a UCI-style "info" line per iteration

    int threads = 1; // search threads (Lazy SMP)

    // For the evaluation bar
    std::function<void(int depth, int score)> onDepth;
};

constexpr int MAX_THREADS = 256;

/*
 * Zobrist keys of every position that came before 'board' in the game, from the oldest to the newest.
 *
 * The current position is not included since the search adds itself. Pass the result as SearchLimits::gameHistory so
 * the search can see repetitions against positions that were actually played.
 *
 * The 'entries' are all the move played from the game's start position, from the oldest. Each element needs a '.move'
 * and a '.undo. that MakeMove() filled in. The keys are rebuilt by unmaking the moves on a copy of the board,
 * so 'board' itself is never touched.
 */
template<typename Entries> std::vector<std::uint64_t> BuildGameHistory(Board board, const Entries &entries) {
    std::vector<std::uint64_t> keys;
    keys.reserve(entries.size());

    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        UnmakeMove(board, it->move, it->undo);
        keys.push_back(PositionKey(board));
    }

    std::ranges::reverse(keys);
    return keys;
}

// State shared by every search thread of one FindBestMove() call
struct SharedSearchState {
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> nodes{0}; // flushed by each thread every 2048 nodes
};

struct SearchContext {
    std::uint64_t nodes = 0;

    SharedSearchState *shared = nullptr;
    int threadId = 0; // 0 = main thread: owns the clock, prints info and supplies the result

    // Best move from the previous completed iteration
    Move preferredRootMove = 0;

    // Two quiet beta-cutoff moves for each search ply
    Move killerMoves[MAX_PLY + 2][2]{};

    // History score indexed by side, source, and target
    int history[2][64][64]{};

    /*
     * Position keys along the current line: game history, then root, then one per ply.
     * Used for repetition detection.
     */
    std::vector<std::uint64_t> keys;
    int rootIndex = 0;

    const SearchLimits *limits = nullptr;
    std::chrono::steady_clock::time_point start;
    bool canStop = false; // only abort once depth 1 has completed
    bool stopped = false;
};

struct SearchResult {
    Move bestMove = 0;
    int score = 0;
    std::uint64_t nodes = 0;
    int depth = 0;
    bool hasMove = false;
};

inline std::int64_t ElapsedMs(const SearchContext &ctx) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - ctx.start).count();
}

/*
 * Polled every 2048 nodes.
 * Every thread watches the shared stop flag. Only the main thread enforces the time,
 * node and external limits, and raises that flag for everybody.
 */
inline void CheckStop(SearchContext &ctx) {
    if ((ctx.nodes & 2047ULL) != 0)
        return;

    auto &[stop, nodes] = *ctx.shared;
    const std::uint64_t totalNodes = nodes.fetch_add(2048, std::memory_order_relaxed) + 2048;

    if (stop.load(std::memory_order_relaxed)) {
        ctx.stopped = true;
        return;
    }

    if (ctx.threadId != 0 || !ctx.canStop)
        return;

    if (const SearchLimits &lim = *ctx.limits; (lim.maxNodes != 0 && totalNodes >= lim.maxNodes) ||
                                               (lim.stop != nullptr && lim.stop->load(std::memory_order_relaxed)) ||
                                               (lim.timeMs > 0 && ElapsedMs(ctx) >= lim.timeMs)) {
        stop.store(true, std::memory_order_relaxed);
        ctx.stopped = true;
    }
}

// LMR table: reduction[depth][moveNumber]
struct LmrTable {
    int reduction[64][64]{};

    LmrTable() {
        for (int d = 1; d < 64; ++d) {
            for (int m = 1; m < 64; ++m) {
                reduction[d][m] = static_cast<int>(0.75 + std::log(d) * std::log(m) / 2.25);
            }
        }
    }
};

inline const LmrTable g_lmr;

/*
 * Static excange evaluation (SEE)
 *
 * Returns true if the capture sequence started by 'move' on its target square gains
 * at least 'threshold' centipawns for the side making it, assuming both sides keep
 * recapturing with their least valuable piece.
 */
inline bool SeeGE(const Board &board, const Move move, const int threshold) {
    // Special moves: don't try to be clever
    if (const int flag = GET_MOVE_FLAG(move); flag == MOVE_EN_PASSANT || flag == MOVE_KING_CASTLE ||
                                              flag == MOVE_QUEEN_CASTLE || GET_MOVE_PROMOTED(move) != 0) {
        return threshold <= 0;
    }

    const int from = GET_MOVE_SOURCE(move);
    const int to = GET_MOVE_TARGET(move);
    const int piece = GET_MOVE_PIECE(move);
    const int captured = GET_MOVE_CAPTURED(move);

    int swap = PieceValue(captured) - threshold;
    if (swap < 0)
        return false;

    swap = PieceValue(piece) - swap;
    if (swap <= 0)
        return true;

    u64 occupied = board.occupancies[BOTH] ^ (1ULL << from) ^ (1ULL << to);
    int stm = piece <= K ? WHITE : BLACK;
    u64 attackers = AttackersTo(board, to, occupied);

    const u64 bishopsQueens = board.bitboards[B] | board.bitboards[b] |
                              board.bitboards[Q] | board.bitboards[q];
    const u64 rooksQueens = board.bitboards[R] | board.bitboards[r] |
                            board.bitboards[Q] | board.bitboards[q];

    int result = 1;

    for (;;) {
        stm ^= 1;
        attackers &= occupied;

        const u64 stmAttackers = attackers & board.occupancies[stm];
        if (!stmAttackers)
            break;

        result ^= 1;

        const int base = stm == WHITE ? 0 : 6;

        if (u64 bb; (bb = stmAttackers & board.bitboards[P + base])) {
            if ((swap = 100 - swap) < result) break;
            occupied ^= bb & -bb;
            attackers |= BishopAttacks(to, occupied) & bishopsQueens;
        } else if ((bb = stmAttackers & board.bitboards[N + base])) {
            if ((swap = 320 - swap) < result) break;
            occupied ^= bb & -bb;
        } else if ((bb = stmAttackers & board.bitboards[B + base])) {
            if ((swap = 330 - swap) < result) break;
            occupied ^= bb & -bb;
            attackers |= BishopAttacks(to, occupied) & bishopsQueens;
        } else if ((bb = stmAttackers & board.bitboards[R + base])) {
            if ((swap = 500 - swap) < result) break;
            occupied ^= bb & -bb;
            attackers |= RookAttacks(to, occupied) & rooksQueens;
        } else if ((bb = stmAttackers & board.bitboards[Q + base])) {
            if ((swap = 900 - swap) < result) break;
            occupied ^= bb & -bb;
            attackers |= (BishopAttacks(to, occupied) & bishopsQueens) |
                    (RookAttacks(to, occupied) & rooksQueens);
        } else {
            // King can only capture if the other side has no attackers left
            return attackers & ~board.occupancies[stm] ? (result ^ 1) != 0 : result != 0;
        }
    }

    return result != 0;
}

// Move ordering

/*
 * A quiet move is neither a capture nor a promotion.
 * En passant is a capture.
 */
inline bool IsQuietMove(const Move move) {
    return GET_MOVE_CAPTURED(move) == NO_PIECE &&
           GET_MOVE_PROMOTED(move) == 0;
}

/*
 * Score every move once.
 * Bands: TT move > winning captures (MVV-LVA) > killers > history quiets > losing captures / underpromotions
 */
inline void ScoreMoves(
    const Board &board,
    const MoveList &list,
    int *scores,
    const Move preferredMove,
    const SearchContext &ctx,
    const int ply
) {
    for (int i = 0; i < list.count; ++i) {
        const Move move = list.moves[i];

        if (move == preferredMove && move != 0) {
            scores[i] = ORDER_TT;
            continue;
        }

        const int piece = GET_MOVE_PIECE(move);
        const int captured = GET_MOVE_CAPTURED(move);

        if (const int promoted = GET_MOVE_PROMOTED(move); captured != NO_PIECE || promoted != 0) {
            const int mvvLva = 10 * PieceValue(captured) - PieceValue(piece) + PieceValue(promoted);

            bool good;

            if (promoted != 0)
                good = promoted % 6 == Q; // underpromotions are rarely best
            else
                good = PieceValue(captured) >= PieceValue(piece) || SeeGE(board, move, 0);

            scores[i] = good ? ORDER_GOOD_CAPTURE + mvvLva : ORDER_BAD + mvvLva;
            continue;
        }

        if (ctx.killerMoves[ply][0] == move) {
            scores[i] = ORDER_KILLER_1;
        } else if (ctx.killerMoves[ply][1] == move) {
            scores[i] = ORDER_KILLER_2;
        } else {
            const int side = piece <= K ? WHITE : BLACK;
            scores[i] = ctx.history[side][GET_MOVE_SOURCE(move)][GET_MOVE_TARGET(move)];
        }
    }
}

/*
 * Selection step: swap the best remaining move into slot 'index'
 * Most nodes cut off after one or two moves, so a full sort is wasted work.
 */
inline Move PickMove(MoveList &list, int *scores, const int index) {
    int best = index;

    for (int j = index + 1; j < list.count; ++j) {
        if (scores[j] > scores[best])
            best = j;
    }

    std::swap(list.moves[index], list.moves[best]);
    std::swap(scores[index], scores[best]);

    return list.moves[index];
}

inline void UpdateHistory(int &entry, const int bonus) {
    // "Gravity": entries approach +-HISTORY_MAX smoothly instead of overflowing
    entry += bonus - entry * std::abs(bonus) / HISTORY_MAX;
}

// Reward a quiet move that causes a cutoff and penalise the quiet moves that were tried and failed before it
inline void RecordQuietCutoff(
    SearchContext &ctx,
    const Move move,
    const Move *quietsTried,
    const int quietCount,
    const int side,
    const int ply,
    const int depth
) {
    if (ctx.killerMoves[ply][0] != move) {
        ctx.killerMoves[ply][1] = ctx.killerMoves[ply][0];
        ctx.killerMoves[ply][0] = move;
    }

    const int bonus = std::min(32 * depth * depth, 2400);

    UpdateHistory(ctx.history[side][GET_MOVE_SOURCE(move)][GET_MOVE_TARGET(move)], bonus);

    for (int i = 0; i < quietCount; ++i) {
        if (const Move other = quietsTried[i]; other != move) {
            UpdateHistory(ctx.history[side][GET_MOVE_SOURCE(other)][GET_MOVE_TARGET(other)], -bonus);
        }
    }
}

/*
 * Repetition: has this exact position occurred earlier in the current line since the last irreversible move?
 *
 *  - A repeat inside the search tree (index >= rootIndex) is scored as a draw.
 *    If one side can repeat once it can repeat again, so treating it as a draw is the standard and safe convention.
 *
 *  - A repeat of a position from the game before the root only counts once that position has already occurred twice
 *   there, otherwise the engine would think a position that has happened only once in the real game is already a draw.
 */
inline bool IsRepetition(const SearchContext &ctx, const int index, const int halfmoveClock) {
    const int limit = std::max(0, index - halfmoveClock);
    int earlierInGame = 0;

    for (int i = index - 2; i >= limit; i -= 2) {
        if (ctx.keys[i] != ctx.keys[index])
            continue;

        if (i >= ctx.rootIndex)
            return true;

        if (++earlierInGame >= 2)
            return true;
    }

    return false;
}

// Convert a search score to its transposition-table representation
inline int ScoreToTT(const int score, const int ply) {
    if (score >= SCORE_MATE_BOUND)
        return score + ply;

    if (score <= -SCORE_MATE_BOUND)
        return score - ply;

    return score;
}

// Restore a stored score for the current ply
inline int ScoreFromTT(const int score, const int ply) {
    if (score >= SCORE_MATE_BOUND)
        return score - ply;

    if (score <= -SCORE_MATE_BOUND)
        return score + ply;

    return score;
}

/*
 * Quiescence search
 *
 * At a quiet position, stand pat on the specific evaluation and search only captures with SEE and delta pruning.
 * If in check, search all evasions.
 */
inline int Quiescence(
    Board &board,
    int alpha,
    const int beta,
    const int ply,
    SearchContext &ctx) {
    ++ctx.nodes;
    CheckStop(ctx);

    if (ctx.stopped)
        return 0;

    if (ply >= MAX_PLY - 1)
        return Evaluate(board);

    const bool isPV = beta - alpha > 1;
    const int alphaOrig = alpha;

    // Probe the transposition table
    EnsurePositionKey(board);
    const std::uint64_t key = board.zobristKey;

    Move ttMove = 0;

    if (TTEntry tt{}; g_transpositionTable.Probe(key, tt)) {
        ttMove = tt.bestMove;

        if (!isPV) {
            const int ttScore = ScoreFromTT(tt.score, ply);

            if (const TTBound bound = tt.Bound(); bound == TTBound::Exact ||
                                                  (bound == TTBound::Lower && ttScore >= beta) ||
                                                  (bound == TTBound::Upper && ttScore <= alpha)) {
                return ttScore;
            }
        }
    }

    const bool inCheck = IsKingInCheck(board, board.side);

    int best = -SCORE_INFINITY;
    int standPat = -SCORE_INFINITY;
    Move bestMove = 0;

    if (!inCheck) {
        standPat = Evaluate(board);

        if (standPat >= beta)
            return standPat;

        best = standPat;

        if (standPat > alpha)
            alpha = standPat;
    }

    MoveList moves = inCheck ? GeneratePseudoLegalMoves(board) : GenerateCaptureMoves(board);

    int scores[256];
    ScoreMoves(board, moves, scores, ttMove, ctx, ply);

    int legalMoves = 0;

    for (int i = 0; i < moves.count; ++i) {
        const Move move = PickMove(moves, scores, i);

        if (!inCheck) {
            // Delta pruning: even winning the victim cannot reach alpha
            if (GET_MOVE_PROMOTED(move) == 0 &&
                standPat + PieceValue(GET_MOVE_CAPTURED(move)) + 200 <= alpha) {
                continue;
            }

            // Skip captures that lose material outright
            if (scores[i] < ORDER_BAD / 2)
                continue;
        }

        UndoState undo{};

        if (!MakeMoveUnchecked(board, move, undo))
            continue;

        ++legalMoves;

        const int score = -Quiescence(board, -beta, -alpha, ply + 1, ctx);

        UnmakeMove(board, move, undo);

        if (ctx.stopped)
            return 0;

        if (score > best) {
            best = score;
            bestMove = move;

            if (score > alpha) {
                alpha = score;

                if (alpha >= beta)
                    break;
            }
        }
    }

    // In check with no legal evasion: checkmate
    if (inCheck && legalMoves == 0)
        return -SCORE_MATE + ply;

    // Store the result
    const TTBound bound = best >= beta
                              ? TTBound::Lower
                              : best > alphaOrig
                                    ? TTBound::Exact
                                    : TTBound::Upper;

    g_transpositionTable.Store(key, 0, ScoreToTT(best, ply), bound, bestMove);

    return best;
}

inline bool HasNonPawnMaterial(const Board &board, const int side) {
    const int base = side == WHITE ? 0 : 6;

    return (board.bitboards[N + base] | board.bitboards[B + base] |
            board.bitboards[R + base] | board.bitboards[Q + base]) != 0ULL;
}

/*
 * Negamax with
*  -  alpha-beta,
*  -  PVS,
*  -  TT,
*  -  check extensions,
*  -  mate-distance pruning,
*  -  reverse futility,
*  -  null-move,
*  -  IIR,
*  -  LMP,
*  -  futility,
*  -  SEE pruning,
*  -  LMR
*
* Returns a score from the current side's POV.
 */
inline int Negamax(
    Board &board,
    int depth,
    int alpha,
    int beta,
    const int ply,
    SearchContext &ctx,
    Move &rootBestMove,
    bool &rootHasMove,
    const bool allowNull = true
) {
    const bool isRoot = ply == 0;
    const bool isPV = beta - alpha > 1;
    const int sideToMove = board.side;

    if (ply >= MAX_PLY - 1)
        return Evaluate(board);

    const bool inCheck = IsKingInCheck(board, sideToMove);

    // Check extension: never drop into quiescence while in check
    if (inCheck)
        ++depth;

    if (depth <= 0)
        return Quiescence(board, alpha, beta, ply, ctx);

    ++ctx.nodes;
    CheckStop(ctx);

    if (ctx.stopped)
        return 0;

    EnsurePositionKey(board);
    const std::uint64_t key = board.zobristKey;
    ctx.keys[ctx.rootIndex + ply] = key;

    if (!isRoot) {
        // Mate-distance pruning: no line can beat a mate we already have
        alpha = std::max(alpha, -SCORE_MATE + ply);
        beta = std::min(beta, SCORE_MATE - ply - 1);

        if (alpha >= beta)
            return alpha;

        // Draws: repetition, fifty-move rule, insufficient material
        if (IsRepetition(ctx, ctx.rootIndex + ply, board.halfmoveClock) ||
            IsInsufficientMaterial(board)) {
            return 0;
        }

        // At 100 plies the game is drawn, unless this position is checkmate
        if (board.halfmoveClock >= 100 && (!inCheck || HasAnyLegalMove(board)))
            return 0;
    }

    const int alphaOriginal = alpha;
    const int betaOriginal = beta;

    // 1. Probe the transposition table
    Move ttMove = 0;

    if (TTEntry tt{}; g_transpositionTable.Probe(key, tt)) {
        ttMove = tt.bestMove;

        if (!isPV && tt.depth >= depth) {
            const int ttScore = ScoreFromTT(tt.score, ply);

            if (const TTBound bound = tt.Bound(); bound == TTBound::Exact ||
                                                  (bound == TTBound::Lower && ttScore >= beta) ||
                                                  (bound == TTBound::Upper && ttScore <= alpha)) {
                return ttScore;
            }
        }
    }

    const int staticEval = inCheck ? -SCORE_INFINITY : Evaluate(board);

    // 2. Whole-node pruning (never in PV nodes or when in check)
    if (!isPV && !inCheck) {
        const bool notMateWindow = beta > -SCORE_MATE_BOUND && beta < SCORE_MATE_BOUND;

        /*
         * Reverse futility: the position is so far above beta that
         * even losing a margin per remaining ply would still fail high.
         */
        if (depth <= 6 && notMateWindow && staticEval - 90 * depth >= beta)
            return staticEval - 90 * depth;

        // Null-move pruning: if passing the move still beats beta, a real move will
        if (allowNull && depth >= 3 && notMateWindow &&
            staticEval >= beta && HasNonPawnMaterial(board, sideToMove)) {
            NullUndoState nullUndo{};

            MakeNullMove(board, nullUndo);

            const int reduction = 3 + depth / 3 + std::min((staticEval - beta) / 200, 3);

            const int nullScore = -Negamax(
                board, depth - 1 - reduction,
                -beta, -beta + 1,
                ply + 1, ctx,
                rootBestMove, rootHasMove,
                false // no two null moves in a row
            );

            UnmakeNullMove(board, nullUndo);

            if (ctx.stopped)
                return 0;

            if (nullScore >= beta)
                return nullScore >= SCORE_MATE_BOUND ? beta : nullScore;
        }
    }

    /*
     * Internal iterative reduction: with no hash move, ordering is poor,
     * so search a bit shallower and let the TT fill in a standard move
     */
    if (depth >= 4 && ttMove == 0)
        --depth;

    // 3. Generate and score moves
    MoveList moves = GeneratePseudoLegalMoves(board);

    const Move preferredMove = isRoot && ctx.preferredRootMove != 0
                                   ? ctx.preferredRootMove
                                   : ttMove;

    int scores[256];
    ScoreMoves(board, moves, scores, preferredMove, ctx, ply);

    int legalMoves = 0;
    int bestScore = -SCORE_INFINITY;
    Move bestMove = 0;

    Move quietsTried[64];
    int quietCount = 0;

    // 4. Search moves (PVS + LMR)
    for (int i = 0; i < moves.count; ++i) {
        const Move move = PickMove(moves, scores, i);
        const int moveScore = scores[i];
        const bool quiet = IsQuietMove(move);

        /*
         * Pruning is of late or helpless moves.
         * Only once legal move is searched (so mate and stalemate detection stays correct) and never
         * in PV nodes, in check or when we are already mated.
         */
        if (!isPV && !inCheck && legalMoves > 0 && bestScore > -SCORE_MATE_BOUND) {
            if (quiet) {
                // Late-move pruning
                if (depth <= 4 && quietCount >= 3 + depth * depth)
                    continue;

                // Futility pruning
                if (depth <= 5 && staticEval + 100 + 120 * depth <= alpha)
                    continue;
            } else if (moveScore < ORDER_BAD / 2 && depth <= 4 &&
                       !SeeGE(board, move, -80 * depth)) {
                continue;
            }
        }

        UndoState undo{};

        if (!MakeMoveUnchecked(board, move, undo))
            continue;

        g_transpositionTable.Prefetch(board.zobristKey);

        ++legalMoves;

        if (quiet && quietCount < 64)
            quietsTried[quietCount++] = move;

        const int newDepth = depth - 1;
        int score;

        if (legalMoves == 1) {
            // First move: full window
            score = -Negamax(board, newDepth, -beta, -alpha, ply + 1, ctx,
                             rootBestMove, rootHasMove);
        } else {
            int reduction = 0;

            if (depth >= 3 && legalMoves >= (isPV ? 4 : 3) && !inCheck &&
                (quiet || moveScore < ORDER_BAD / 2) &&
                !IsKingInCheck(board, board.side)) {
                reduction = g_lmr.reduction[std::min(depth, 63)][std::min(legalMoves, 63)];

                if (isPV)
                    --reduction;

                if (quiet) {
                    const int side = sideToMove;
                    const int hist = ctx.history[side][GET_MOVE_SOURCE(move)][GET_MOVE_TARGET(move)];

                    reduction -= hist / 8192; // good history -> reduce less
                }

                reduction = std::clamp(reduction, 0, newDepth - 1);
            }

            // Reduced null-window search
            score = -Negamax(board, newDepth - reduction, -alpha - 1, -alpha, ply + 1, ctx,
                             rootBestMove, rootHasMove);

            // Verify at full depth if it unexpectedly beat alpha
            if (score > alpha && reduction > 0) {
                score = -Negamax(board, newDepth, -alpha - 1, -alpha, ply + 1, ctx,
                                 rootBestMove, rootHasMove);
            }

            // Then re-search with the full window only if it may be the new best
            if (score > alpha && score < beta) {
                score = -Negamax(board, newDepth, -beta, -alpha, ply + 1, ctx,
                                 rootBestMove, rootHasMove);
            }
        }

        UnmakeMove(board, move, undo);

        // Never trust or store results from an aborted search
        if (ctx.stopped)
            return 0;

        if (score > bestScore) {
            bestScore = score;
            bestMove = move;

            if (isRoot) {
                rootBestMove = move;
                rootHasMove = true;
            }
        }

        if (score > alpha) {
            alpha = score;

            if (alpha >= beta) {
                if (quiet)
                    RecordQuietCutoff(ctx, move, quietsTried, quietCount, sideToMove, ply, depth);

                break;
            }
        }
    }

    // 5. Checkmate and stalemate
    if (legalMoves == 0) {
        const int terminalScore = inCheck ? -SCORE_MATE + ply : 0;

        g_transpositionTable.Store(key, depth, ScoreToTT(terminalScore, ply), TTBound::Exact, 0);

        return terminalScore;
    }

    // 6. Store the result and its alpha-beta bound
    auto bound = TTBound::Exact;

    if (bestScore <= alphaOriginal)
        bound = TTBound::Upper; // failed low: score is an upper bound
    else if (bestScore >= betaOriginal)
        bound = TTBound::Lower; // failed high: score is a lower bound

    g_transpositionTable.Store(key, depth, ScoreToTT(bestScore, ply), bound, bestMove);

    return bestScore;
}

// UCI helpers

// Convert a square index to algebraic coordinates
inline std::string SquareToString(const int sq) {
    if (sq < 0 || sq >= 64)
        return "?";

    std::string result;
    result += static_cast<char>('a' + SQ_TO_FILE(sq));
    result += static_cast<char>('1' + SQ_TO_RANK(sq));

    return result;
}

// Convert a move to UCI notation
inline std::string MoveToUci(const Move move) {
    std::string result =
            SquareToString(GET_MOVE_SOURCE(move)) +
            SquareToString(GET_MOVE_TARGET(move));

    if (const int promoted = GET_MOVE_PROMOTED(move); promoted != 0 && promoted < PIECE_NB) {
        char c = PIECE_CHARS[promoted];

        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');

        result += c;
    }

    return result;
}

// Follow the transposition table from the root to print a principal variation.
inline std::string ExtractPV(Board board, const Move first, const int maxLength) {
    std::string pv = MoveToUci(first);

    UndoState undo{};
    if (!MakeMove(board, first, undo))
        return pv;

    for (int i = 1; i < maxLength; ++i) {
        TTEntry tt{};

        if (!g_transpositionTable.Probe(PositionKey(board), tt) || tt.bestMove == 0)
            break;

        if (!MakeMove(board, tt.bestMove, undo))
            break;

        pv += ' ' + MoveToUci(tt.bestMove);
    }

    return pv;
}

inline std::string ScoreToString(const int score) {
    if (score >= SCORE_MATE_BOUND)
        return "mate " + std::to_string((SCORE_MATE - score + 1) / 2);

    if (score <= -SCORE_MATE_BOUND)
        return "mate -" + std::to_string((SCORE_MATE + score + 1) / 2);

    return "cp " + std::to_string(score);
}

/*
 * Lazy SMP depth skipping: helper threads search different depth sequences so they do not all
 * duplicate the main thread's work. Which depths they skip depends on their id
 */
inline bool SkipDepth(const int threadId, const int depth) {
    static constexpr int SKIP_SIZE[20] = {1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4};
    static constexpr int SKIP_PHASE[20] = {0, 1, 0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 6, 7};

    const int i = (threadId - 1) % 20;
    return (depth + SKIP_PHASE[i]) / SKIP_SIZE[i] % 2 != 0;
}

/*
 * Iterative deepening with aspiration windows, run by every search thread.
 * Thread 0 also manages the clock and prints info.
 * 'board' must be this thread's own copy. It is restored to the root position afterward.
 */
inline SearchResult IterativeDeepening(Board &board, SearchContext &context, const int maxDepth) {
    const SearchLimits &limits = *context.limits;
    const bool isMain = context.threadId == 0;

    SearchResult result{};

    for (int depth = 1; depth <= maxDepth; ++depth) {
        if (isMain) {
            // Don't start an iteration we are unlikely to finish
            if (depth > 1 && limits.timeMs > 0 && ElapsedMs(context) * 2 >= limits.timeMs)
                break;
        } else {
            if (context.shared->stop.load(std::memory_order_relaxed))
                break;

            if (SkipDepth(context.threadId, depth))
                continue;
        }

        context.preferredRootMove = result.hasMove ? result.bestMove : 0;

        int alpha = -SCORE_INFINITY;
        int beta = SCORE_INFINITY;
        int delta = 25;

        // Aspiration window around the previous score
        if (depth >= 5) {
            alpha = std::max(-SCORE_INFINITY, result.score - delta);
            beta = std::min(SCORE_INFINITY, result.score + delta);
        }

        int score = 0;
        Move iterationBestMove = 0;
        bool iterationHasMove = false;

        for (;;) {
            iterationBestMove = 0;
            iterationHasMove = false;

            score = Negamax(board, depth, alpha, beta, 0, context,
                            iterationBestMove, iterationHasMove);

            if (context.stopped)
                break;

            if (score <= alpha && alpha > -SCORE_INFINITY) {
                alpha = std::max(-SCORE_INFINITY, alpha - delta);
                delta = std::min(delta * 2, SCORE_INFINITY);
                continue;
            }

            if (score >= beta && beta < SCORE_INFINITY) {
                beta = std::min(SCORE_INFINITY, beta + delta);
                delta = std::min(delta * 2, SCORE_INFINITY);
                continue;
            }

            break;
        }

        // An aborted iteration is discarded. Keep the last completed one
        if (context.stopped)
            break;

        result.score = score;
        result.nodes = context.nodes;
        result.depth = depth;

        if (!iterationHasMove) {
            result.bestMove = 0;
            result.hasMove = false;
            break;
        }

        result.bestMove = iterationBestMove;
        result.hasMove = true;
        context.canStop = true;

        if (isMain && limits.printInfo) {
            const auto ms = std::max<std::int64_t>(1, ElapsedMs(context));
            const std::uint64_t nodes = context.shared->nodes.load(std::memory_order_relaxed);

            std::cout << "info depth " << depth
                    << " score " << ScoreToString(score)
                    << " nodes " << nodes
                    << " nps " << nodes * 1000 / static_cast<std::uint64_t>(ms)
                    << " time " << ms
                    << " pv " << ExtractPV(board, result.bestMove, depth)
                    << std::endl;
        }

        // A forced mate is already found: deeper search cannot improve it
        if (std::abs(score) >= SCORE_MATE_BOUND && depth >= SCORE_MATE - std::abs(score) + 1)
            break;
    }

    result.nodes = context.nodes;
    return result;
}

/*
 * Lazy SMP: every thread runs its own iterative deepening on its own copy of the board.
 * The only thing they share is the transposition table (and the stop flag).
 * The result comes from thread 0.
 */
inline SearchResult FindBestMove(
    Board &board,
    const SearchLimits &limits
) {
    EnsurePositionKey(board);
    g_transpositionTable.NewSearch();

    if (limits.maxDepth < 1) {
        SearchResult result{};
        result.score = Evaluate(board);
        return result;
    }

    const int maxDepth = std::min(limits.maxDepth, MAX_PLY - 8);
    const int threadCount = std::clamp(limits.threads, 1, MAX_THREADS);

    SharedSearchState shared;
    const auto start = std::chrono::steady_clock::now();

    // Copy the root for every helper BEFORE any thread runs: thread 0 mutates 'board' while searching
    std::vector helperBoards(static_cast<std::size_t>(threadCount - 1), board);
    std::vector<SearchContext> contexts(static_cast<std::size_t>(threadCount));

    for (int id = 0; id < threadCount; ++id) {
        SearchContext &ctx = contexts[static_cast<std::size_t>(id)];

        ctx.limits = &limits;
        ctx.shared = &shared;
        ctx.threadId = id;
        ctx.start = start;

        ctx.keys = limits.gameHistory;
        ctx.rootIndex = static_cast<int>(ctx.keys.size());
        ctx.keys.resize(ctx.keys.size() + MAX_PLY + 2, 0);
    }

    std::vector<std::thread> helpers;
    helpers.reserve(helperBoards.size());

    for (int id = 1; id < threadCount; ++id) {
        helpers.emplace_back([&, id] {
            IterativeDeepening(helperBoards[static_cast<std::size_t>(id - 1)],
                               contexts[static_cast<std::size_t>(id)], maxDepth);
        });
    }

    SearchResult result = IterativeDeepening(board, contexts[0], maxDepth);

    // Thread 0 is done (limit hit or max depth reached): tell the helpers to stop and wait for them
    shared.stop.store(true, std::memory_order_relaxed);

    for (std::thread &t: helpers)
        t.join();

    result.nodes = 0;
    for (const SearchContext &ctx: contexts)
        result.nodes += ctx.nodes;

    return result;
}

// Convenience overload: fixed-depth search with no other limits
inline SearchResult FindBestMove(
    Board &board,
    const int maxDepth
) {
    SearchLimits limits;
    limits.maxDepth = maxDepth;

    return FindBestMove(board, limits);
}

#endif // M3R_SEARCH_H