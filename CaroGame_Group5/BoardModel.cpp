#include "BoardModel.h"

#include <algorithm>

BoardMove::BoardMove()
    : row(0), col(0), mark(CellState::Empty)
{
}

BoardMove::BoardMove(int rowValue, int colValue, CellState markValue)
    : row(rowValue), col(colValue), mark(markValue)
{
}

BoardModel::BoardModel()
    : cells(Size * Size, CellState::Empty), cursorRow(0), cursorCol(0)
{
}

void BoardModel::reset()
{
    std::fill(cells.begin(), cells.end(), CellState::Empty);
    moves.clear();
    cursorRow = 0;
    cursorCol = 0;
}

bool BoardModel::isInside(int row, int col) const
{
    return row >= 0 && row < Size && col >= 0 && col < Size;
}

bool BoardModel::isEmpty(int row, int col) const
{
    return isInside(row, col) && getCell(row, col) == CellState::Empty;
}

bool BoardModel::isFull() const
{
    for (size_t i = 0; i < cells.size(); ++i)
    {
        if (cells[i] == CellState::Empty) return false;
    }
    return true;
}

bool BoardModel::placeMove(int row, int col, CellState mark)
{
    if (!isEmpty(row, col)) return false;
    cells[indexOf(row, col)] = mark;
    moves.push_back(BoardMove(row, col, mark));
    cursorRow = row;
    cursorCol = col;
    return true;
}

bool BoardModel::undoLast()
{
    if (moves.empty()) return false;
    BoardMove last = moves.back();
    moves.pop_back();
    if (isInside(last.row, last.col))
    {
        cells[indexOf(last.row, last.col)] = CellState::Empty;
        cursorRow = last.row;
        cursorCol = last.col;
    }
    return true;
}

int BoardModel::undoBotPair()
{
    int undone = 0;
    if (undoLast()) ++undone;
    if (undoLast()) ++undone;
    return undone;
}

CellState BoardModel::getCell(int row, int col) const
{
    if (!isInside(row, col)) return CellState::Empty;
    return cells[indexOf(row, col)];
}

const std::vector<BoardMove>& BoardModel::getMoves() const
{
    return moves;
}

int BoardModel::moveCount() const
{
    return static_cast<int>(moves.size());
}

int BoardModel::getCursorRow() const
{
    return cursorRow;
}

int BoardModel::getCursorCol() const
{
    return cursorCol;
}

void BoardModel::setCursor(int row, int col)
{
    if (!isInside(row, col)) return;
    cursorRow = row;
    cursorCol = col;
}

void BoardModel::moveCursor(int dRow, int dCol)
{
    int nextRow = cursorRow + dRow;
    int nextCol = cursorCol + dCol;
    if (nextRow < 0) nextRow = 0;
    if (nextRow >= Size) nextRow = Size - 1;
    if (nextCol < 0) nextCol = 0;
    if (nextCol >= Size) nextCol = Size - 1;
    cursorRow = nextRow;
    cursorCol = nextCol;
}

bool BoardModel::checkWin(int row, int col, CellState mark,
    std::vector<std::pair<int, int> >* winningCells) const
{
    const int directions[4][2] = { {0, 1}, {1, 0}, {1, 1}, {1, -1} };
    for (int i = 0; i < 4; ++i)
    {
        int dRow = directions[i][0];
        int dCol = directions[i][1];
        int total = 1
            + countDirection(row, col, dRow, dCol, mark)
            + countDirection(row, col, -dRow, -dCol, mark);
        if (total >= 5)
        {
            if (winningCells)
            {
                winningCells->clear();
                int startRow = row;
                int startCol = col;
                while (isInside(startRow - dRow, startCol - dCol)
                    && getCell(startRow - dRow, startCol - dCol) == mark)
                {
                    startRow -= dRow;
                    startCol -= dCol;
                }
                for (int n = 0; n < total; ++n)
                {
                    int r = startRow + dRow * n;
                    int c = startCol + dCol * n;
                    if (isInside(r, c) && getCell(r, c) == mark)
                        winningCells->push_back(std::make_pair(r, c));
                }
            }
            return true;
        }
    }
    return false;
}

std::vector<int> BoardModel::serializeGrid() const
{
    std::vector<int> values;
    values.reserve(cells.size());
    for (size_t i = 0; i < cells.size(); ++i)
        values.push_back(static_cast<int>(cells[i]));
    return values;
}

void BoardModel::loadGrid(const std::vector<int>& gridValues,
    const std::vector<BoardMove>& savedMoves)
{
    reset();
    for (int i = 0; i < Size * Size && i < static_cast<int>(gridValues.size()); ++i)
    {
        int value = gridValues[i];
        if (value == 1) cells[i] = CellState::X;
        else if (value == 2) cells[i] = CellState::O;
        else cells[i] = CellState::Empty;
    }
    moves = savedMoves;
    if (!moves.empty())
    {
        cursorRow = moves.back().row;
        cursorCol = moves.back().col;
    }
}

int BoardModel::indexOf(int row, int col) const
{
    return row * Size + col;
}

int BoardModel::countDirection(int row, int col, int dRow, int dCol,
    CellState mark) const
{
    int total = 0;
    int r = row + dRow;
    int c = col + dCol;
    while (isInside(r, c) && getCell(r, c) == mark)
    {
        ++total;
        r += dRow;
        c += dCol;
    }
    return total;
}
