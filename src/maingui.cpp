#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <future>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <cmath>

#define Color EngineColor
#include "board.h"
#include "attacks.h"
#include "search.h"
#undef Color

static constexpr int SIDE_WHITE = WHITE;
#undef WHITE
#undef BLACK
#include "raylib.h"
#undef WHITE
#undef BLACK

namespace {
    constexpr int SQ = 80;
    constexpr int BOARD_X = 76;
    constexpr int BOARD_Y = 40;
    constexpr int BOARD_PX = SQ * 8;
    constexpr int PANEL_X = BOARD_X + BOARD_PX + 40;
    constexpr int PANEL_W = 260;
    constexpr int WIN_W = PANEL_X + PANEL_W + 20;
    constexpr int WIN_H = BOARD_Y + BOARD_PX + 40;

    // Bar constants
    constexpr int EVAL_X = 14;
    constexpr int EVAL_W = 24;

    // Must match the mate constant used in your search
    constexpr int MATE_SCORE = 30000;
    constexpr int MATE_BOUND = MATE_SCORE - 256;

    constexpr Color C_EVAL_WHITE{.r = 240, .g = 240, .b = 240, .a = 255};
    constexpr Color C_EVAL_BLACK{.r = 64,  .g = 62,  .b = 60,  .a = 255};
    constexpr Color C_EVAL_MID{.r = 128, .g = 128, .b = 128, .a = 200};

    constexpr auto START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    constexpr int ENGINE_DEPTH = 30;
    constexpr int TIME_PRESETS_MS[] = {100, 500, 1000, 2000, 5000, 10000, 20000};
    constexpr int DEFAULT_TIME_INDEX = 4;

    constexpr Color C_BG{.r = 38, .g = 36, .b = 33, .a = 255};
    constexpr Color C_PANEL{.r = 49, .g = 46, .b = 43, .a = 255};
    constexpr Color C_LIGHT_SQ{.r = 240, .g = 217, .b = 181, .a = 255};
    constexpr Color C_DARK_SQ{.r = 181, .g = 136, .b = 99, .a = 255};
    constexpr Color C_LAST_MOVE{.r = 205, .g = 210, .b = 106, .a = 140};
    constexpr Color C_SELECTED{.r = 246, .g = 246, .b = 105, .a = 170};
    constexpr Color C_CHECK{.r = 235, .g = 64, .b = 52, .a = 150};
    constexpr Color C_HINT{.r = 0, .g = 0, .b = 0, .a = 55};
    constexpr Color C_TEXT{.r = 235, .g = 235, .b = 235, .a = 255};
    constexpr Color C_TEXT_DIM{.r = 160, .g = 160, .b = 160, .a = 255};
    constexpr Color C_BTN{.r = 74, .g = 70, .b = 66, .a = 255};
    constexpr Color C_BTN_HOT{.r = 98, .g = 93, .b = 88, .a = 255};
    constexpr Color C_BTN_OFF{.r = 58, .g = 56, .b = 53, .a = 255};
    constexpr Color C_PIECE_W{.r = 250, .g = 250, .b = 250, .a = 255};
    constexpr Color C_PIECE_B{.r = 26, .g = 26, .b = 26, .a = 255};
    constexpr Color C_OUTLINE{.r = 45, .g = 45, .b = 45, .a = 255};

    constexpr Rectangle BTN_NEW_WHITE{.x = PANEL_X, .y = 190, .width = PANEL_W, .height = 38};
    constexpr Rectangle BTN_NEW_BLACK{.x = PANEL_X, .y = 236, .width = PANEL_W, .height = 38};
    constexpr Rectangle BTN_UNDO{.x = PANEL_X, .y = 282, .width = PANEL_W, .height = 38};
    constexpr Rectangle BTN_FLIP{.x = PANEL_X, .y = 328, .width = PANEL_W, .height = 38};
    constexpr Rectangle BTN_TIME{.x = PANEL_X, .y = 374, .width = PANEL_W, .height = 38};

