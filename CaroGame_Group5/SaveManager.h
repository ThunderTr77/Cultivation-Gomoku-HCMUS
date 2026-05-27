#pragma once

#include "BoardModel.h"
#include "BotEngine.h"

#include <string>
#include <vector>

struct SaveRecord
{
    std::string fileName;
    std::string player1Name;
    std::string player2Name;
    std::string dateSaved;
    std::string status;
};

struct SaveGameData
{
    std::string mode;
    BotLevel botLevel;
    std::string player1Name;
    std::string player2Name;
    int player1Avatar;
    int player2Avatar;
    CellState currentTurn;
    int elapsedSeconds;
    std::string status;
    std::vector<int> grid;
    std::vector<BoardMove> moves;

    SaveGameData();
};

class SaveManager
{
public:
    SaveManager();

    std::string normalizeName(const std::string& name) const;
    bool exists(const std::string& name) const;
    bool saveGame(const std::string& name, const SaveGameData& data, bool overwrite);
    bool loadGame(const std::string& name, SaveGameData& data,
        bool requirePlaying, std::string* errorMessage);
    std::vector<SaveRecord> getAllRecords() const;

private:
    std::string savesDir;
    std::string historyFile;

    void ensureDirectories() const;
    std::string pathFor(const std::string& normalizedName) const;
    std::string currentDateTime() const;
    std::vector<SaveRecord> readHistory() const;
    void writeHistory(const std::vector<SaveRecord>& records) const;
    void updateHistory(const SaveGameData& data, const std::string& fileName);
    bool loadLegacy(const std::string& path, SaveGameData& data);
};
