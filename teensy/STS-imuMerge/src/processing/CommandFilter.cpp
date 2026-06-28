#include "CommandFilter.h"

void CommandFilter::clear() {
    for (int i = 0; i < FILTER_QUEUE_SIZE; i++) _queue[i] = 0;
    _head = 0;
    votesLeft = votesRight = 0;
    votesRed = votesSilver = votesNoLine = votesBlack = votesSearchLineBlack = 0;
}

uint8_t CommandFilter::update(uint8_t rawCmd) {
    _queue[_head] = rawCmd;
    _head = (_head + 1) % FILTER_QUEUE_SIZE;

    // Tally the events seen in the rolling window.
    votesLeft = votesRight = 0;
    votesRed = votesSilver = votesNoLine = votesBlack = votesSearchLineBlack = 0;
    for (int i = 0; i < FILTER_QUEUE_SIZE; i++) {
        switch (_queue[i]) {
            // FEAT_UTURN = both-green in the same frame: count it toward BOTH
            // sides. U-turn is then inferred when left AND right both build up —
            // no dedicated u-turn vote/type.
            case FEAT_UTURN:       votesLeft++; votesRight++; break;
            case FEAT_GREEN_LEFT:  votesLeft++;   break;
            case FEAT_GREEN_RIGHT: votesRight++;  break;
            case FEAT_RED:         votesRed++;    break;
            case FEAT_SILVER:      votesSilver++; break;
            case FEAT_LINE_LOST:   votesNoLine++; break;
            case 6:  // FEAT_BLACK_INTERSECT in LINE mode, FEAT_SEARCH_LINE_BLACK in SEARCH_LINE mode
                votesBlack++;
                votesSearchLineBlack++;  // same feature id is used in SEARCH_LINE mode
                break;
            default: break;
        }
    }

    // Safety first.
    if (votesRed    >= FILTER_THRESHOLD_RED)    return FEAT_RED;
    if (votesSilver >= FILTER_THRESHOLD_SILVER) return FEAT_SILVER;

    // U-turn = left AND right both build up (main-branch logic). One side strong
    // with the other barely present, or both moderately present — looser than the
    // single-side green threshold so a real both-green wins before a plain turn.
    const bool isUturn = (votesLeft  >= 6 && votesRight >= 1)
                      || (votesRight >= 6 && votesLeft  >= 1)
                      || (votesLeft  >= 4 && votesRight >= 4);
    if (isUturn) return FEAT_UTURN;

    // Green left/right.
    if (votesLeft  >= 8) return FEAT_GREEN_LEFT;
    if (votesRight >= 8) return FEAT_GREEN_RIGHT;

    // Saturated black row: suppress possible green rereads at intersections.
    if (votesBlack >= FILTER_THRESHOLD_BLACK_INTERSECT) return FEAT_BLACK_INTERSECT;

    // Sustained line loss.
    if (votesNoLine >= FILTER_THRESHOLD_LINELOST) return FEAT_LINE_LOST;

    // Mode-scoped: SEARCH_LINE-mode black return line (uturn recovery waits on this).
    if (votesSearchLineBlack >= 2) return FEAT_SEARCH_LINE_BLACK;
    return FEAT_NONE;
}