    Font g_uiFont{};
    Font g_pieceFont{};
    bool g_fontsLoaded = false;
    bool g_useGlyphs = false;
}

namespace {
    struct LegalMove {
        Move move{};
        std::string uci;
    };

    struct HistoryEntry {
        Move move{};
        UndoState undo{};
        bool human = false;
        std::string uci;
    };

    struct Game {
        Board board;
        std::vector<HistoryEntry> history;
        std::vector<LegalMove> legal;

        bool humanIsWhite = true;
        bool flipped = false;
        bool inCheck = false;
        bool gameOver = false;
        std::string resultText;

        int lastFrom = -1, lastTo = -1;
        int selected = -1;
        bool dragging = false;
        int promoFrom = -1, promoTo = -1;

        std::future<SearchResult> search;
        bool thinking = false;
        int timeIndex = DEFAULT_TIME_INDEX;
        std::string engineInfo;

        // Eval bar
        std::atomic<int> evalCp{0};     // White's perspective in centipawns
        std::atomic<int> evalMate{0};   // 0 = none, +n = White mates in n, -n = Black mates in n
        float shownWhite = 0.5f;          // animated bar fill (white's share)
    };
}

static std::string SquareName(const int sq) {
    return std::string{static_cast<char>('a' + sq % 8), static_cast<char>('1' + sq / 8)};
}

static int SquareFromUci(const std::string &uci, const int offset) {
    return (uci[offset + 1] - '1') * 8 + (uci[offset] - 'a');
}

