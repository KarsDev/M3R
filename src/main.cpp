#include <chrono>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "board.h"
#include "attacks.h"
#include "search.h"

// Unlike assert(), CHECK still runs when compiled with -DNDEBUG (-O3 release builds).
#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "CHECK FAILED: " #cond " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

static void TestMagicAttacks() {
    std::uint64_t seed = 0x1234567ULL;

    for (int i = 0; i < 200000; ++i) {
        u64 occupied = NextRandom(seed) & NextRandom(seed);
        if (i % 3 == 0) occupied &= NextRandom(seed);

        const int sq = static_cast<int>(NextRandom(seed) & 63);

        CHECK(RookAttacks(sq, occupied) == RookAttacksSlow(sq, occupied));
        CHECK(BishopAttacks(sq, occupied) == BishopAttacksSlow(sq, occupied));
    }

    std::cout << "Magic bitboard tests passed.\n";
}

static void VerifyIncrementalHashForFen(const std::string &fen) {
    Board board;
    ParseFEN(board, fen);

    EnsurePositionKey(board);
    const std::uint64_t originalKey = board.zobristKey;
    const Board original = board;

    const auto [moves, count] = GeneratePseudoLegalMoves(board);

    for (int i = 0; i < count; ++i) {
        const Move move = moves[i];

        if (UndoState undo{}; MakeMove(board, move, undo)) {
            CHECK(board.zobristKeyValid);
            CHECK(board.zobristKey == ComputePositionKey(board));

            Board rebuilt = board;
            UpdateOccupancies(rebuilt);
            CHECK(rebuilt.occupancies[WHITE] == board.occupancies[WHITE]);
            CHECK(rebuilt.occupancies[BLACK] == board.occupancies[BLACK]);
            CHECK(rebuilt.occupancies[BOTH] == board.occupancies[BOTH]);

            for (int sq = 0; sq < 64; ++sq)
                CHECK(rebuilt.mailbox[sq] == board.mailbox[sq]);

            UnmakeMove(board, move, undo);
        }

        CHECK(board.zobristKeyValid);
        CHECK(board.zobristKey == originalKey);
        CHECK(board.zobristKey == ComputePositionKey(board));

        for (int piece = 0; piece < PIECE_NB; ++piece)
            CHECK(board.bitboards[piece] == original.bitboards[piece]);

        for (int sq = 0; sq < 64; ++sq)
            CHECK(board.mailbox[sq] == original.mailbox[sq]);

        CHECK(board.occupancies[BOTH] == original.occupancies[BOTH]);
        CHECK(board.halfmoveClock == original.halfmoveClock);
    }
}

