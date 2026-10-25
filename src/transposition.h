#pragma once
#ifndef M3R_TRANSPOSITION_H
#define M3R_TRANSPOSITION_H

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "board.h"
#include "move.h"

// Zobrist hashing
struct ZobristKeys {
    std::uint64_t pieces[PIECE_NB][64]{};
    std::uint64_t side = 0;
    std::uint64_t castling[16]{};
    std::uint64_t enPassant[64]{};
};

// Deterministic pseudo-random 64-bit number generator
inline std::uint64_t SplitMix64(std::uint64_t &state) {
    std::uint64_t z = state += 0x9E3779B97F4A7C15ULL;

    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;

    return z ^ (z >> 31);
}

inline ZobristKeys BuildZobristKeys() {
    ZobristKeys result{};

    std::uint64_t seed = 0x4D33525F43484553ULL;

    for (auto &pieceKeys: result.pieces) {
        for (auto &key: pieceKeys)
            key = SplitMix64(seed);
    }

    result.side = SplitMix64(seed);

    for (auto &key: result.castling)
        key = SplitMix64(seed);

    for (auto &key: result.enPassant)
        key = SplitMix64(seed);

    return result;
}

/*
 * A plain global instead of a function-local static:
 *  no thread-safe-init guard check on every access
 */
inline const ZobristKeys g_zobristKeys = BuildZobristKeys();

inline const ZobristKeys &GetZobristKeys() {
    return g_zobristKeys;
}

/*
 * Compute the key from the current board state:
 *  piece placement, side to move, castling rights and en-passant square.
 */
inline std::uint64_t ComputePositionKey(const Board &board) {
    const auto &[zPieces, zSide, zCastling, zEnPassant] = GetZobristKeys();

    std::uint64_t key = 0;

    for (int piece = 0; piece < PIECE_NB; ++piece) {
        u64 pieces = board.bitboards[piece];

        while (pieces) {
            const int sq = __builtin_ctzll(pieces);
            pieces &= pieces - 1;
            key ^= zPieces[piece][sq];
        }
    }

    if (board.side == BLACK)
        key ^= zSide;

    key ^= zCastling[static_cast<unsigned int>(board.castlingRights) & 0xFU];

    if (board.enPassantSquare >= 0 && board.enPassantSquare < 64)
        key ^= zEnPassant[board.enPassantSquare];

    return key;
}

// Compute and cache the hash when it is not yet valid
inline void EnsurePositionKey(Board &board) {
    if (!board.zobristKeyValid) {
        board.zobristKey = ComputePositionKey(board);
        board.zobristKeyValid = true;
    }
}

// Return the cached hash when available
inline std::uint64_t PositionKey(const Board &board) {
    return board.zobristKeyValid
               ? board.zobristKey
               : ComputePositionKey(board);
}

// Transposition table
enum class TTBound : std::uint8_t {
    Exact,
    Lower,
    Upper
};

// Decoded view of a table slot. This is what the search sees; it is never stored directly
struct TTEntry {
    std::uint64_t key = 0;
    Move bestMove = 0;
    std::int16_t score = 0;
    std::int8_t depth = -1; // < 0 means empty
    std::uint8_t ageBound = 0; // age << 2 | bound

    [[nodiscard]] TTBound Bound() const { return static_cast<TTBound>(ageBound & 3U); }
    [[nodiscard]] unsigned Age() const { return ageBound >> 2; }
};

/*
 * Lockless slot (the "XOR trick").
 *
 * Several search threads read and write the table at the same time, and a 16-byte entry cannot be
 * written atomically. Instead the payload is packed into one 64-bit word, and the key is stored XORed with it.
 * A torn read (key from one write, data from another) fails the key check on probe and is simply treated as a miss.
 *
 * data layout: bits 0-31 move | 32-47 score | 48-55 depth + 1 (0 = empty) | 56-63 age << 2 | bound
 */
struct TTSlot {
    std::atomic<std::uint64_t> keyXorData{0};
    std::atomic<std::uint64_t> data{0};
};

inline std::uint64_t PackTTData(const TTEntry &e) {
    return static_cast<std::uint64_t>(e.bestMove)
           | static_cast<std::uint64_t>(static_cast<std::uint16_t>(e.score)) << 32
           | static_cast<std::uint64_t>(static_cast<std::uint8_t>(e.depth + 1)) << 48
           | static_cast<std::uint64_t>(e.ageBound) << 56;
}

inline TTEntry UnpackTTData(const std::uint64_t key, const std::uint64_t data) {
    TTEntry e;
    e.key = key;
    e.bestMove = static_cast<Move>(data & 0xFFFFFFFFULL);
    e.score = static_cast<std::int16_t>((data >> 32) & 0xFFFFU);
    e.depth = static_cast<std::int8_t>(static_cast<int>((data >> 48) & 0xFFU) - 1);
    e.ageBound = static_cast<std::uint8_t>(data >> 56);
    return e;
}