static char PieceChar(const Board &board, const int square) {
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

static bool IsHumanTurn(const Game &g) {
    return (g.board.side == SIDE_WHITE) == g.humanIsWhite;
}

static std::vector<LegalMove> GenerateLegal(Board &board) {
    std::vector<LegalMove> out;
    const auto [moves, count] = GeneratePseudoLegalMoves(board);

    for (int i = 0; i < count; ++i) {
        if (UndoState undo{}; MakeMove(board, moves[i], undo)) {
            UnmakeMove(board, moves[i], undo);
            out.push_back({.move = moves[i], .uci = std::string(MoveToUci(moves[i]))});
        }
    }
    return out;
}

static void ClearSelection(Game &g) {
    g.selected = -1;
    g.dragging = false;
    g.promoFrom = g.promoTo = -1;
}

static void RefreshPosition(Game &g) {
    g.legal = GenerateLegal(g.board);
    g.inCheck = IsKingInCheck(g.board, g.board.side);
    g.gameOver = g.legal.empty();
    g.resultText.clear();

    if (g.gameOver) {
        if (g.inCheck)
            g.resultText = std::string("Checkmate! ") +
                           (g.board.side == SIDE_WHITE ? "Black" : "White") + " wins.";
        else
            g.resultText = "Stalemate. The game is a draw.";
    }
}

static void ResetEval(Game &g) {
    g.evalCp = 0;
    g.evalMate = 0;
}

static void NewGame(Game &g, const bool humanWhite) {
    ParseFEN(g.board, START_FEN);
    g_transpositionTable.Clear();

    g.history.clear();
    g.humanIsWhite = humanWhite;
    g.flipped = !humanWhite;
    g.lastFrom = g.lastTo = -1;
    g.engineInfo.clear();
    ClearSelection(g);
    RefreshPosition(g);
    ResetEval(g);
}

static void RecordLastMove(Game &g) {
    if (g.history.empty()) {
        g.lastFrom = g.lastTo = -1;
        return;
    }
    const std::string &uci = g.history.back().uci;
    g.lastFrom = SquareFromUci(uci, 0);
    g.lastTo = SquareFromUci(uci, 2);
}

static void PlayMove(Game &g, const LegalMove &lm, const bool human) {
    UndoState undo{};
    if (!MakeMove(g.board, lm.move, undo))
        return;

    g.history.push_back({.move = lm.move, .undo = undo, .human = human, .uci = lm.uci});
    RecordLastMove(g);
    ClearSelection(g);
    RefreshPosition(g);
}

static bool TryMove(Game &g, const int from, const int to) {
    const std::string prefix = SquareName(from) + SquareName(to);

    std::vector<const LegalMove *> matches;
    for (const LegalMove &lm: g.legal)
        if (lm.uci.compare(0, 4, prefix) == 0)
            matches.push_back(&lm);

    if (matches.empty())
        return false;

    if (matches.size() == 1 && matches[0]->uci.size() == 4) {
        const LegalMove chosen = *matches[0];
        PlayMove(g, chosen, true);
        return true;
    }

    g.promoFrom = from;
    g.promoTo = to;
    return true;
}

static bool CanUndo(const Game &g) {
    if (g.thinking) return false;
    return std::ranges::any_of(g.history,
                               [](const HistoryEntry &e) { return e.human; });
}

static void UndoMove(Game &g) {
    if (!CanUndo(g)) return;

    while (!g.history.empty()) {
        HistoryEntry entry = g.history.back();
        g.history.pop_back();
        UnmakeMove(g.board, entry.move, entry.undo);
        if (entry.human) break;
    }

    g_transpositionTable.Clear();
    g.engineInfo.clear();
    ClearSelection(g);
    RecordLastMove(g);
    RefreshPosition(g);
}

static void StoreEval(Game &g, const int score, const bool whiteToMove) {
    if (const int w = whiteToMove ? score : -score; std::abs(w) >= MATE_BOUND) {
        const int plies = MATE_SCORE - std::abs(w);
        const int moves = std::max(1, (plies + 1) / 2);
        g.evalMate = w > 0 ? moves : -moves;
        g.evalCp = w > 0 ? 10000 : -10000;
    } else {
        g.evalMate = 0;
        g.evalCp = w;
    }
}

static void StartEngine(Game &g) {
    SearchLimits limits;
    limits.timeMs = TIME_PRESETS_MS[g.timeIndex];
    limits.maxDepth = ENGINE_DEPTH;
    limits.printInfo = false;
    limits.threads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));

    // Let the search see the positions already played, so it recognises repetitions
    limits.gameHistory = BuildGameHistory(g.board, g.history);

    g.search = std::async(std::launch::async, [b = g.board, limits]() mutable {
        return FindBestMove(b, limits);
    });
    g.thinking = true;

    const bool whiteToMove = g.board.side == SIDE_WHITE;
    limits.onDepth = [&g, whiteToMove](int, const int score) {
        StoreEval(g, score, whiteToMove);
    };
}