static void TestPerft() {
    struct Case {
        const char *name;
        const char *fen;
        int depth;
        std::uint64_t expected;
    };

    const Case cases[] = {
        {
            .name = "startpos", .fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", .depth = 5,
            .expected = 4865609
        },
        {
            .name = "kiwipete", .fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            .depth = 4, .expected = 4085603
        },
        {.name = "position 3", .fen = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", .depth = 5, .expected = 674624},
        {
            .name = "position 4", .fen = "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", .depth = 4,
            .expected = 422333
        },
        {
            .name = "position 5", .fen = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", .depth = 4,
            .expected = 2103487
        },
        {
            .name = "position 6", .fen = "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
            .depth = 4, .expected = 3894594
        },
    };

    for (const auto &[name, fen, depth, expected]: cases) {
        Board board;
        ParseFEN(board, fen);

        if (const std::uint64_t nodes = Perft(board, depth); nodes != expected) {
            std::cerr << "Perft mismatch in " << name << ": got " << nodes
                    << ", expected " << expected << '\n';
            std::exit(1);
        }
    }

    std::cout << "Perft tests passed (6 standard positions).\n";
}

static void TestSee() {
    Board board;

    ParseFEN(board, "4k3/8/3p4/4p3/3P4/8/8/4K3 w - - 0 1");
    {
        const Move dxe5 = EncodeMove(d4, e5, P, p, 0, MOVE_CAPTURE);
        CHECK(SeeGE(board, dxe5, 0));
        CHECK(!SeeGE(board, dxe5, 1));
    }

    ParseFEN(board, "4k3/8/3p4/4p3/8/8/4Q3/4K3 w - - 0 1");
    {
        const Move qxe5 = EncodeMove(e2, e5, Q, p, 0, MOVE_CAPTURE);
        CHECK(!SeeGE(board, qxe5, 0));
    }

    ParseFEN(board, "4k3/8/8/4n3/8/8/4R3/4K3 w - - 0 1");
    {
        const Move rxe5 = EncodeMove(e2, e5, R, n, 0, MOVE_CAPTURE);
        CHECK(SeeGE(board, rxe5, 320));
        CHECK(!SeeGE(board, rxe5, 321));
    }

    // King takes a defended pawn: must lose
    ParseFEN(board, "4k3/8/4p3/3p4/2K5/8/8/8 w - - 0 1");
    CHECK(!SeeGE(board, EncodeMove(c4, d5, K, p, 0, MOVE_CAPTURE), 0));

    // King takes an undefended pawn: must win
    ParseFEN(board, "4k3/8/8/3p4/2K5/8/8/8 w - - 0 1");
    CHECK(SeeGE(board, EncodeMove(c4, d5, K, p, 0, MOVE_CAPTURE), 0));

    std::cout << "SEE tests passed.\n";
}

static void ExpectMateInOne(const char *fen, const char *expectedUci) {
    Board board;
    ParseFEN(board, fen);
    g_transpositionTable.Clear();

    const SearchResult result = FindBestMove(board, 4);

    CHECK(result.hasMove);
    CHECK(result.score >= SCORE_MATE - 10);
    CHECK(MoveToUci(result.bestMove) == expectedUci);

    UndoState undo{};
    const bool made = MakeMove(board, result.bestMove, undo);
    CHECK(made);
    CHECK(IsKingInCheck(board, board.side));
    CHECK(!HasAnyLegalMove(board));
    UnmakeMove(board, result.bestMove, undo);

    g_transpositionTable.Clear();
}

static void TestSearch() {
    ExpectMateInOne("7k/8/5KQ1/8/8/8/8/8 w - - 0 1", "g6g7");
    ExpectMateInOne("6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1", "a1a8"); // back-rank mate
    ExpectMateInOne("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4", "h5f7"); // scholar's mate
    ExpectMateInOne("rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq - 0 2", "d8h4"); // fool's mate

    {
        Board board;
        ParseFEN(board, "4k3/8/8/3q4/8/8/3R4/4K3 w - - 0 1");
        g_transpositionTable.Clear();
        const SearchResult result = FindBestMove(board, 5);
        CHECK(MoveToUci(result.bestMove) == "d2d5");
    }

    {
        Board board;
        ParseFEN(board, "4k3/8/8/8/8/8/4Q3/4K3 w - - 99 100");
        g_transpositionTable.Clear();
        const SearchResult result = FindBestMove(board, 4);
        CHECK(result.score == 0);
    }

    {
        Board board;
        ParseFEN(board, "4k3/8/8/8/8/8/4B3/4K3 w - - 0 1");
        CHECK(Evaluate(board) == 0);
    }

    {
        Board board;
        ParseFEN(board, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        g_transpositionTable.Clear();

        SearchLimits limits;
        limits.maxDepth = 30;
        limits.maxNodes = 50000;

        const SearchResult result = FindBestMove(board, limits);
        CHECK(result.hasMove);
        CHECK(result.depth >= 1 && result.depth < 30);
    }

    std::cout << "Search tests passed.\n";
}

static void TestNullMove() {
    Board board;
    ParseFEN(board, "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    EnsurePositionKey(board);

    const std::uint64_t originalKey = board.zobristKey;
    const int originalSide = board.side;
    const int originalEp = board.enPassantSquare;

    NullUndoState nullUndo{};
    MakeNullMove(board, nullUndo);

    CHECK(board.side != originalSide);
    CHECK(board.enPassantSquare == NO_SQ);
    CHECK(board.zobristKey == ComputePositionKey(board));

    UnmakeNullMove(board, nullUndo);

    CHECK(board.side == originalSide);
    CHECK(board.enPassantSquare == originalEp);
    CHECK(board.zobristKeyValid);
    CHECK(board.zobristKey == originalKey);
    CHECK(board.zobristKey == ComputePositionKey(board));

    std::cout << "Null-move hash tests passed.\n";
}

static void Benchmark(const int threads) {
    const char *fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "r1bq1rk1/pp2ppbp/2np1np1/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQ - 0 9",
    };

    std::uint64_t totalNodes = 0;
    const auto start = std::chrono::steady_clock::now();

    for (const char *fen: fens) {
        Board board;
        ParseFEN(board, fen);
        g_transpositionTable.Clear();

        SearchLimits limits;
        limits.maxDepth = 11;
        limits.threads = threads;

        const SearchResult result = FindBestMove(board, limits);
        totalNodes += result.nodes;

        std::cout << "  " << MoveToUci(result.bestMove)
                << "  score " << result.score
                << "  nodes " << result.nodes << '\n';
    }

    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();

    std::cout << "Bench: " << totalNodes << " nodes, " << seconds << " s, "
            << static_cast<std::uint64_t>(static_cast<double>(totalNodes) / seconds) << " NPS\n";
}

static std::string NormalizeInput(const std::string &input) {
    std::string normalized;
    normalized.reserve(input.size());

    for (const unsigned char ch: input) {
        if (!std::isspace(ch))
            normalized.push_back(static_cast<char>(std::tolower(ch)));
    }

    return normalized;
}

static char PieceAt(const Board &board, const int square) {
    const std::uint64_t mask = std::uint64_t{1} << square;

    if (board.bitboards[P] & mask) return 'P';
    if (board.bitboards[N] & mask) return 'N';
    if (board.bitboards[B] & mask) return 'B';
    if (board.bitboards[R] & mask) return 'R';
    if (board.bitboards[Q] & mask) return 'Q';
    if (board.bitboards[K] & mask) return 'K';
    if (board.bitboards[p] & mask) return 'p';
    if (board.bitboards[n] & mask) return 'n';
    if (board.bitboards[b] & mask) return 'b';
    if (board.bitboards[r] & mask) return 'r';
    if (board.bitboards[q] & mask) return 'q';
    if (board.bitboards[k] & mask) return 'k';

    return '.';
}

static void PrintBoard(const Board &board) {
    std::cout << "\n    a b c d e f g h\n";
    std::cout << "  +-----------------+\n";

    for (int rank = 7; rank >= 0; --rank) {
        std::cout << rank + 1 << " | ";
        for (int file = 0; file < 8; ++file)
            std::cout << PieceAt(board, rank * 8 + file) << ' ';
        std::cout << "| " << rank + 1 << '\n';
    }

    std::cout << "  +-----------------+\n";
    std::cout << "    a b c d e f g h\n";
    std::cout << "Side to move: "
            << (board.side == WHITE ? "White" : "Black") << '\n';
    if (IsKingInCheck(board, board.side))
        std::cout << "Check!\n";
}

static bool ApplyHumanMove(Board &board, const std::string &uci,
                           Move &chosenMove,
                           UndoState &chosenUndo,
                           bool &matchedGeneratedMove) {
    matchedGeneratedMove = false;

    const auto [moves, count] = GeneratePseudoLegalMoves(board);

    for (int i = 0; i < count; ++i) {
        if (NormalizeInput(MoveToUci(moves[i])) != uci)
            continue;

        matchedGeneratedMove = true;

        UndoState undo{};
        if (!MakeMove(board, moves[i], undo))
            return false;

        chosenMove = moves[i];
        chosenUndo = undo;
        return true;
    }

    return false;
}

static bool GameIsOver(Board &board) {
    if (!HasAnyLegalMove(board)) {
        if (IsKingInCheck(board, board.side)) {
            std::cout << "Checkmate! "
                    << (board.side == WHITE ? "Black" : "White")
                    << " wins.\n";
        } else {
            std::cout << "Stalemate. The game is a draw.\n";
        }
        return true;
    }

    return false;
}

namespace {
    struct HistoryEntry {
        Move move{};
        UndoState undo{};
        bool humanMove = false;
    };
}

static int PlayGame() {
    constexpr auto START_FEN =
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    constexpr int ENGINE_DEPTH = 30;
    constexpr int TIME_LIMIT = 5000; // ms

    Board board;
    ParseFEN(board, START_FEN);
    g_transpositionTable.Clear();

    std::vector<HistoryEntry> history;

    bool humanIsWhite = true;

    for (;;) {
        std::string sideInput;
        std::cout << "Play as White or Black? [w/b, default w]: ";
        if (!std::getline(std::cin, sideInput))
            return 0;

        sideInput = NormalizeInput(sideInput);
        if (sideInput.empty() || sideInput == "w") {
            humanIsWhite = true;
            break;
        }
        if (sideInput == "b") {
            humanIsWhite = false;
            break;
        }
        std::cout << "Please enter 'w' or 'b'.\n";
    }

    std::cout << "\nEnter moves in UCI format: e2e4, g1f3, or e7e8q for promotion.\n"
            << "Type 'quit' or 'resign' to end the game.\n";

    SearchLimits limits;
    limits.timeMs = TIME_LIMIT;
    limits.maxDepth = ENGINE_DEPTH;
    limits.printInfo = false;
    limits.threads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));

    while (true) {
        PrintBoard(board);
        if (GameIsOver(board))
            break;

        if ((board.side == WHITE) == humanIsWhite) {
            std::cout << "Your move: ";
            std::string input;
            if (!std::getline(std::cin, input)) {
                std::cout << "Input closed; exiting game.\n";
                break;
            }

            input = NormalizeInput(input);
            if (input == "quit" || input == "resign") {
                std::cout << (humanIsWhite ? "White" : "Black")
                        << " resigns. "
                        << (humanIsWhite ? "Black" : "White")
                        << " wins.\n";
                break;
            }
            if (input == "help") {
                std::cout << "Use UCI moves such as e2e4, e7e8q (promotion), "
                        "or type 'quit'.\n";
                continue;
            }

            if (input == "undo") {
                if (history.empty()) {
                    std::cout << "No moves to undo.\n";
                    continue;
                }

                auto [move, undo, humanMove] = history.back();
                history.pop_back();

                UnmakeMove(board, move, undo);

                if (!humanMove && !history.empty() &&
                    history.back().humanMove) {
                    HistoryEntry previous = history.back();
                    history.pop_back();

                    UnmakeMove(board, previous.move, previous.undo);
                    }

                g_transpositionTable.Clear();

                std::cout << "Move undone.\n";
                PrintBoard(board);
                continue;
            }

            Move move{};
            UndoState undo{};

            if (bool matchedGeneratedMove = false; !ApplyHumanMove(
                    board, input, move, undo, matchedGeneratedMove)) {
                if (matchedGeneratedMove) {
                    std::cout << "Illegal move: it leaves your king in check.\n";
                } else {
                    std::cout << "Invalid or illegal move. Enter a legal UCI move "
                                 "such as e2e4 or e7e8q.\n";
                }
                continue;
                    }

            history.push_back({.move = move, .undo = undo, .humanMove = true});

            std::cout << "You played " << MoveToUci(move) << ".\n";
        } else {
            std::cout << "Engine thinking (" << TIME_LIMIT / 1000 << " s)...\n";

            // Let the search see the positions already played, so it recognises repetitions
            limits.gameHistory = BuildGameHistory(board, history);

            const SearchResult result = FindBestMove(board, limits);
            if (!result.hasMove) {
                std::cout << "The engine has no legal move.\n";
                break;
            }

            UndoState undo{};
            if (!MakeMove(board, result.bestMove, undo)) {
                std::cerr << "Internal error: engine returned an illegal move.\n";
                return 1;
            }

            // Record the engine's move too: the history (and the 'undo' command) need every move played
            history.push_back({.move = result.bestMove, .undo = undo, .humanMove = false});

            std::cout << "Engine plays " << MoveToUci(result.bestMove)
                    << " (searched depth " << result.depth
                    << ", " << result.nodes << " nodes).\n";
        }
    }

    PrintBoard(board);

    getchar();
    return 0;
}

static void RunTests() {
    TestMagicAttacks();

    VerifyIncrementalHashForFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    VerifyIncrementalHashForFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    VerifyIncrementalHashForFen("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1");
    VerifyIncrementalHashForFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    VerifyIncrementalHashForFen("1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    VerifyIncrementalHashForFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    std::cout << "Incremental hash / mailbox / occupancy tests passed.\n";

    TestPerft();
    TestNullMove();
    TestSee();
    TestSearch();
}

int main(const int argc, const char **argv) {
    InitAllAttacks();

    if (argc > 1 && std::string(argv[1]) == "--test") {
        RunTests();
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--bench") {
        const int threads = argc > 2 ? std::max(1, std::atoi(argv[2])) : 1;
        std::cout << "Benchmark (depth 11, 5 positions, " << threads << " thread(s)):\n";
        Benchmark(threads);
        return 0;
    }

    return PlayGame();
}