#pragma once

#include "BoardModel.h"

enum class BotLevel
{
    Easy = 0,
    Medium = 1,
    Hard = 2
};

class BotEngine
{
public:
    BoardMove chooseMove(const BoardModel& board, BotLevel level,
        CellState botMark, CellState humanMark);

private:
    struct Candidate
    {
        int row;
        int col;
        int score;
        Candidate(int rowValue, int colValue, int scoreValue)
            : row(rowValue), col(colValue), score(scoreValue) {}
    };

    std::vector<Candidate> collectCandidates(const BoardModel& board) const;
    int scoreMove(const BoardModel& board, int row, int col, CellState who) const;
    int countLine(const BoardModel& board, int row, int col, int dRow, int dCol,
        CellState who, int& openEnds) const;
    bool completesFive(const BoardModel& board, int row, int col, CellState who) const;
    int centerScore(int row, int col) const;
    int bestReplyScore(const BoardModel& board, int blockedRow, int blockedCol,
        CellState replyMark) const;
    int tacticalScore(const BoardModel& board, int row, int col,
        CellState botMark, CellState humanMark, BotLevel level) const;
};
