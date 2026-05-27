#pragma once

#include <utility>
#include <vector>

enum class CellState
{
    Empty = 0,
    X = 1,
    O = 2
};

struct BoardMove
{
    int row;
    int col;
    CellState mark;

    BoardMove();
    BoardMove(int rowValue, int colValue, CellState markValue);
};

class BoardModel
{
public:
    static const int Size = 12;

    BoardModel();

    void reset();
    bool isInside(int row, int col) const;
    bool isEmpty(int row, int col) const;
    bool isFull() const;
    bool placeMove(int row, int col, CellState mark);
    bool undoLast();
    int undoBotPair();

    CellState getCell(int row, int col) const;
    const std::vector<BoardMove>& getMoves() const;
    int moveCount() const;

    int getCursorRow() const;
    int getCursorCol() const;
    void setCursor(int row, int col);
    void moveCursor(int dRow, int dCol);

    bool checkWin(int row, int col, CellState mark,
        std::vector<std::pair<int, int> >* winningCells = 0) const;

    std::vector<int> serializeGrid() const;
    void loadGrid(const std::vector<int>& gridValues,
        const std::vector<BoardMove>& savedMoves);

private:
    std::vector<CellState> cells;
    std::vector<BoardMove> moves;
    int cursorRow;
    int cursorCol;

    int indexOf(int row, int col) const;
    int countDirection(int row, int col, int dRow, int dCol,
        CellState mark) const;
};