static void PollEngine(Game &g) {
    if (!g.thinking) return;
    if (g.search.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

    const auto [bestMove, score, nodes, depth, hasMove] = g.search.get();
    g.thinking = false;

    if (!hasMove) {
        g.engineInfo = "Engine has no legal move.";
        return;
    }

    const std::string uci = MoveToUci(bestMove);
    const auto it = std::ranges::find_if(g.legal,
                                         [&](const LegalMove &lm) { return lm.uci == uci; });
    if (it == g.legal.end()) {
        g.engineInfo = "Internal error: engine returned an illegal move.";
        return;
    }

    const LegalMove& chosen = *it;

    StoreEval(g, score, g.board.side == SIDE_WHITE);
    PlayMove(g, chosen, false);

    PlayMove(g, chosen, false);

    char buf[96];
    std::snprintf(buf, sizeof buf, "Engine: %s (depth %d, %llu nodes)", uci.c_str(),
                  static_cast<int>(depth), static_cast<unsigned long long>(nodes));
    g.engineInfo = buf;
}

static bool LoadFonts(const char *userFont) {
    std::vector<std::string> candidates;
    if (userFont) candidates.emplace_back(userFont);

    for (const char *path: {
             "DejaVuSans.ttf", "assets/DejaVuSans.ttf", "fonts/DejaVuSans.ttf",
             "C:/Windows/Fonts/seguisym.ttf",
             "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
             "/usr/share/fonts/dejavu/DejaVuSans.ttf",
             "/usr/share/fonts/TTF/DejaVuSans.ttf",
             "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
             "/Library/Fonts/Arial Unicode.ttf"
         })
        candidates.emplace_back(path);

    std::vector<int> text;
    for (int c = 32; c < 127; ++c) text.push_back(c);
    int glyphs[6];
    for (int i = 0; i < 6; ++i) glyphs[i] = 0x265A + i;

    for (const std::string &path: candidates) {
        if (!FileExists(path.c_str())) continue;

        const Font ui = LoadFontEx(path.c_str(), 28, text.data(), static_cast<int>(text.size()));
        const Font pieces = LoadFontEx(path.c_str(), 128, glyphs, 6);
        if (ui.texture.id == 0 || pieces.texture.id == 0) continue;

        SetTextureFilter(ui.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureFilter(pieces.texture, TEXTURE_FILTER_BILINEAR);
        g_uiFont = ui;
        g_pieceFont = pieces;
        g_fontsLoaded = true;
        return true;
    }

    g_uiFont = GetFontDefault();
    return false;
}

static void DrawLabel(const std::string &s, const float x, const float y, const float size, const Color c) {
    DrawTextEx(g_uiFont, s.c_str(), Vector2{.x = x, .y = y}, size, 1.0f, c);
}

static float LabelWidth(const std::string &s, const float size) {
    return MeasureTextEx(g_uiFont, s.c_str(), size, 1.0f).x;
}

// Lichess-Style cp -> win-chance curve, as White's share of the bar.
static float WhiteFraction(const Game &g) {
    if (g.gameOver) {
        if (!g.inCheck) return 0.5f;
        return g.board.side == SIDE_WHITE ? 0.0f : 1.0f;   // side to move is mated
    }
    if (const int m = g.evalMate; m != 0) return m > 0 ? 1.0f : 0.0f;
    return 1.0f / (1.0f + std::exp(-0.00368208f * static_cast<float>(g.evalCp.load())));
}

static void UpdateEvalBar(Game &g) {
    const float target = WhiteFraction(g);
    g.shownWhite += (target - g.shownWhite) * std::min(1.0f, GetFrameTime() * 8.0f);
}

static std::string EvalLabel(const Game &g) {
    if (g.gameOver) {
        if (!g.inCheck) return "1/2";
        return g.board.side == SIDE_WHITE ? "0-1" : "1-0";
    }
    char buf[24];
    if (const int m = g.evalMate; m != 0)
        std::snprintf(buf, sizeof buf, "%sM%d", m < 0 ? "-" : "", std::abs(m));
    else
        std::snprintf(buf, sizeof buf, "%+.1f", g.evalCp / 100.0);
    return buf;
}

static void DrawEvalBar(const Game &g) {
    constexpr float x = EVAL_X, y = BOARD_Y, w = EVAL_W, h = BOARD_PX;
    const float whiteH = h * g.shownWhite;
    const float whiteY = g.flipped ? y : y + h - whiteH;   // White's end follows the board

    DrawRectangleRec({.x = x, .y = y, .width = w, .height = h}, C_EVAL_BLACK);
    DrawRectangleRec({.x = x, .y = whiteY, .width = w, .height = whiteH}, C_EVAL_WHITE);
    DrawRectangleRec({.x = x, .y = y + h / 2.0f - 1.0f, .width = w, .height = 2.0f}, C_EVAL_MID);

    const std::string label = EvalLabel(g);
    const float lw = LabelWidth(label, 14.0f);
    DrawLabel(label, std::max(2.0f, x + w / 2.0f - lw / 2.0f), y - 24.0f, 14.0f, C_TEXT);
}

static Vector2 SquareOrigin(const Game &g, const int sq) {
    const int file = sq % 8, rank = sq / 8;
    const int col = g.flipped ? 7 - file : file;
    const int row = g.flipped ? rank : 7 - rank;
    return Vector2{.x = static_cast<float>(BOARD_X + col * SQ), .y = static_cast<float>(BOARD_Y + row * SQ)};
}

static int SquareAt(const Game &g, const Vector2 m) {
    const int x = static_cast<int>(m.x) - BOARD_X;
    const int y = static_cast<int>(m.y) - BOARD_Y;
    if (m.x < BOARD_X || m.y < BOARD_Y || x >= BOARD_PX || y >= BOARD_PX) return -1;

    const int col = x / SQ, row = y / SQ;
    const int file = g.flipped ? 7 - col : col;
    const int rank = g.flipped ? row : 7 - row;
    return rank * 8 + file;
}

static void DrawPiece(const char piece, const Vector2 center, const float alpha = 1.0f) {
    const bool white = std::isupper(static_cast<unsigned char>(piece)) != 0;
    const char type = static_cast<char>(std::tolower(static_cast<unsigned char>(piece)));

    auto fade = [alpha](Color c) {
        c.a = static_cast<unsigned char>(static_cast<float>(c.a) * alpha);
        return c;
    };

    if (g_useGlyphs) {
        int cp = 0x265F;
        switch (type) {
            case 'k': cp = 0x265A;
                break;
            case 'q': cp = 0x265B;
                break;
            case 'r': cp = 0x265C;
                break;
            case 'b': cp = 0x265D;
                break;
            case 'n': cp = 0x265E;
                break;
            default: break;
        }

        int len = 0;
        const char *utf8 = CodepointToUTF8(cp, &len);
        const std::string glyph(utf8, static_cast<size_t>(len));
        constexpr float size = SQ * 0.82f;
        const auto [x, y] = MeasureTextEx(g_pieceFont, glyph.c_str(), size, 0.0f);
        const Vector2 pos{.x = center.x - x / 2.0f, .y = center.y - y / 2.0f};

        if (white) {
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    if (dx || dy)
                        DrawTextEx(g_pieceFont, glyph.c_str(),
                                   Vector2{.x = pos.x + static_cast<float>(dx) * 2.0f, .y = pos.y + static_cast<float>(dy) * 2.0f}, size, 0.0f, fade(C_OUTLINE));
        }
        DrawTextEx(g_pieceFont, glyph.c_str(), pos, size, 0.0f, fade(white ? C_PIECE_W : C_PIECE_B));
        return;
    }

    constexpr float radius = SQ * 0.38f;
    DrawCircleV(center, radius, fade(white ? C_PIECE_W : C_PIECE_B));
    DrawRing(center, radius - 2.0f, radius, 0.0f, 360.0f, 40, fade(white ? C_OUTLINE : C_PIECE_W));

    const std::string letter(1, static_cast<char>(std::toupper(static_cast<unsigned char>(piece))));
    const auto [x, y] = MeasureTextEx(g_uiFont, letter.c_str(), 36.0f, 1.0f);
    DrawTextEx(g_uiFont, letter.c_str(), Vector2{.x = center.x - x / 2.0f, .y = center.y - y / 2.0f},
               36.0f, 1.0f, fade(white ? C_PIECE_B : C_PIECE_W));
}

static bool Hovered(const Rectangle &r) {
    return CheckCollisionPointRec(GetMousePosition(), r);
}

static bool ButtonClicked(const Rectangle &r, const bool enabled) {
    return enabled && Hovered(r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void DrawButton(const Rectangle &r, const std::string &label, const bool enabled) {
    const Color fill = !enabled ? C_BTN_OFF : Hovered(r) ? C_BTN_HOT : C_BTN;
    DrawRectangleRounded(r, 0.2f, 8, fill);
    const float w = LabelWidth(label, 20.0f);
    DrawLabel(label, r.x + (r.width - w) / 2.0f, r.y + 9.0f, 20.0f, enabled ? C_TEXT : C_TEXT_DIM);
}

static std::string TimeLabel(const Game &g) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "Engine time: %g s", TIME_PRESETS_MS[g.timeIndex] / 1000.0);
    return buf;
}

static std::string StatusText(const Game &g) {
    if (g.gameOver) return g.resultText;

    if (g.thinking) {
        const int dots = static_cast<int>(GetTime() * 3.0) % 4;
        return "Engine is thinking" + std::string(static_cast<size_t>(dots), '.');
    }

    const std::string side = g.board.side == SIDE_WHITE ? "White" : "Black";
    return std::string(g.inCheck ? "Check! " : "") + "Your move (" + side + ")";
}

static void HandlePromotionInput(Game &g) {
    constexpr auto panelW = SQ * 4.0f;
    constexpr auto px = BOARD_X + (BOARD_PX - panelW) / 2.0f;
    constexpr auto py = BOARD_Y + (BOARD_PX - SQ) / 2.0f;

    if (IsKeyPressed(KEY_ESCAPE)) {
        ClearSelection(g);
        return;
    }
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;

    const Vector2 m = GetMousePosition();
    if (constexpr Rectangle panel{.x = px, .y = py, .width = panelW, .height = static_cast<float>(SQ)}; !CheckCollisionPointRec(m, panel)) {
        ClearSelection(g);
        return;
    }

    constexpr char CHOICES[] = {'q', 'r', 'b', 'n'};
    const int index = std::clamp(static_cast<int>((m.x - px) / SQ), 0, 3);
    const std::string uci = SquareName(g.promoFrom) + SquareName(g.promoTo) + CHOICES[index];

    const auto it = std::ranges::find_if(g.legal,
                                         [&](const LegalMove &lm) { return lm.uci == uci; });
    if (it != g.legal.end()) {
        const LegalMove &chosen = *it;
        PlayMove(g, chosen, true);
    } else {
        ClearSelection(g);
    }
}

static void HandleBoardInput(Game &g, const std::vector<char> &grid) {
    const Vector2 m = GetMousePosition();
    const int sq = SquareAt(g, m);

    auto isOwn = [&](const int s) {
        const char c = grid[static_cast<size_t>(s)];
        return c != '.' && (std::isupper(static_cast<unsigned char>(c)) != 0) == g.humanIsWhite;
    };

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && sq >= 0) {
        if (g.selected < 0 || sq == g.selected || !TryMove(g, g.selected, sq)) {
            if (isOwn(sq)) {
                g.selected = sq;
                g.dragging = true;
            } else {
                g.selected = -1;
            }
        } else {
            g.selected = -1;
        }
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && g.dragging) {
        g.dragging = false;
        if (sq >= 0 && sq != g.selected) {
            TryMove(g, g.selected, sq);
            g.selected = -1;
        } else if (sq < 0) {
            g.selected = -1;
        }
    }
}

static void DrawBoard(Game &g, const std::vector<char> &grid) {
    for (int sq = 0; sq < 64; ++sq) {
        const auto [x, y] = SquareOrigin(g, sq);
        const bool light = (sq / 8 + sq % 8) % 2 == 1;
        DrawRectangle(static_cast<int>(x), static_cast<int>(y), SQ, SQ, light ? C_LIGHT_SQ : C_DARK_SQ);
    }

    auto tint = [&](const int sq, const Color c) {
        if (sq < 0) return;
        const auto [x, y] = SquareOrigin(g, sq);
        DrawRectangle(static_cast<int>(x), static_cast<int>(y), SQ, SQ, c);
    };

    tint(g.lastFrom, C_LAST_MOVE);
    tint(g.lastTo, C_LAST_MOVE);

    if (g.inCheck) {
        const char king = g.board.side == SIDE_WHITE ? 'K' : 'k';
        for (int sq = 0; sq < 64; ++sq)
            if (grid[static_cast<size_t>(sq)] == king) tint(sq, C_CHECK);
    }
    tint(g.selected, C_SELECTED);

    for (int i = 0; i < 8; ++i) {
        const int file = g.flipped ? 7 - i : i;
        const int rank = g.flipped ? i : 7 - i;
        const std::string f(1, static_cast<char>('a' + file));
        const std::string r(1, static_cast<char>('1' + rank));
        DrawLabel(f, static_cast<float>(BOARD_X) + static_cast<float>(i) * SQ + SQ / 2.0f - 5.0f,
                  BOARD_Y + BOARD_PX + 8.0f, 20.0f, C_TEXT_DIM);
        DrawLabel(r, BOARD_X - 22.0f, static_cast<float>(BOARD_Y) + static_cast<float>(i) * SQ + SQ / 2.0f - 10.0f,
                  20.0f, C_TEXT_DIM);
    }

    if (g.selected >= 0 && !g.thinking) {
        for (const auto &[move, uci]: g.legal) {
            if (SquareFromUci(uci, 0) != g.selected) continue;
            const int to = SquareFromUci(uci, 2);
            const auto [x, y] = SquareOrigin(g, to);
            const Vector2 c{.x = x + SQ / 2.0f, .y = y + SQ / 2.0f};
            if (grid[static_cast<size_t>(to)] != '.')
                DrawRing(c, SQ * 0.40f, SQ * 0.48f, 0.0f, 360.0f, 48, C_HINT);
            else
                DrawCircleV(c, SQ * 0.16f, C_HINT);
        }
    }

    for (int sq = 0; sq < 64; ++sq) {
        const char piece = grid[static_cast<size_t>(sq)];
        if (piece == '.') continue;

        const auto [x, y] = SquareOrigin(g, sq);
        const Vector2 c{.x = x + SQ / 2.0f, .y = y + SQ / 2.0f};
        const bool beingDragged = g.dragging && sq == g.selected;
        DrawPiece(piece, c, beingDragged ? 0.35f : 1.0f);
    }

    if (g.dragging && g.selected >= 0)
        DrawPiece(grid[static_cast<size_t>(g.selected)], GetMousePosition());
}

static void DrawPromotionChooser(const Game &g) {
    DrawRectangle(BOARD_X, BOARD_Y, BOARD_PX, BOARD_PX, Color{.r = 0, .g = 0, .b = 0, .a = 140});

    constexpr auto panelW = SQ * 4.0f;
    constexpr auto px = BOARD_X + (BOARD_PX - panelW) / 2.0f;
    constexpr auto py = BOARD_Y + (BOARD_PX - SQ) / 2.0f;
    DrawRectangleRounded(Rectangle{.x = px - 6, .y = py - 6, .width = panelW + 12, .height = SQ + 12.0f}, 0.12f, 8,
                         C_PANEL);

    constexpr auto whiteSet = "QRBN";
    constexpr auto blackSet = "qrbn";
    const char *set = g.board.side == SIDE_WHITE ? whiteSet : blackSet;

    for (int i = 0; i < 4; ++i) {
        const Rectangle cell{
            .x = px + static_cast<float>(i) * SQ, .y = py, .width = static_cast<float>(SQ),
            .height = static_cast<float>(SQ)
        };
        DrawRectangleRec(cell, Hovered(cell) ? C_SELECTED : C_LIGHT_SQ);
        DrawPiece(set[i], Vector2{.x = cell.x + SQ / 2.0f, .y = cell.y + SQ / 2.0f});
    }
}

static void DrawPanel(const Game &g) {
    DrawRectangle(PANEL_X - 20, 0, PANEL_W + 40, WIN_H, C_PANEL);

    DrawLabel("Chess", PANEL_X, 36, 36.0f, C_TEXT);
    DrawLabel(StatusText(g), PANEL_X, 100, 22.0f, C_TEXT);
    DrawLabel(g.engineInfo, PANEL_X, 134, 15.0f, C_TEXT_DIM);

    const bool idle = !g.thinking;
    DrawButton(BTN_NEW_WHITE, "New game (play White)", idle);
    DrawButton(BTN_NEW_BLACK, "New game (play Black)", idle);
    DrawButton(BTN_UNDO, "Undo", CanUndo(g));
    DrawButton(BTN_FLIP, "Flip board", true);
    DrawButton(BTN_TIME, TimeLabel(g), true);

    DrawLabel("Moves", PANEL_X, 430, 18.0f, C_TEXT_DIM);

    std::vector<std::string> lines;
    for (size_t i = 0; i < g.history.size(); i += 2) {
        std::string line = std::to_string(i / 2 + 1) + ". " + g.history[i].uci;
        if (i + 1 < g.history.size()) line += "   " + g.history[i + 1].uci;
        lines.push_back(line);
    }

    constexpr size_t MAX_LINES = 9;
    const size_t first = lines.size() > MAX_LINES ? lines.size() - MAX_LINES : 0;
    for (size_t i = first; i < lines.size(); ++i)
        DrawLabel(lines[i], PANEL_X, 456.0f + static_cast<float>(i - first) * 24.0f, 18.0f, C_TEXT);

    DrawLabel("N new   U undo   F flip   L glyphs", PANEL_X, WIN_H - 34.0f, 15.0f, C_TEXT_DIM);
}

int main(const int argc, const char **argv) {
    const char *fontPath = nullptr;
    bool forceLetters = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--letters") == 0) forceLetters = true;
        else if (std::strcmp(argv[i], "--font") == 0 && i + 1 < argc) fontPath = argv[++i];
    }

    InitAllAttacks();

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(WIN_W, WIN_H, "Chess Engine");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    g_useGlyphs = LoadFonts(fontPath) && !forceLetters;

    Game game;
    NewGame(game, true);

    std::vector grid(64, '.');

    while (!WindowShouldClose()) {
        PollEngine(game);
        UpdateEvalBar(game);

        for (int sq = 0; sq < 64; ++sq)
            grid[static_cast<size_t>(sq)] = PieceChar(game.board, sq);

        const bool humanCanMove = !game.thinking && !game.gameOver && IsHumanTurn(game);

        if (game.promoFrom >= 0) HandlePromotionInput(game);
        else if (humanCanMove) HandleBoardInput(game, grid);

        if (const bool idle = !game.thinking; ButtonClicked(BTN_NEW_WHITE, idle) ||
                                              (idle && IsKeyPressed(KEY_N) && game.humanIsWhite))
            NewGame(game, true);
        else if (ButtonClicked(BTN_NEW_BLACK, idle) || (idle && IsKeyPressed(KEY_N) && !game.humanIsWhite))
            NewGame(game, false);
        if (ButtonClicked(BTN_UNDO, CanUndo(game)) || (IsKeyPressed(KEY_U) && CanUndo(game)))
            UndoMove(game);
        if (ButtonClicked(BTN_FLIP, true) || IsKeyPressed(KEY_F))
            game.flipped = !game.flipped;
        if (ButtonClicked(BTN_TIME, true))
            game.timeIndex = (game.timeIndex + 1) % static_cast<int>(std::size(TIME_PRESETS_MS));
        if (IsKeyPressed(KEY_L) && g_fontsLoaded && !forceLetters)
            g_useGlyphs = !g_useGlyphs;

        for (int sq = 0; sq < 64; ++sq)
            grid[static_cast<size_t>(sq)] = PieceChar(game.board, sq);

        if (!game.thinking && !game.gameOver && !IsHumanTurn(game) && game.promoFrom < 0)
            StartEngine(game);

        BeginDrawing();
        ClearBackground(C_BG);
        DrawBoard(game, grid);
        DrawEvalBar(game);
        if (game.promoFrom >= 0) DrawPromotionChooser(game);
        DrawPanel(game);
        EndDrawing();
    }

    if (game.thinking) game.search.wait();

    if (g_fontsLoaded) {
        UnloadFont(g_uiFont);
        UnloadFont(g_pieceFont);
    }
    CloseWindow();
    return 0;
}