// Two slots per bucket, two buckets per cache line
struct alignas(32) TTBucket {
    TTSlot slots[2];
};

class TranspositionTable {
public:
    explicit TranspositionTable(const std::size_t megabytes = 64) {
        Resize(megabytes);
    }

    // Resize also clears the table. Never call while a search is running
    void Resize(const std::size_t megabytes) {
        const std::size_t bytes = megabytes * 1024ULL * 1024ULL;
        size_ = std::max<std::size_t>(1, bytes / sizeof(TTBucket));

        buckets_ = std::make_unique<TTBucket[]>(size_); // value-initialised: every slot empty
        age_ = 0;
    }

    void Clear() {
        for (std::size_t i = 0; i < size_; ++i) {
            for (auto &[keyXorData, data]: buckets_[i].slots) {
                keyXorData.store(0, std::memory_order_relaxed);
                data.store(0, std::memory_order_relaxed);
            }
        }

        age_ = 0;
    }

    // Call once per search, from the main thread before helpers start
    void NewSearch() { age_ = age_ + 1 & 63U; }

    void Prefetch(const std::uint64_t key) const {
        __builtin_prefetch(&buckets_[Index(key)]);
    }

    // Return an entry only when its full key matches. Safe to call from any thread
    bool Probe(const std::uint64_t key, TTEntry &result) const {
        for (const TTSlot &slot: buckets_[Index(key)].slots) {
            if (Load(slot, key, result))
                return true;
        }

        return false;
    }

    // storedScore must already have mate scores normalized. Safe to call from any thread
    void Store(
        const std::uint64_t key,
        const int depth,
        const int storedScore,
        const TTBound bound,
        Move bestMove
    ) {
        auto &[slots] = buckets_[Index(key)];
        const auto clampedDepth = static_cast<std::int8_t>(std::clamp(depth, 0, 127));

        TTSlot *target = nullptr;
        TTEntry old;

        for (TTSlot &slot: slots) {
            if (Load(slot, key, old)) {
                target = &slot;
                break;
            }
        }

        if (target != nullptr) {
            // Same position: keep a much deeper non-exact entry, but never lose a useful move
            if (bound != TTBound::Exact && clampedDepth + 3 < old.depth) {
                if (old.bestMove == 0 && bestMove != 0) {
                    old.bestMove = bestMove;
                    Write(*target, key, old);
                }

                return;
            }

            if (bestMove == 0)
                bestMove = old.bestMove;
        } else {
            // Different position: evict the least valuable entry
            int worst = 1 << 30;

            for (TTSlot &slot: slots) {
                const TTEntry e = UnpackTTData(0, slot.data.load(std::memory_order_relaxed));
                const int value = e.depth < 0
                                      ? -(1 << 20)
                                      : e.depth - 8 * static_cast<int>(age_ - e.Age() & 63U);

                if (value < worst) {
                    worst = value;
                    target = &slot;
                }
            }
        }

        TTEntry entry;
        entry.key = key;
        entry.bestMove = bestMove;
        entry.score = static_cast<std::int16_t>(storedScore);
        entry.depth = clampedDepth;
        entry.ageBound = static_cast<std::uint8_t>((age_ << 2) | static_cast<unsigned>(bound));

        Write(*target, key, entry);
    }

private:
    static bool Load(const TTSlot &slot, const std::uint64_t key, TTEntry &out) {
        const std::uint64_t data = slot.data.load(std::memory_order_relaxed);

        if (const std::uint64_t keyXorData = slot.keyXorData.load(std::memory_order_relaxed); (keyXorData ^ data) != key)
            return false;

        out = UnpackTTData(key, data);
        return out.depth >= 0;
    }

    static void Write(TTSlot &slot, const std::uint64_t key, const TTEntry &entry) {
        const std::uint64_t data = PackTTData(entry);

        slot.data.store(data, std::memory_order_relaxed);
        slot.keyXorData.store(key ^ data, std::memory_order_relaxed);
    }

    // Multiply-shift maps a key onto [0, size) without a slow modulo and works for any table size
    [[nodiscard]] std::size_t Index(const std::uint64_t key) const {
        return static_cast<std::size_t>(
            (static_cast<unsigned __int128>(key) * size_) >> 64
        );
    }

    std::unique_ptr<TTBucket[]> buckets_;
    std::size_t size_ = 0;
    unsigned age_ = 0;
};

// Persistent table shared by FindBestMove() calls
inline TranspositionTable g_transpositionTable{64};

#endif // M3R_TRANSPOSITION_H