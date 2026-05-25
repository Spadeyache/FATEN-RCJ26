#include "CommandFilter.h"

void CommandFilter::clear() {
    for (int i = 0; i < FILTER_QUEUE_SIZE; i++) _queue[i] = 0;
    _head = 0;
    votesUturn = votesLeft = votesRight = 0;
    votesRed = votesSilver = votesBlack = votesNoLine = 0;
}

uint8_t CommandFilter::update(uint8_t rawCmd) {
    _queue[_head] = rawCmd;
    _head = (_head + 1) % FILTER_QUEUE_SIZE;

    votesUturn = votesLeft = votesRight = 0;
    votesRed = votesSilver = votesBlack = votesNoLine = 0;

    for (int i = 0; i < FILTER_QUEUE_SIZE; i++) {
        switch (_queue[i]) {
            // Code 1 = U-turn glyph (both colours visible at once).
            // It also counts as 1 vote toward each direction — so a pure
            // single-side approach never crosses the U-turn threshold.
            case 1: votesUturn++; votesLeft++; votesRight++; break;
            case 2: votesLeft++;   break;
            case 3: votesRight++;  break;
            case 4: votesRed++;    break;
            case 5: votesSilver++; break;  // line-follow silver
            case 6: votesBlack++;  break;  // line-follow black intersection
            // code 7 is a back-up variant of 6 (handled separately below)
            case 8: votesNoLine++; break;
        }
    }

    if (votesRed    >= FILTER_THRESHOLD_RED)    return 4;
    if (votesSilver >= FILTER_THRESHOLD_SILVER) return 5;

    // U-turn precedence: two unambiguous u-turn glyphs, OR an unusually
    // strong dual-colour count.
    const bool isUturn = (votesUturn >= 2)
                      || (votesLeft  >= 6 && votesRight >= 1)
                      || (votesRight >= 6 && votesLeft  >= 1)
                      || (votesLeft  >= 4 && votesRight >= 4);
    if (isUturn) return 1;

    // Green wins ties against black intersection.
    if (votesLeft  >= FILTER_THRESHOLD) return 2;
    if (votesRight >= FILTER_THRESHOLD) return 3;

    // No-green intersection: black only, no green at all.
    if (votesBlack >= FILTER_THRESHOLD_INTERSECTION && votesLeft <= 2 && votesRight <= 2) return 6;
    if (votesBlack >= 2 && votesLeft == 0 && votesRight == 0)                              return 7;
    if (votesNoLine >= FILTER_THRESHOLD_NOLINE)                                            return 8;

    return 0;
}
