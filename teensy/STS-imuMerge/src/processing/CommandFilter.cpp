#include "CommandFilter.h"

void CommandFilter::clear() {
    for (int i = 0; i < FILTER_QUEUE_SIZE; i++) _queue[i] = 0;
    _head = 0;
    votesUturn = votesRed = votesSilver = votesLineLost = votesSearchLineBlack = 0;
    votesGreenLeft = votesGreenRight = 0;
}

uint8_t CommandFilter::update(uint8_t rawCmd) {
    _queue[_head] = rawCmd;
    _head = (_head + 1) % FILTER_QUEUE_SIZE;

    // Tally the events seen in the rolling window.
    votesUturn = votesRed = votesSilver = votesLineLost = votesSearchLineBlack = 0;
    votesGreenLeft = votesGreenRight = 0;
    for (int i = 0; i < FILTER_QUEUE_SIZE; i++) {
        switch (_queue[i]) {
            case FEAT_UTURN:      votesUturn++;     break;
            case FEAT_RED:        votesRed++;       break;
            case FEAT_SILVER:     votesSilver++;    break;
            case FEAT_LINE_LOST:  votesLineLost++;  break;
            case FEAT_GREEN_LEFT:  votesGreenLeft++;  break;
            case FEAT_GREEN_RIGHT: votesGreenRight++; break;
            case FEAT_SEARCH_LINE_BLACK: votesSearchLineBlack++; break;  // only sent in SEARCH_LINE mode
            default: break;
        }
    }

    // Confirm in priority order (safety first, then navigation, then line loss).
    if (votesRed      >= FILTER_THRESHOLD_RED)      return FEAT_RED;
    if (votesSilver   >= FILTER_THRESHOLD_SILVER)   return FEAT_SILVER;
    if (votesUturn    >= FILTER_THRESHOLD_UTURN)    return FEAT_UTURN;
    if (votesGreenLeft  >= FILTER_THRESHOLD_GREEN)  return FEAT_GREEN_LEFT;
    if (votesGreenRight >= FILTER_THRESHOLD_GREEN)  return FEAT_GREEN_RIGHT;
    if (votesLineLost >= FILTER_THRESHOLD_LINELOST) return FEAT_LINE_LOST;

    // Mode-scoped: SEARCH_LINE-mode black return line (LINE_Obstacle waits on this).
    if (votesSearchLineBlack >= 2) return FEAT_SEARCH_LINE_BLACK;
    return FEAT_NONE;
}
