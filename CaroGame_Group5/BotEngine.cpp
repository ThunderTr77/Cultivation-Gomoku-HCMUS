#include "BotEngine.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <climits>

BoardMove BotEngine::chooseMove(const BoardModel& board, BotLevel level,
    CellState botMark, CellState humanMark)
{
    std::vector<Candidate> candidates = collectCandidates(board);
    if (candidates.empty())
        return BoardMove(BoardModel::Size / 2, BoardModel::Size / 2, botMark);

    static bool seeded = false;
    if (!seeded)
    {
        std::srand(static_cast<unsigned int>(std::time(0)));
        seeded = true;
    }

    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (completesFive(board, candidates[i].row, candidates[i].col, botMark))
            return BoardMove(candidates[i].row, candidates[i].col, botMark);
    }

    if (level != BotLevel::Easy)
    {
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            if (completesFive(board, candidates[i].row, candidates[i].col, humanMark))
                return BoardMove(candidates[i].row, candidates[i].col, botMark);
        }
    }

    for (size_t i = 0; i < candidates.size(); ++i)
    {
        candidates[i].score = tacticalScore(board, candidates[i].row, candidates[i].col,
            botMark, humanMark, level);
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

    int range = 1;
    if (level == BotLevel::Easy) range = std::min(9, static_cast<int>(candidates.size()));
    else if (level == BotLevel::Medium) range = std::min(2, static_cast<int>(candidates.size()));

    int picked = (range <= 1) ? 0 : (std::rand() % range);
    return BoardMove(candidates[picked].row, candidates[picked].col, botMark);
}

std::vector<BotEngine::Candidate> BotEngine::collectCandidates(const BoardModel& board) const
{
    std::vector<Candidate> result;
    if (board.moveCount() == 0)
    {
        result.push_back(Candidate(BoardModel::Size / 2, BoardModel::Size / 2, 0));
        return result;
    }

    for (int row = 0; row < BoardModel::Size; ++row)
    {
        for (int col = 0; col < BoardModel::Size; ++col)
        {
            if (!board.isEmpty(row, col)) continue;
            bool near = false;
            for (int dr = -2; dr <= 2 && !near; ++dr)
            {
                for (int dc = -2; dc <= 2 && !near; ++dc)
                {
                    if (dr == 0 && dc == 0) continue;
                    int rr = row + dr;
                    int cc = col + dc;
                    if (board.isInside(rr, cc) && board.getCell(rr, cc) != CellState::Empty)
                        near = true;
                }
            }
            if (near) result.push_back(Candidate(row, col, 0));
        }
    }
    return result;
}

int BotEngine::scoreMove(const BoardModel& board, int row, int col, CellState who) const
{
    const int dirs[4][2] = { {0, 1}, {1, 0}, {1, 1}, {1, -1} };
    int totalScore = 0;
    for (int i = 0; i < 4; ++i)
    {
        int openEnds = 0;
        int count = 1
            + countLine(board, row, col, dirs[i][0], dirs[i][1], who, openEnds)
            + countLine(board, row, col, -dirs[i][0], -dirs[i][1], who, openEnds);

        if (count >= 5) totalScore += 1000000;
        else if (count == 4 && openEnds == 2) totalScore += 90000;
        else if (count == 4) totalScore += 30000;
        else if (count == 3 && openEnds == 2) totalScore += 9000;
        else if (count == 3) totalScore += 2500;
        else if (count == 2 && openEnds == 2) totalScore += 700;
        else if (count == 2) totalScore += 180;
        else totalScore += 20;
    }
    totalScore += centerScore(row, col);
    return totalScore;
}

int BotEngine::countLine(const BoardModel& board, int row, int col, int dRow, int dCol,
    CellState who, int& openEnds) const
{
    int count = 0;
    int r = row + dRow;
    int c = col + dCol;
    while (board.isInside(r, c) && board.getCell(r, c) == who)
    {
        ++count;
        r += dRow;
        c += dCol;
    }
    if (board.isInside(r, c) && board.getCell(r, c) == CellState::Empty)
        ++openEnds;
    return count;
}

bool BotEngine::completesFive(const BoardModel& board, int row, int col, CellState who) const
{
    const int dirs[4][2] = { {0, 1}, {1, 0}, {1, 1}, {1, -1} };
    for (int i = 0; i < 4; ++i)
    {
        int openEnds = 0;
        int total = 1
            + countLine(board, row, col, dirs[i][0], dirs[i][1], who, openEnds)
            + countLine(board, row, col, -dirs[i][0], -dirs[i][1], who, openEnds);
        if (total >= 5) return true;
    }
    return false;
}

int BotEngine::centerScore(int row, int col) const
{
    float center = (BoardModel::Size - 1) * 0.5f;
    float dist = static_cast<float>(abs(static_cast<int>(center * 2) - row * 2)
        + abs(static_cast<int>(center * 2) - col * 2)) * 0.5f;
    int score = 110 - static_cast<int>(dist * 12.0f);
    return score < 0 ? 0 : score;
}

int BotEngine::bestReplyScore(const BoardModel& board, int blockedRow, int blockedCol,
    CellState replyMark) const
{
    std::vector<Candidate> replies = collectCandidates(board);
    int best = 0;
    for (size_t i = 0; i < replies.size(); ++i)
    {
        if (replies[i].row == blockedRow && replies[i].col == blockedCol) continue;
        int score = scoreMove(board, replies[i].row, replies[i].col, replyMark);
        if (score > best) best = score;
    }
    return best;
}

int BotEngine::tacticalScore(const BoardModel& board, int row, int col,
    CellState botMark, CellState humanMark, BotLevel level) const
{
    int bot = scoreMove(board, row, col, botMark);
    int human = scoreMove(board, row, col, humanMark);
    int score = 0;
    if (level == BotLevel::Easy)
    {
        score = centerScore(row, col) + bot / 18 + human / 28 + (std::rand() % 140);
    }
    else if (level == BotLevel::Medium)
    {
        score = bot + human * 13 / 10 + centerScore(row, col) * 2;
    }
    else
    {
        int reply = bestReplyScore(board, row, col, humanMark);
        score = bot * 15 / 10 + human * 14 / 10 + centerScore(row, col) * 3 - reply / 5;
        if (bot >= 90000) score += 220000;
        if (human >= 90000) score += 180000;
        if (bot >= 9000 && human >= 9000) score += 32000;
    }
    return score;
}
