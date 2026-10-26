#pragma once

#ifndef M3R_MOVELIST_H
#define M3R_MOVELIST_H

#include "move.h"

struct MoveList { // NOLINT(*-pro-type-member-init)
    // Deliberately NOT zero-initialised: value-initialising 1 KB on every move-generation call was measurable.
    // Only moves[0..count) are valid
    Move moves[256];
    int count = 0;

    void AddMove(const Move move) {
        moves[count++] = move;
    }
};

#endif //M3R_MOVELIST_H