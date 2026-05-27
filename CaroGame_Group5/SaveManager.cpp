#include "SaveManager.h"

#include <ctime>
#include <cstring>
#include <fstream>
#include <sstream>
#include <windows.h>

SaveGameData::SaveGameData()
    : mode("two"), botLevel(BotLevel::Easy),
    player1Name("Đạo Hữu 1"), player2Name("Đạo Hữu 2"),
    player1Avatar(0), player2Avatar(0), currentTurn(CellState::X),
    elapsedSeconds(0), status("playing")
{
}

SaveManager::SaveManager()
    : savesDir("assets/saves"), historyFile("assets/saves/history.txt")
{
    ensureDirectories();
}

std::string SaveManager::normalizeName(const std::string& name) const
{
    std::string result = name;
    if (result.empty()) result = "game";
    const char* bad = "\\/:*?\"<>|";
    for (size_t i = 0; i < result.size(); ++i)
    {
        if (std::strchr(bad, result[i])) result[i] = '_';
    }
    if (result.size() < 4 || result.substr(result.size() - 4) != ".sav")
        result += ".sav";
    return result;
}

bool SaveManager::exists(const std::string& name) const
{
    std::string path = pathFor(normalizeName(name));
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool SaveManager::saveGame(const std::string& name, const SaveGameData& data, bool overwrite)
{
    ensureDirectories();
    std::string normalized = normalizeName(name);
    if (exists(normalized) && !overwrite) return false;

    std::ofstream file(pathFor(normalized).c_str(), std::ios::out | std::ios::trunc);
    if (!file.is_open()) return false;

    file << "CARO_SFML_V1\n";
    file << "mode " << data.mode << "\n";
    file << "botLevel " << static_cast<int>(data.botLevel) << "\n";
    file << "p1 " << data.player1Name << "\n";
    file << "p2 " << data.player2Name << "\n";
    file << "p1Avatar " << data.player1Avatar << "\n";
    file << "p2Avatar " << data.player2Avatar << "\n";
    file << "turn " << static_cast<int>(data.currentTurn) << "\n";
    file << "elapsed " << data.elapsedSeconds << "\n";
    file << "status " << data.status << "\n";
    file << "grid\n";
    for (int row = 0; row < BoardModel::Size; ++row)
    {
        for (int col = 0; col < BoardModel::Size; ++col)
        {
            int idx = row * BoardModel::Size + col;
            int value = (idx < static_cast<int>(data.grid.size())) ? data.grid[idx] : 0;
            file << value;
            if (col + 1 < BoardModel::Size) file << ' ';
        }
        file << "\n";
    }
    file << "moves " << data.moves.size() << "\n";
    for (size_t i = 0; i < data.moves.size(); ++i)
    {
        file << data.moves[i].row << ' ' << data.moves[i].col << ' '
            << static_cast<int>(data.moves[i].mark) << "\n";
    }
    file.close();
    updateHistory(data, normalized);
    return true;
}

bool SaveManager::loadGame(const std::string& name, SaveGameData& data,
    bool requirePlaying, std::string* errorMessage)
{
    std::string normalized = normalizeName(name);
    std::string path = pathFor(normalized);
    if (!exists(normalized))
    {
        DWORD rootAttr = GetFileAttributesA(normalized.c_str());
        if (rootAttr != INVALID_FILE_ATTRIBUTES) path = normalized;
        else
        {
            if (errorMessage) *errorMessage = "missing";
            return false;
        }
    }

    std::ifstream file(path.c_str());
    if (!file.is_open())
    {
        if (errorMessage) *errorMessage = "open";
        return false;
    }

    std::string firstLine;
    std::getline(file, firstLine);
    if (firstLine != "CARO_SFML_V1")
    {
        file.close();
        if (!loadLegacy(path, data))
        {
            if (errorMessage) *errorMessage = "format";
            return false;
        }
        if (requirePlaying && data.status != "playing")
        {
            if (errorMessage) *errorMessage = "not playing";
            return false;
        }
        return true;
    }

    data = SaveGameData();
    data.grid.assign(BoardModel::Size * BoardModel::Size, 0);
    std::string key;
    while (file >> key)
    {
        if (key == "mode") file >> data.mode;
        else if (key == "botLevel")
        {
            int value = 0;
            file >> value;
            data.botLevel = static_cast<BotLevel>(value);
        }
        else if (key == "p1")
        {
            file.ignore(1);
            std::getline(file, data.player1Name);
        }
        else if (key == "p2")
        {
            file.ignore(1);
            std::getline(file, data.player2Name);
        }
        else if (key == "p1Avatar") file >> data.player1Avatar;
        else if (key == "p2Avatar") file >> data.player2Avatar;
        else if (key == "turn")
        {
            int value = 1;
            file >> value;
            data.currentTurn = static_cast<CellState>(value);
        }
        else if (key == "elapsed") file >> data.elapsedSeconds;
        else if (key == "status") file >> data.status;
        else if (key == "grid")
        {
            for (int i = 0; i < BoardModel::Size * BoardModel::Size; ++i)
                file >> data.grid[i];
        }
        else if (key == "moves")
        {
            int count = 0;
            file >> count;
            data.moves.clear();
            for (int i = 0; i < count; ++i)
            {
                int row = 0, col = 0, mark = 0;
                file >> row >> col >> mark;
                data.moves.push_back(BoardMove(row, col, static_cast<CellState>(mark)));
            }
        }
    }

    if (requirePlaying && data.status != "playing")
    {
        if (errorMessage) *errorMessage = "not playing";
        return false;
    }
    return true;
}

std::vector<SaveRecord> SaveManager::getAllRecords() const
{
    return readHistory();
}

void SaveManager::ensureDirectories() const
{
    CreateDirectoryA("assets", 0);
    CreateDirectoryA(savesDir.c_str(), 0);
}

std::string SaveManager::pathFor(const std::string& normalizedName) const
{
    return savesDir + "/" + normalizedName;
}

std::string SaveManager::currentDateTime() const
{
    time_t now = time(0);
    tm localTime;
    localtime_s(&localTime, &now);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M", &localTime);
    return std::string(buffer);
}

std::vector<SaveRecord> SaveManager::readHistory() const
{
    std::vector<SaveRecord> records;
    std::ifstream file(historyFile.c_str());
    if (!file.is_open()) return records;
    int count = 0;
    file >> count;
    file.ignore(10000, '\n');
    for (int i = 0; i < count; ++i)
    {
        std::string line;
        if (!std::getline(file, line)) break;
        std::stringstream ss(line);
        SaveRecord rec;
        std::getline(ss, rec.fileName, '|');
        std::getline(ss, rec.player1Name, '|');
        std::getline(ss, rec.player2Name, '|');
        std::getline(ss, rec.dateSaved, '|');
        std::getline(ss, rec.status, '|');
        records.push_back(rec);
    }
    return records;
}

void SaveManager::writeHistory(const std::vector<SaveRecord>& records) const
{
    ensureDirectories();
    std::ofstream file(historyFile.c_str(), std::ios::out | std::ios::trunc);
    if (!file.is_open()) return;
    file << records.size() << "\n";
    for (size_t i = 0; i < records.size(); ++i)
    {
        file << records[i].fileName << '|'
            << records[i].player1Name << '|'
            << records[i].player2Name << '|'
            << records[i].dateSaved << '|'
            << records[i].status << "\n";
    }
}

void SaveManager::updateHistory(const SaveGameData& data, const std::string& fileName)
{
    std::vector<SaveRecord> records = readHistory();
    bool found = false;
    for (size_t i = 0; i < records.size(); ++i)
    {
        if (records[i].fileName == fileName)
        {
            records[i].player1Name = data.player1Name;
            records[i].player2Name = data.player2Name;
            records[i].dateSaved = currentDateTime();
            records[i].status = data.status;
            found = true;
            break;
        }
    }
    if (!found)
    {
        SaveRecord rec;
        rec.fileName = fileName;
        rec.player1Name = data.player1Name;
        rec.player2Name = data.player2Name;
        rec.dateSaved = currentDateTime();
        rec.status = data.status;
        records.push_back(rec);
    }
    writeHistory(records);
}

bool SaveManager::loadLegacy(const std::string& path, SaveGameData& data)
{
    std::ifstream file(path.c_str());
    if (!file.is_open()) return false;
    data = SaveGameData();
    std::getline(file, data.player1Name);
    std::getline(file, data.player2Name);
    int turn = 1;
    file >> turn;
    data.currentTurn = (turn == 1) ? CellState::X : CellState::O;
    file >> data.elapsedSeconds;
    int size = 0;
    file >> size;
    if (size != BoardModel::Size) return false;
    data.grid.assign(BoardModel::Size * BoardModel::Size, 0);
    for (int i = 0; i < BoardModel::Size * BoardModel::Size; ++i)
        file >> data.grid[i];
    data.status = "playing";
    data.mode = "two";
    return true;
}
