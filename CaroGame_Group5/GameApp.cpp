#include "GameApp.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <windows.h>

namespace
{
    const unsigned int WindowW = 1440;
    const unsigned int WindowH = 810;
    const sf::FloatRect BoardRect(72.0f, 92.0f, 650.0f, 650.0f);
    const int AvatarCols = 4;
    const float AvatarX0 = 106.0f;
    const float AvatarY0 = 168.0f;
    const float AvatarCellW = 178.0f;
    const float AvatarCellH = 136.0f;
    const sf::Vector2f AvatarCardSize(158.0f, 134.0f);

    float clampFloat(float v, float lo, float hi)
    {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    CellState opposite(CellState mark)
    {
        return mark == CellState::X ? CellState::O : CellState::X;
    }

    bool endsWithNoCase(const std::string& value, const std::string& suffix)
    {
        if (value.size() < suffix.size()) return false;
        std::string tail = value.substr(value.size() - suffix.size());
        std::string suf = suffix;
        std::transform(tail.begin(), tail.end(), tail.begin(), ::tolower);
        std::transform(suf.begin(), suf.end(), suf.begin(), ::tolower);
        return tail == suf;
    }

    sf::String utf8(const std::string& value)
    {
        return sf::String::fromUtf8(value.begin(), value.end());
    }

    void appendUtf8(std::string& out, sf::Uint32 codepoint)
    {
        if (codepoint <= 0x7F)
        {
            out += static_cast<char>(codepoint);
        }
        else if (codepoint <= 0x7FF)
        {
            out += static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else if (codepoint <= 0xFFFF)
        {
            out += static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
            out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
    }

    void eraseLastUtf8(std::string& value)
    {
        if (value.empty()) return;
        size_t pos = value.size() - 1;
        while (pos > 0 && (static_cast<unsigned char>(value[pos]) & 0xC0) == 0x80)
            --pos;
        value.erase(pos);
    }

    std::string cellName(int row, int col)
    {
        std::ostringstream out;
        out << static_cast<char>('A' + col) << (row + 1);
        return out.str();
    }

    bool fileExists(const char* path)
    {
        DWORD attr = GetFileAttributesA(path);
        return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    }

    bool isCardBackgroundPixel(const sf::Color& color)
    {
        int maxChannel = std::max(static_cast<int>(color.r),
            std::max(static_cast<int>(color.g), static_cast<int>(color.b)));
        int minChannel = std::min(static_cast<int>(color.r),
            std::min(static_cast<int>(color.g), static_cast<int>(color.b)));
        return color.r >= 224 && color.g >= 224 && color.b >= 224
            && maxChannel - minChannel <= 36;
    }

    bool loadCardTextureWithTransparentBackground(const std::string& path, sf::Texture& texture)
    {
        sf::Image image;
        if (!image.loadFromFile(path)) return false;

        sf::Vector2u size = image.getSize();
        if (size.x == 0 || size.y == 0) return false;

        std::vector<unsigned char> visited(size.x * size.y, 0);
        std::vector<sf::Vector2u> stack;
        stack.reserve(size.x + size.y);

        auto pushIfBackground = [&](unsigned int x, unsigned int y)
        {
            size_t index = static_cast<size_t>(y) * size.x + x;
            if (visited[index]) return;
            visited[index] = 1;
            if (isCardBackgroundPixel(image.getPixel(x, y)))
                stack.push_back(sf::Vector2u(x, y));
        };

        for (unsigned int x = 0; x < size.x; ++x)
        {
            pushIfBackground(x, 0);
            pushIfBackground(x, size.y - 1);
        }
        for (unsigned int y = 0; y < size.y; ++y)
        {
            pushIfBackground(0, y);
            pushIfBackground(size.x - 1, y);
        }

        while (!stack.empty())
        {
            sf::Vector2u p = stack.back();
            stack.pop_back();

            sf::Color transparent = image.getPixel(p.x, p.y);
            transparent.a = 0;
            image.setPixel(p.x, p.y, transparent);

            if (p.x > 0) pushIfBackground(p.x - 1, p.y);
            if (p.x + 1 < size.x) pushIfBackground(p.x + 1, p.y);
            if (p.y > 0) pushIfBackground(p.x, p.y - 1);
            if (p.y + 1 < size.y) pushIfBackground(p.x, p.y + 1);
        }

        return texture.loadFromImage(image);
    }
}

GameApp::GameApp()
    : hasTamma(false), hasCardTexture(false), musicVolume(70.0f), sfxVolume(80.0f),
    screen(Screen::Menu), previousScreen(Screen::Menu), dialogMode(DialogMode::None),
    language("vi"), vsBot(false), botLevel(BotLevel::Easy), currentTurn(CellState::X),
    player1Name("Nguoi choi 1"), player2Name("Nguoi choi 2"),
    player1Avatar(0), player2Avatar(0), selectedAvatar(0), selectingPlayer(1),
    focusedNameField(0), fateScoreX(0), fateScoreO(0), fateTimer(0.0f),
    fateOpening(false), fateXRevealed(false), fateORevealed(false),
    startingTurn(CellState::X),
    botThinking(false), botThinkTimer(0.0f), matchFinished(false),
    loadedElapsedSeconds(0), animationTime(0.0f), selectedHistory(-1)
{
}

int GameApp::run()
{
    initialize();
    sf::Clock clock;
    while (window.isOpen())
    {
        float dt = clock.restart().asSeconds();
        sf::Event event;
        while (window.pollEvent(event))
            handleEvent(event);
        update(dt);
        draw();
    }
    saveSettings();
    return 0;
}

void GameApp::initialize()
{
    configureWorkingDirectory();
    std::srand(static_cast<unsigned int>(std::time(0)));
    window.create(sf::VideoMode(WindowW, WindowH), utf8("Thiên Cơ Kỳ Bàn"),
        sf::Style::Titlebar | sf::Style::Close);
    window.setFramerateLimit(60);
    window.setVerticalSyncEnabled(true);

    loadSettings();
    loadLocalization(language);
    loadAssets();
    startMusic();
    if (!menuVideo.open("assets/video/video.mp4") && !menuVideo.open("assets/video/video.m4"))
        menuVideo.openFrameSequence("assets/video/menu_frames");
    resetParticles();
}

void GameApp::configureWorkingDirectory()
{
    if (fileExists("assets\\video\\video.mp4")) return;

    char modulePath[MAX_PATH];
    DWORD len = GetModuleFileNameA(0, modulePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return;

    std::string path(modulePath);
    size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos) return;
    std::string exeDir = path.substr(0, slash);
    std::vector<std::string> candidates;
    candidates.push_back(exeDir + "\\..\\..\\CaroGame_Group5");
    candidates.push_back(exeDir + "\\CaroGame_Group5");
    candidates.push_back(exeDir + "\\..\\CaroGame_Group5");

    for (size_t i = 0; i < candidates.size(); ++i)
    {
        std::string videoPath = candidates[i] + "\\assets\\video\\video.mp4";
        if (fileExists(videoPath.c_str()))
        {
            SetCurrentDirectoryA(candidates[i].c_str());
            return;
        }
    }
}

void GameApp::loadSettings()
{
    std::ifstream file("assets/settings.ini");
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line))
    {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (key == "language") language = value;
        else if (key == "music") musicVolume = static_cast<float>(std::atof(value.c_str()));
        else if (key == "sfx") sfxVolume = static_cast<float>(std::atof(value.c_str()));
    }
}

void GameApp::saveSettings()
{
    std::ofstream file("assets/settings.ini", std::ios::out | std::ios::trunc);
    if (!file.is_open()) return;
    file << "language=" << language << "\n";
    file << "music=" << static_cast<int>(musicVolume) << "\n";
    file << "sfx=" << static_cast<int>(sfxVolume) << "\n";
}

void GameApp::loadLocalization(const std::string& lang)
{
    text.clear();
    std::string path = "assets/lang/" + lang + ".txt";
    std::ifstream file(path.c_str());
    if (!file.is_open() && lang != "en")
        file.open("assets/lang/en.txt");
    std::string line;
    while (std::getline(file, line))
    {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        text[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

std::string GameApp::tr(const std::string& key) const
{
    std::map<std::string, std::string>::const_iterator it = text.find(key);
    if (it != text.end()) return it->second;
    return key;
}

void GameApp::loadAssets()
{
    font.loadFromFile("assets/fonts/DFVN-FreckleFace.ttf");
    loadBackground("menu", "assets/backgrounds/menu.png");
    loadBackground("mode", "assets/backgrounds/mode.png");
    loadBackground("avatar", "assets/backgrounds/avatar.png");
    loadBackground("game_two", "assets/backgrounds/game_two.png");
    loadBackground("game_bot", "assets/backgrounds/game_bot.png");
    loadBackground("history", "assets/backgrounds/history.png");
    loadBackground("settings", "assets/backgrounds/settings.png");
    loadBackground("guide", "assets/backgrounds/guide.png");
    loadBackground("info", "assets/backgrounds/info.png");
    loadBackground("win", "assets/backgrounds/win.png");
    loadBackground("lose", "assets/backgrounds/lose.png");
    loadBackground("draw", "assets/backgrounds/draw.png");

    loadAvatars();
    hasTamma = tammaTexture.loadFromFile("assets/char/tamma.jpg");
    if (hasTamma) tammaTexture.setSmooth(true);
    hasCardTexture = loadCardTextureWithTransparentBackground("assets/item/card.png", cardTexture);
    if (hasCardTexture) cardTexture.setSmooth(true);

    chessBuffer.loadFromFile("assets/audio/chess.wav");
    killBuffer.loadFromFile("assets/audio/kiem.wav");
    chessSound.setBuffer(chessBuffer);
    killSound.setBuffer(killBuffer);
    applyVolumes();
}

void GameApp::loadBackground(const std::string& key, const std::string& path)
{
    sf::Texture texture;
    if (texture.loadFromFile(path))
    {
        texture.setSmooth(true);
        backgrounds[key] = texture;
    }
}

void GameApp::loadAvatars()
{
    avatars.clear();
    WIN32_FIND_DATAA data;
    HANDLE hFind = FindFirstFileA("assets/char/*.*", &data);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do
    {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string name = data.cFileName;
        if (name == "tamma.jpg") continue;
        if (!endsWithNoCase(name, ".png") && !endsWithNoCase(name, ".jpg")
            && !endsWithNoCase(name, ".jpeg"))
            continue;
        AvatarItem item;
        item.name = name;
        size_t dot = item.name.find_last_of('.');
        if (dot != std::string::npos) item.name = item.name.substr(0, dot);
        item.path = "assets/char/" + name;
        if (item.texture.loadFromFile(item.path))
        {
            item.texture.setSmooth(true);
            avatars.push_back(item);
        }
    } while (FindNextFileA(hFind, &data));
    FindClose(hFind);

    std::sort(avatars.begin(), avatars.end(),
        [](const AvatarItem& a, const AvatarItem& b) { return a.name < b.name; });
}

void GameApp::startMusic()
{
    if (music.openFromFile("assets/audio/nhac_nen.wav"))
    {
        music.setLoop(true);
        music.setVolume(musicVolume);
        music.play();
    }
}

void GameApp::applyVolumes()
{
    musicVolume = clampFloat(musicVolume, 0.0f, 100.0f);
    sfxVolume = clampFloat(sfxVolume, 0.0f, 100.0f);
    music.setVolume(musicVolume);
    chessSound.setVolume(sfxVolume);
    killSound.setVolume(sfxVolume);
}

void GameApp::handleEvent(const sf::Event& event)
{
    if (event.type == sf::Event::Closed)
    {
        window.close();
        return;
    }
    if (event.type == sf::Event::MouseMoved)
    {
        mousePos = window.mapPixelToCoords(sf::Vector2i(event.mouseMove.x, event.mouseMove.y));
        if (!draggingSlider.empty())
        {
            for (size_t i = 0; i < sliders.size(); ++i)
            {
                if (sliders[i].id == draggingSlider)
                {
                    float value = (mousePos.x - sliders[i].track.left) / sliders[i].track.width;
                    value = clampFloat(value, 0.0f, 1.0f) * 100.0f;
                    if (draggingSlider == "music") musicVolume = value;
                    if (draggingSlider == "sfx") sfxVolume = value;
                    applyVolumes();
                }
            }
        }
    }
    if (event.type == sf::Event::MouseButtonReleased)
        draggingSlider.clear();

    if (dialogMode != DialogMode::None)
    {
        handleDialogEvent(event);
        return;
    }

    if (screen == Screen::Names && event.type == sf::Event::TextEntered)
    {
        std::string* target = 0;
        if (focusedNameField == 1) target = &player1Name;
        else if (focusedNameField == 2 && !vsBot) target = &player2Name;
        if (target && event.text.unicode >= 32 && event.text.unicode != 127
            && target->size() < 32)
            appendUtf8(*target, event.text.unicode);
        return;
    }

    if (event.type == sf::Event::KeyPressed)
        handleKeyPressed(event.key);

    if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left)
    {
        sf::Vector2f p = window.mapPixelToCoords(sf::Vector2i(event.mouseButton.x, event.mouseButton.y));
        for (size_t i = 0; i < sliders.size(); ++i)
        {
            if (sliders[i].track.contains(p))
            {
                draggingSlider = sliders[i].id;
                float value = (p.x - sliders[i].track.left) / sliders[i].track.width;
                value = clampFloat(value, 0.0f, 1.0f) * 100.0f;
                if (draggingSlider == "music") musicVolume = value;
                if (draggingSlider == "sfx") sfxVolume = value;
                applyVolumes();
                return;
            }
        }
        for (size_t i = 0; i < buttons.size(); ++i)
        {
            if (buttons[i].rect.contains(p))
            {
                handleButton(buttons[i].id);
                return;
            }
        }
        if (screen == Screen::Playing && !matchFinished)
        {
            float cell = BoardRect.width / BoardModel::Size;
            int col = static_cast<int>((p.x - BoardRect.left) / cell);
            int row = static_cast<int>((p.y - BoardRect.top) / cell);
            if (board.isInside(row, col))
            {
                board.setCursor(row, col);
                if (!vsBot || currentTurn == CellState::X)
                    makeMove(row, col);
            }
        }
        if (screen == Screen::Avatar)
        {
            for (int i = 0; i < static_cast<int>(avatars.size()); ++i)
            {
                int c = i % AvatarCols;
                int r = i / AvatarCols;
                sf::FloatRect rect(AvatarX0 + c * AvatarCellW, AvatarY0 + r * AvatarCellH,
                    AvatarCardSize.x, AvatarCardSize.y);
                if (rect.contains(p))
                {
                    selectedAvatar = i;
                    return;
                }
            }
        }
    }
}

void GameApp::handleButton(const std::string& id)
{
    if (id.compare(0, 12, "history_row_") == 0)
    {
        selectedHistory = std::atoi(id.substr(12).c_str());
        loadSelectedHistory();
        return;
    }

    if (id == "menu_new") screen = Screen::Mode;
    else if (id == "menu_load") openHistory();
    else if (id == "menu_settings") screen = Screen::Settings;
    else if (id == "menu_guide") screen = Screen::Guide;
    else if (id == "menu_info") screen = Screen::Info;
    else if (id == "menu_quit") window.close();
    else if (id == "back")
    {
        if (screen == Screen::History)
        {
            screen = previousScreen;
            historyMessage.clear();
        }
        else if (screen == Screen::Avatar) screen = Screen::Names;
        else if (screen == Screen::Fate) screen = Screen::Avatar;
        else screen = Screen::Menu;
    }
    else if (id == "mode_two") beginModeSelect(false, BotLevel::Easy);
    else if (id == "mode_easy") beginModeSelect(true, BotLevel::Easy);
    else if (id == "mode_medium") beginModeSelect(true, BotLevel::Medium);
    else if (id == "mode_hard") beginModeSelect(true, BotLevel::Hard);
    else if (id == "name_p1") focusedNameField = 1;
    else if (id == "name_p2" && !vsBot) focusedNameField = 2;
    else if (id == "names_continue")
    {
        if (player1Name.empty()) player1Name = tr("player.one");
        if (vsBot) player2Name = tr("player.bot");
        else if (player2Name.empty()) player2Name = tr("player.two");
        selectingPlayer = 1;
        selectedAvatar = std::min(player1Avatar, std::max(0, static_cast<int>(avatars.size()) - 1));
        screen = Screen::Avatar;
    }
    else if (id == "avatar_confirm")
    {
        if (avatars.empty()) selectedAvatar = 0;
        if (selectingPlayer == 1)
        {
            player1Avatar = selectedAvatar;
            if (!vsBot)
            {
                selectingPlayer = 2;
                selectedAvatar = std::min(player2Avatar, std::max(0, static_cast<int>(avatars.size()) - 1));
                return;
            }
        }
        else player2Avatar = selectedAvatar;
        fateOpening = false;
        fateTimer = 0.0f;
        fateXRevealed = false;
        fateORevealed = false;
        fateScoreX = 0;
        fateScoreO = 0;
        startingTurn = CellState::X;
        screen = Screen::Fate;
    }
    else if (id == "fate_open") startFateOpening();
    else if (id == "fate_continue" && fateXRevealed && fateORevealed && !fateOpening) beginMatch();
    else if (id == "play_save") openSaveDialog();
    else if (id == "play_load") openHistory();
    else if (id == "play_undo") undoMove();
    else if (id == "play_menu") returnToMenu();
    else if (id == "history_continue") loadSelectedHistory();
    else if (id == "history_input") openLoadDialog();
    else if (id == "settings_lang")
    {
        language = (language == "vi") ? "en" : "vi";
        loadLocalization(language);
        saveSettings();
    }
    else if (id == "again") beginMatch();
    else if (id == "result_menu") returnToMenu();
}

void GameApp::handleKeyPressed(const sf::Event::KeyEvent& key)
{
    if (key.code == sf::Keyboard::Escape)
    {
        if (screen == Screen::Menu) window.close();
        else if (screen == Screen::History)
        {
            screen = previousScreen;
            historyMessage.clear();
        }
        else if (screen == Screen::Playing) returnToMenu();
        else if (screen == Screen::Result) returnToMenu();
        else screen = Screen::Menu;
        return;
    }

    if (screen == Screen::Menu)
    {
        if (key.code == sf::Keyboard::Num1) screen = Screen::Mode;
        else if (key.code == sf::Keyboard::Num2) openHistory();
        else if (key.code == sf::Keyboard::Num3) screen = Screen::Settings;
        else if (key.code == sf::Keyboard::Num4) screen = Screen::Guide;
        else if (key.code == sf::Keyboard::Num5) screen = Screen::Info;
        else if (key.code == sf::Keyboard::Num6) window.close();
        else if (key.code == sf::Keyboard::T) openHistory();
    }
    else if (screen == Screen::History)
    {
        if (historyRecords.empty()) return;
        if (key.code == sf::Keyboard::Up)
            selectedHistory = std::max(0, selectedHistory - 1);
        else if (key.code == sf::Keyboard::Down)
            selectedHistory = std::min(static_cast<int>(historyRecords.size()) - 1, selectedHistory + 1);
        else if (key.code == sf::Keyboard::Enter || key.code == sf::Keyboard::Space)
            loadSelectedHistory();
    }
    else if (screen == Screen::Mode)
    {
        if (key.code == sf::Keyboard::Num1) beginModeSelect(false, BotLevel::Easy);
        else if (key.code == sf::Keyboard::Num2) beginModeSelect(true, BotLevel::Easy);
        else if (key.code == sf::Keyboard::Num3) beginModeSelect(true, BotLevel::Medium);
        else if (key.code == sf::Keyboard::Num4) beginModeSelect(true, BotLevel::Hard);
    }
    else if (screen == Screen::Names)
    {
        if (key.code == sf::Keyboard::Tab && !vsBot) focusedNameField = focusedNameField == 1 ? 2 : 1;
        else if (key.code == sf::Keyboard::Tab && vsBot) focusedNameField = 1;
        else if (key.code == sf::Keyboard::BackSpace)
        {
            if (focusedNameField == 1) eraseLastUtf8(player1Name);
            else if (focusedNameField == 2 && !vsBot) eraseLastUtf8(player2Name);
        }
        else if (key.code == sf::Keyboard::Enter) handleButton("names_continue");
    }
    else if (screen == Screen::Avatar)
    {
        if (avatars.empty()) return;
        if (key.code == sf::Keyboard::Left) selectedAvatar = std::max(0, selectedAvatar - 1);
        else if (key.code == sf::Keyboard::Right) selectedAvatar = std::min(static_cast<int>(avatars.size()) - 1, selectedAvatar + 1);
        else if (key.code == sf::Keyboard::Up) selectedAvatar = std::max(0, selectedAvatar - AvatarCols);
        else if (key.code == sf::Keyboard::Down) selectedAvatar = std::min(static_cast<int>(avatars.size()) - 1, selectedAvatar + AvatarCols);
        else if (key.code == sf::Keyboard::Enter || key.code == sf::Keyboard::Space) handleButton("avatar_confirm");
    }
    else if (screen == Screen::Fate)
    {
        if ((key.code == sf::Keyboard::Enter || key.code == sf::Keyboard::Space) && !fateOpening)
        {
            if (!fateXRevealed || !fateORevealed) startFateOpening();
            else beginMatch();
        }
    }
    else if (screen == Screen::Playing && !matchFinished)
    {
        if (key.control && key.code == sf::Keyboard::Z) { undoMove(); return; }
        if (key.code == sf::Keyboard::L) { openSaveDialog(); return; }
        if (key.code == sf::Keyboard::T) { openHistory(); return; }
        if (key.code == sf::Keyboard::Left || key.code == sf::Keyboard::A) board.moveCursor(0, -1);
        else if (key.code == sf::Keyboard::Right || key.code == sf::Keyboard::D) board.moveCursor(0, 1);
        else if (key.code == sf::Keyboard::Up || key.code == sf::Keyboard::W) board.moveCursor(-1, 0);
        else if (key.code == sf::Keyboard::Down || key.code == sf::Keyboard::S) board.moveCursor(1, 0);
        else if (key.code == sf::Keyboard::Enter || key.code == sf::Keyboard::Space)
        {
            if (!vsBot || currentTurn == CellState::X)
                makeMove(board.getCursorRow(), board.getCursorCol());
        }
    }
}

void GameApp::handleDialogEvent(const sf::Event& event)
{
    if (event.type == sf::Event::TextEntered
        && (dialogMode == DialogMode::Save || dialogMode == DialogMode::Load))
    {
        if (event.text.unicode >= 32 && event.text.unicode <= 126)
            dialogInput += static_cast<char>(event.text.unicode);
    }
    if (event.type == sf::Event::KeyPressed)
    {
        if (event.key.code == sf::Keyboard::Escape)
        {
            dialogMode = DialogMode::None;
            dialogInput.clear();
            dialogMessage.clear();
        }
        else if (event.key.code == sf::Keyboard::BackSpace
            && (dialogMode == DialogMode::Save || dialogMode == DialogMode::Load))
        {
            if (!dialogInput.empty()) dialogInput.erase(dialogInput.size() - 1);
        }
        else if (event.key.code == sf::Keyboard::Enter)
        {
            if (dialogMode == DialogMode::Save) acceptSaveName(false);
            else if (dialogMode == DialogMode::Load) acceptLoadName();
            else if (dialogMode == DialogMode::Overwrite) acceptSaveName(true);
        }
        else if (dialogMode == DialogMode::Overwrite)
        {
            if (event.key.code == sf::Keyboard::Y) acceptSaveName(true);
            if (event.key.code == sf::Keyboard::N)
            {
                dialogMode = DialogMode::Save;
                dialogInput = pendingSaveName;
            }
        }
    }
    if (event.type == sf::Event::MouseButtonPressed && event.mouseButton.button == sf::Mouse::Left)
    {
        sf::Vector2f p = window.mapPixelToCoords(sf::Vector2i(event.mouseButton.x, event.mouseButton.y));
        for (size_t i = 0; i < buttons.size(); ++i)
        {
            if (!buttons[i].rect.contains(p)) continue;
            if (buttons[i].id == "dialog_cancel")
            {
                dialogMode = DialogMode::None;
                dialogInput.clear();
                dialogMessage.clear();
            }
            else if (buttons[i].id == "dialog_ok")
            {
                if (dialogMode == DialogMode::Save) acceptSaveName(false);
                else if (dialogMode == DialogMode::Load) acceptLoadName();
            }
            else if (buttons[i].id == "dialog_yes") acceptSaveName(true);
            else if (buttons[i].id == "dialog_no")
            {
                dialogMode = DialogMode::Save;
                dialogInput = pendingSaveName;
            }
            return;
        }
    }
}

void GameApp::update(float dt)
{
    animationTime += dt;
    if (screen == Screen::Menu)
        menuVideo.update(dt);
    if (screen == Screen::Fate && fateOpening)
    {
        fateTimer += dt;
        if (fateTimer > 0.78f) fateXRevealed = true;
        if (fateTimer > 1.34f) fateORevealed = true;
        if (fateTimer > 2.15f) fateOpening = false;
    }
    updateParticles(dt);
    updateEffects(dt);

    if (screen == Screen::Playing && vsBot && currentTurn == CellState::O
        && !matchFinished && botThinking)
    {
        botThinkTimer += dt;
        if (botThinkTimer > 0.45f)
            performBotMove();
    }
}

void GameApp::draw()
{
    clearUi();
    if (screen == Screen::Menu) drawMenu();
    else if (screen == Screen::Mode) drawMode();
    else if (screen == Screen::Names) drawNames();
    else if (screen == Screen::Avatar) drawAvatar();
    else if (screen == Screen::Fate) drawFate();
    else if (screen == Screen::Playing) drawPlaying();
    else if (screen == Screen::History) drawHistory();
    else if (screen == Screen::Settings) drawSettings();
    else if (screen == Screen::Guide) drawGuide();
    else if (screen == Screen::Info) drawInfo();
    else if (screen == Screen::Result) drawResult();
    if (dialogMode != DialogMode::None) drawDialog();
    window.display();
}

void GameApp::drawBackground(const std::string& key)
{
    window.clear(sf::Color(245, 250, 255));
    if (key == "menu" && menuVideo.isReady())
    {
        menuVideo.drawCover(window, sf::FloatRect(0, 0, static_cast<float>(WindowW), static_cast<float>(WindowH)));
        window.resetGLStates();
    }
    else if (backgrounds.find(key) != backgrounds.end())
        drawSpriteCover(window, backgrounds[key], sf::FloatRect(0, 0, static_cast<float>(WindowW), static_cast<float>(WindowH)));
    else
    {
        sf::VertexArray quad(sf::Quads, 4);
        quad[0].position = sf::Vector2f(0, 0);
        quad[1].position = sf::Vector2f(static_cast<float>(WindowW), 0);
        quad[2].position = sf::Vector2f(static_cast<float>(WindowW), static_cast<float>(WindowH));
        quad[3].position = sf::Vector2f(0, static_cast<float>(WindowH));
        quad[0].color = sf::Color(230, 248, 255);
        quad[1].color = sf::Color(255, 248, 218);
        quad[2].color = sf::Color(255, 255, 242);
        quad[3].color = sf::Color(226, 245, 240);
        window.draw(quad);
    }
    sf::RectangleShape veil(sf::Vector2f(static_cast<float>(WindowW), static_cast<float>(WindowH)));
    veil.setFillColor(sf::Color(255, 255, 255, key == "menu" ? 18 : 12));
    window.draw(veil);
    drawParticles(1.0f);
}

void GameApp::drawMenu()
{
    drawBackground("menu");
    sf::FloatRect panel(500, 120, 440, 586);
    sf::RectangleShape panelShadow(sf::Vector2f(panel.width, panel.height));
    panelShadow.setPosition(panel.left + 8.0f, panel.top + 10.0f);
    panelShadow.setFillColor(sf::Color(12, 10, 18, 74));
    window.draw(panelShadow);

    sf::VertexArray wash(sf::Quads, 4);
    wash[0].position = sf::Vector2f(panel.left, panel.top);
    wash[1].position = sf::Vector2f(panel.left + panel.width, panel.top);
    wash[2].position = sf::Vector2f(panel.left + panel.width, panel.top + panel.height);
    wash[3].position = sf::Vector2f(panel.left, panel.top + panel.height);
    wash[0].color = sf::Color(255, 245, 199, 88);
    wash[1].color = sf::Color(255, 218, 169, 76);
    wash[2].color = sf::Color(80, 44, 50, 42);
    wash[3].color = sf::Color(235, 236, 215, 76);
    window.draw(wash);

    sf::RectangleShape box(sf::Vector2f(panel.width, panel.height));
    box.setPosition(panel.left, panel.top);
    box.setFillColor(sf::Color::Transparent);
    box.setOutlineThickness(3.0f);
    box.setOutlineColor(sf::Color(255, 221, 145, 176));
    window.draw(box);

    for (int i = 0; i < 6; ++i)
    {
        sf::RectangleShape shelf(sf::Vector2f(panel.width - 74.0f, 2.0f));
        shelf.setPosition(panel.left + 37.0f, 275.0f + i * 72.0f);
        shelf.setFillColor(i % 2 == 0 ? sf::Color(255, 244, 188, 70) : sf::Color(77, 220, 230, 42));
        window.draw(shelf, sf::BlendAdd);
    }

    drawText(tr("title"), 58, sf::Vector2f(WindowW * 0.5f, 166.0f),
        sf::Color(239, 235, 216), true);
    float x = 540.0f;
    float y = 244.0f;
    float w = 360.0f;
    float h = 56.0f;
    float gap = 72.0f;
    addButton("menu_new", tr("menu.new"), sf::FloatRect(x, y + gap * 0, w, h));
    addButton("menu_load", tr("menu.load"), sf::FloatRect(x, y + gap * 1, w, h));
    addButton("menu_settings", tr("menu.settings"), sf::FloatRect(x, y + gap * 2, w, h));
    addButton("menu_guide", tr("menu.guide"), sf::FloatRect(x, y + gap * 3, w, h));
    addButton("menu_info", tr("menu.info"), sf::FloatRect(x, y + gap * 4, w, h));
    addButton("menu_quit", tr("menu.quit"), sf::FloatRect(x, y + gap * 5, w, h));
    drawButtons();
}

void GameApp::drawMode()
{
    drawBackground("mode");
    drawText(tr("mode.title"), 54, sf::Vector2f(WindowW * 0.5f, 110.0f),
        sf::Color(58, 95, 85), true);
    sf::FloatRect panel(450, 190, 540, 370);
    sf::RectangleShape shadow(sf::Vector2f(panel.width, panel.height));
    shadow.setPosition(panel.left + 8.0f, panel.top + 10.0f);
    shadow.setFillColor(sf::Color(18, 12, 26, 76));
    window.draw(shadow);

    sf::VertexArray wash(sf::Quads, 4);
    wash[0].position = sf::Vector2f(panel.left, panel.top);
    wash[1].position = sf::Vector2f(panel.left + panel.width, panel.top);
    wash[2].position = sf::Vector2f(panel.left + panel.width, panel.top + panel.height);
    wash[3].position = sf::Vector2f(panel.left, panel.top + panel.height);
    wash[0].color = sf::Color(255, 233, 174, 90);
    wash[1].color = sf::Color(244, 95, 120, 54);
    wash[2].color = sf::Color(82, 42, 68, 58);
    wash[3].color = sf::Color(255, 242, 197, 78);
    window.draw(wash);

    sf::RectangleShape box(sf::Vector2f(panel.width, panel.height));
    box.setPosition(panel.left, panel.top);
    box.setFillColor(sf::Color::Transparent);
    box.setOutlineThickness(3.0f);
    box.setOutlineColor(sf::Color(255, 219, 131, 185));
    window.draw(box);

    sf::CircleShape seal(176.0f, 96);
    seal.setOrigin(176.0f, 176.0f);
    seal.setPosition(panel.left + panel.width * 0.5f, panel.top + panel.height * 0.5f);
    seal.setFillColor(sf::Color::Transparent);
    seal.setOutlineThickness(2.0f);
    seal.setOutlineColor(sf::Color(255, 214, 112, 58));
    window.draw(seal, sf::BlendAdd);
    addButton("mode_two", "1. " + tr("mode.two"), sf::FloatRect(500, 228, 440, 58));
    addButton("mode_easy", "2. " + tr("mode.bot.easy"), sf::FloatRect(500, 310, 440, 58));
    addButton("mode_medium", "3. " + tr("mode.bot.medium"), sf::FloatRect(500, 392, 440, 58));
    addButton("mode_hard", "4. " + tr("mode.bot.hard"), sf::FloatRect(500, 474, 440, 58));
    addButton("back", tr("back"), sf::FloatRect(32, 30, 160, 48));
    drawButtons();
}

void GameApp::drawNames()
{
    drawBackground("mode");
    drawText("Nhập đạo danh", 54, sf::Vector2f(WindowW * 0.5f, 104.0f),
        sf::Color(246, 238, 218), true);

    sf::FloatRect panel(410, 178, 620, 420);
    sf::RectangleShape box(sf::Vector2f(panel.width, panel.height));
    box.setPosition(panel.left, panel.top);
    box.setFillColor(sf::Color(28, 28, 42, 126));
    box.setOutlineThickness(4.0f);
    box.setOutlineColor(sf::Color(255, 210, 112, 185));
    window.draw(box);

    drawText("Đạo hữu cầm X", 28, sf::Vector2f(panel.left + 70, panel.top + 72),
        sf::Color(255, 242, 196), false);
    std::string p1 = player1Name.empty() ? "Đạo Hữu 1" : player1Name;
    if (focusedNameField == 1 && (static_cast<int>(animationTime * 2.0f) % 2 == 0)) p1 += "_";
    addButton("name_p1", p1, sf::FloatRect(panel.left + 70, panel.top + 108, 480, 58));

    drawText(vsBot ? "Đối thủ" : "Đạo hữu cầm O", 28,
        sf::Vector2f(panel.left + 70, panel.top + 196), sf::Color(255, 242, 196), false);
    if (vsBot)
    {
        sf::FloatRect botField(panel.left + 70, panel.top + 232, 480, 58);
        sf::RectangleShape disabled(sf::Vector2f(botField.width, botField.height));
        disabled.setPosition(botField.left, botField.top);
        disabled.setFillColor(sf::Color(236, 221, 179, 192));
        disabled.setOutlineColor(sf::Color(126, 87, 51, 165));
        disabled.setOutlineThickness(3.0f);
        window.draw(disabled);
        drawTextBox(tr("player.bot"), 26, botField, sf::Color(67, 51, 45));
    }
    else
    {
        std::string p2 = player2Name.empty() ? "Đạo Hữu 2" : player2Name;
        if (focusedNameField == 2 && (static_cast<int>(animationTime * 2.0f) % 2 == 0)) p2 += "_";
        addButton("name_p2", p2, sf::FloatRect(panel.left + 70, panel.top + 232, 480, 58));
    }

    drawText("Click vào ô để nhập tên, Tab để đổi ô, Enter để tiếp tục", 22,
        sf::Vector2f(panel.left + panel.width * 0.5f, panel.top + 326),
        sf::Color(226, 240, 241), true);
    addButton("names_continue", "Tiếp tục", sf::FloatRect(panel.left + 200, panel.top + 350, 220, 56));
    addButton("back", tr("back"), sf::FloatRect(32, 30, 160, 48));
    drawButtons();
}

void GameApp::drawAvatar()
{
    drawBackground("avatar");
    std::string player = (selectingPlayer == 1) ? player1Name : player2Name;
    drawText(tr("avatar.title") + " - " + player, 48,
        sf::Vector2f(WindowW * 0.5f, 78.0f), sf::Color(67, 73, 118), true);

    sf::FloatRect gridPanel(78, 136, 778, 582);
    sf::RectangleShape bg(sf::Vector2f(gridPanel.width, gridPanel.height));
    bg.setPosition(gridPanel.left, gridPanel.top);
    bg.setFillColor(sf::Color(255, 244, 214, 142));
    bg.setOutlineColor(sf::Color(123, 78, 134, 170));
    bg.setOutlineThickness(3.0f);
    window.draw(bg);

    sf::RectangleShape gridShine(sf::Vector2f(gridPanel.width - 26, 38));
    gridShine.setPosition(gridPanel.left + 13, gridPanel.top + 12);
    gridShine.setFillColor(sf::Color(255, 255, 245, 74));
    window.draw(gridShine);

    for (int i = 0; i < static_cast<int>(avatars.size()); ++i)
    {
        int c = i % AvatarCols;
        int r = i / AvatarCols;
        sf::FloatRect rect(AvatarX0 + c * AvatarCellW, AvatarY0 + r * AvatarCellH,
            AvatarCardSize.x, AvatarCardSize.y);
        bool selected = i == selectedAvatar;
        float bob = selected ? std::sin(animationTime * 5.0f) * 6.0f : 0.0f;
        rect.top += bob;
        if (selected)
        {
            sf::CircleShape aura(76.0f + std::sin(animationTime * 4.0f) * 7.0f);
            aura.setOrigin(aura.getRadius(), aura.getRadius());
            aura.setPosition(rect.left + rect.width * 0.5f, rect.top + rect.height * 0.42f);
            aura.setFillColor(sf::Color(116, 224, 240, 72));
            aura.setOutlineThickness(4.0f);
            aura.setOutlineColor(sf::Color(255, 206, 83, 170));
            window.draw(aura);

            for (int k = 0; k < 8; ++k)
            {
                float a = animationTime * 2.5f + k * 0.785398f;
                sf::CircleShape dot(4.0f, 12);
                dot.setOrigin(4.0f, 4.0f);
                dot.setPosition(rect.left + rect.width * 0.5f + std::cos(a) * 72.0f,
                    rect.top + rect.height * 0.42f + std::sin(a) * 52.0f);
                dot.setFillColor(k % 2 == 0 ? sf::Color(255, 238, 118, 190) : sf::Color(77, 220, 226, 178));
                window.draw(dot);
            }
        }
        sf::RectangleShape card(sf::Vector2f(rect.width, rect.height));
        card.setPosition(rect.left, rect.top);
        card.setFillColor(selected ? sf::Color(255, 232, 157, 236) : sf::Color(239, 250, 235, 218));
        card.setOutlineThickness(selected ? 5.0f : 2.0f);
        card.setOutlineColor(selected ? sf::Color(226, 126, 39) : sf::Color(89, 159, 168));
        window.draw(card);

        sf::RectangleShape imagePlate(sf::Vector2f(rect.width - 14, rect.height - 34));
        imagePlate.setPosition(rect.left + 7, rect.top + 7);
        imagePlate.setFillColor(sf::Color(255, 255, 247, 126));
        imagePlate.setOutlineThickness(1.5f);
        imagePlate.setOutlineColor(sf::Color(112, 73, 132, selected ? 160 : 86));
        window.draw(imagePlate);

        sf::RectangleShape seal(sf::Vector2f(rect.width - 14, 5));
        seal.setPosition(rect.left + 7, rect.top + 7);
        seal.setFillColor(selected ? sf::Color(69, 202, 212, 205) : sf::Color(180, 132, 56, 132));
        window.draw(seal);
        drawSpriteContain(window, avatars[i].texture,
            sf::FloatRect(rect.left + 10, rect.top + 10, rect.width - 20, rect.height - 42));
        drawTextBox(avatars[i].name, 18, sf::FloatRect(rect.left + 6, rect.top + rect.height - 29, rect.width - 12, 24),
            sf::Color(63, 75, 82));
    }

    sf::FloatRect preview(902, 150, 410, 530);
    sf::RectangleShape previewBox(sf::Vector2f(preview.width, preview.height));
    previewBox.setPosition(preview.left, preview.top);
    previewBox.setFillColor(sf::Color(245, 252, 237, 178));
    previewBox.setOutlineThickness(4.0f);
    previewBox.setOutlineColor(sf::Color(205, 145, 53, 178));
    window.draw(previewBox);

    sf::CircleShape previewAura(164.0f + std::sin(animationTime * 3.0f) * 8.0f);
    previewAura.setOrigin(previewAura.getRadius(), previewAura.getRadius());
    previewAura.setPosition(preview.left + preview.width * 0.5f, preview.top + 206.0f);
    previewAura.setFillColor(sf::Color(113, 224, 238, 58));
    previewAura.setOutlineThickness(5.0f);
    previewAura.setOutlineColor(sf::Color(255, 218, 91, 155));
    window.draw(previewAura);

    for (int k = 0; k < 12; ++k)
    {
        float a = animationTime * 1.6f + k * 0.523598f;
        sf::RectangleShape rune(sf::Vector2f(34.0f, 3.0f));
        rune.setOrigin(17.0f, 1.5f);
        rune.setPosition(preview.left + preview.width * 0.5f + std::cos(a) * 174.0f,
            preview.top + 206.0f + std::sin(a) * 174.0f);
        rune.setRotation(a * 57.2958f + 90.0f);
        rune.setFillColor(k % 2 == 0 ? sf::Color(91, 198, 208, 150) : sf::Color(219, 138, 47, 150));
        window.draw(rune);
    }

    if (!avatars.empty())
    {
        int index = std::max(0, std::min(selectedAvatar, static_cast<int>(avatars.size()) - 1));
        drawSpriteContain(window, avatars[index].texture,
            sf::FloatRect(preview.left + 58, preview.top + 48, preview.width - 116, 316));
        drawTextBox(avatars[index].name, 28,
            sf::FloatRect(preview.left + 40, preview.top + 386, preview.width - 80, 46),
            sf::Color(65, 65, 88));
        drawTextBox(selectingPlayer == 1 ? "X - Kiếm khí" : "O - Linh hoàn", 24,
            sf::FloatRect(preview.left + 50, preview.top + 438, preview.width - 100, 38),
            selectingPlayer == 1 ? sf::Color(32, 136, 164) : sf::Color(177, 101, 28));
    }

    addButton("avatar_confirm", tr("avatar.confirm"), sf::FloatRect(940, 704, 336, 56));
    addButton("back", tr("back"), sf::FloatRect(32, 30, 160, 48));
    drawButtons();
}

void GameApp::drawFate()
{
    drawBackground("mode");
    drawText("Khai mở khí vận", 54, sf::Vector2f(WindowW * 0.5f, 88.0f),
        sf::Color(255, 242, 210), true);

    sf::FloatRect altar(270, 148, 900, 520);
    sf::VertexArray altarWash(sf::Quads, 4);
    altarWash[0].position = sf::Vector2f(altar.left, altar.top);
    altarWash[1].position = sf::Vector2f(altar.left + altar.width, altar.top);
    altarWash[2].position = sf::Vector2f(altar.left + altar.width, altar.top + altar.height);
    altarWash[3].position = sf::Vector2f(altar.left, altar.top + altar.height);
    altarWash[0].color = sf::Color(20, 24, 46, 178);
    altarWash[1].color = sf::Color(24, 38, 54, 170);
    altarWash[2].color = sf::Color(72, 54, 44, 132);
    altarWash[3].color = sf::Color(28, 32, 54, 150);
    window.draw(altarWash);

    sf::RectangleShape panel(sf::Vector2f(altar.width, altar.height));
    panel.setPosition(altar.left, altar.top);
    panel.setFillColor(sf::Color::Transparent);
    panel.setOutlineColor(sf::Color(255, 218, 122, 190));
    panel.setOutlineThickness(4.0f);
    window.draw(panel);

    sf::RectangleShape topGlow(sf::Vector2f(altar.width - 42.0f, 4.0f));
    topGlow.setPosition(altar.left + 21.0f, altar.top + 18.0f);
    topGlow.setFillColor(sf::Color(255, 240, 170, 122));
    window.draw(topGlow, sf::BlendAdd);
    topGlow.setPosition(altar.left + 21.0f, altar.top + altar.height - 24.0f);
    topGlow.setFillColor(sf::Color(61, 221, 232, 84));
    window.draw(topGlow, sf::BlendAdd);

    sf::FloatRect leftCard(356, 234, 260, 310);
    sf::FloatRect rightCard(824, 234, 260, 310);
    sf::Vector2f center(altar.left + altar.width * 0.5f, altar.top + 246.0f);
    for (int i = 0; i < 5; ++i)
    {
        sf::CircleShape ring(92.0f + i * 38.0f + std::sin(animationTime * 2.0f + i) * 4.0f, 96);
        ring.setOrigin(ring.getRadius(), ring.getRadius());
        ring.setPosition(center);
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineThickness(i == 0 ? 4.0f : 2.0f);
        ring.setOutlineColor(i % 2 == 0 ? sf::Color(255, 209, 86, 95) : sf::Color(68, 218, 232, 88));
        window.draw(ring);
    }

    auto drawCardGround = [&](const sf::FloatRect& rect, sf::Color accent, float phase)
    {
        sf::Vector2f cardCenter(rect.left + rect.width * 0.5f, rect.top + rect.height * 0.5f);
        float floorY = rect.top + rect.height + 24.0f;

        sf::CircleShape shadow(112.0f, 96);
        shadow.setOrigin(112.0f, 112.0f);
        shadow.setPosition(cardCenter.x + 8.0f, floorY + 18.0f);
        shadow.setScale(1.72f, 0.30f);
        shadow.setFillColor(sf::Color(7, 9, 18, 118));
        window.draw(shadow);

        sf::CircleShape aura(118.0f + std::sin(animationTime * 3.4f + phase) * 5.0f, 96);
        aura.setOrigin(aura.getRadius(), aura.getRadius());
        aura.setPosition(cardCenter.x, floorY);
        aura.setScale(1.44f, 0.34f);
        aura.setFillColor(sf::Color(accent.r, accent.g, accent.b, 38));
        aura.setOutlineThickness(3.0f);
        aura.setOutlineColor(sf::Color(accent.r, accent.g, accent.b, 120));
        window.draw(aura, sf::BlendAdd);

        sf::ConvexShape plinth(6);
        plinth.setPoint(0, sf::Vector2f(rect.left - 46.0f, rect.top + rect.height - 20.0f));
        plinth.setPoint(1, sf::Vector2f(rect.left + rect.width + 46.0f, rect.top + rect.height - 20.0f));
        plinth.setPoint(2, sf::Vector2f(rect.left + rect.width + 72.0f, rect.top + rect.height + 34.0f));
        plinth.setPoint(3, sf::Vector2f(rect.left + rect.width + 46.0f, rect.top + rect.height + 68.0f));
        plinth.setPoint(4, sf::Vector2f(rect.left - 46.0f, rect.top + rect.height + 68.0f));
        plinth.setPoint(5, sf::Vector2f(rect.left - 72.0f, rect.top + rect.height + 34.0f));
        plinth.setFillColor(sf::Color(40, 48, 62, 178));
        plinth.setOutlineThickness(3.0f);
        plinth.setOutlineColor(sf::Color(accent.r, accent.g, accent.b, 142));
        window.draw(plinth);

        sf::RectangleShape jade(sf::Vector2f(rect.width + 116.0f, 18.0f));
        jade.setPosition(rect.left - 58.0f, rect.top + rect.height + 4.0f);
        jade.setFillColor(sf::Color(255, 236, 164, 118));
        window.draw(jade, sf::BlendAdd);
        jade.setPosition(rect.left - 42.0f, rect.top + rect.height + 42.0f);
        jade.setSize(sf::Vector2f(rect.width + 84.0f, 4.0f));
        jade.setFillColor(sf::Color(accent.r, accent.g, accent.b, 150));
        window.draw(jade, sf::BlendAdd);

        for (int i = 0; i < 12; ++i)
        {
            float a = animationTime * 1.7f + phase + i * 0.523598f;
            sf::CircleShape rune(5.0f + (i % 3), i % 2 == 0 ? 4 : 6);
            rune.setOrigin(rune.getRadius(), rune.getRadius());
            rune.setPosition(cardCenter.x + std::cos(a) * (116.0f + (i % 2) * 24.0f),
                floorY + std::sin(a) * 30.0f);
            rune.setRotation(a * 57.2958f + i * 21.0f);
            rune.setFillColor(i % 2 == 0 ? sf::Color(255, 235, 144, 135)
                : sf::Color(accent.r, accent.g, accent.b, 145));
            window.draw(rune, sf::BlendAdd);
        }

        for (int i = 0; i < 5; ++i)
        {
            float sway = std::sin(animationTime * 2.0f + phase + i) * 10.0f;
            sf::RectangleShape beam(sf::Vector2f(3.0f + (i % 2), rect.height * 0.70f));
            beam.setOrigin(beam.getSize().x * 0.5f, beam.getSize().y);
            beam.setPosition(rect.left + 40.0f + i * 45.0f + sway, rect.top + rect.height + 6.0f);
            beam.setRotation(-8.0f + i * 4.0f);
            beam.setFillColor(sf::Color(accent.r, accent.g, accent.b, 44));
            window.draw(beam, sf::BlendAdd);
        }
    };

    drawCardGround(leftCard, sf::Color(57, 213, 232), 0.0f);
    drawCardGround(rightCard, sf::Color(255, 190, 70), 2.3f);

    float p1Progress = fateXRevealed ? 1.0f : clampFloat((fateTimer - 0.18f) / 0.62f, 0.0f, 1.0f);
    float p2Progress = fateORevealed ? 1.0f : clampFloat((fateTimer - 0.78f) / 0.62f, 0.0f, 1.0f);
    drawFateCard(leftCard, player1Name, fateScoreX, fateXRevealed, p1Progress, sf::Color(57, 213, 232));
    drawFateCard(rightCard, player2Name, fateScoreO, fateORevealed, p2Progress, sf::Color(255, 190, 70));

    if (fateXRevealed && fateORevealed)
    {
        std::string starter = startingTurn == CellState::X ? player1Name : player2Name;
        drawText("Thiên cơ chọn: " + starter + " đi trước", 30,
            sf::Vector2f(WindowW * 0.5f, 596.0f), sf::Color(255, 242, 198), true);
        if (!fateOpening)
            addButton("fate_continue", "Nhập cuộc", sf::FloatRect(610, 692, 220, 56));
    }
    else
    {
        drawText(vsBot ? "Tâm Ma sẽ tự mở thẻ khí vận" : "Mỗi đạo hữu mở một thẻ từ 0 đến 999",
            26, sf::Vector2f(WindowW * 0.5f, 596.0f), sf::Color(225, 242, 244), true);
        if (!fateOpening)
            addButton("fate_open", "Khai mở khí vận", sf::FloatRect(560, 692, 320, 56));
    }

    addButton("back", tr("back"), sf::FloatRect(32, 30, 160, 48));
    drawButtons();
}

void GameApp::drawPlaying()
{
    drawBackground(vsBot ? "game_bot" : "game_two");
    drawBoard();

    sf::RectangleShape side(sf::Vector2f(610.0f, 650.0f));
    side.setPosition(780.0f, 92.0f);
    side.setFillColor(sf::Color(255, 240, 188, 160));
    side.setOutlineThickness(2.0f);
    side.setOutlineColor(sf::Color(112, 76, 35, 150));
    window.draw(side);

    for (int i = 0; i < 3; ++i)
    {
        sf::CircleShape halo(70.0f + i * 34.0f + std::sin(animationTime * 2.0f + i) * 3.0f, 80);
        halo.setOrigin(halo.getRadius(), halo.getRadius());
        halo.setPosition(1085.0f, 348.0f);
        halo.setFillColor(sf::Color::Transparent);
        halo.setOutlineThickness(2.0f);
        halo.setOutlineColor(i % 2 == 0 ? sf::Color(255, 211, 95, 72) : sf::Color(72, 211, 228, 70));
        window.draw(halo);
    }
    for (int i = 0; i < 10; ++i)
    {
        float a = animationTime * 1.8f + i * 0.628318f;
        sf::CircleShape spark(3.0f + (i % 3), 12);
        spark.setOrigin(spark.getRadius(), spark.getRadius());
        spark.setPosition(1085.0f + std::cos(a) * 148.0f, 348.0f + std::sin(a) * 60.0f);
        spark.setFillColor(i % 2 == 0 ? sf::Color(255, 224, 88, 140) : sf::Color(75, 219, 232, 130));
        window.draw(spark);
    }

    drawPlayerPanel(810.0f, 116.0f, player1Name, CellState::X, player1Avatar,
        currentTurn == CellState::X, false);
    drawPlayerPanel(1110.0f, 116.0f, player2Name, CellState::O, player2Avatar,
        currentTurn == CellState::O, vsBot);

    std::ostringstream timer;
    int elapsed = elapsedSeconds();
    timer << tr("timer") << ": " << std::setw(2) << std::setfill('0') << (elapsed / 60)
        << ":" << std::setw(2) << (elapsed % 60);
    drawText(timer.str(), 28, sf::Vector2f(1085.0f, 430.0f), sf::Color(66, 82, 90), true);
    drawText(tr("turn") + ": " + (currentTurn == CellState::X ? player1Name : player2Name),
        26, sf::Vector2f(1085.0f, 472.0f), sf::Color(66, 82, 90), true);
    const std::vector<BoardMove>& moves = board.getMoves();
    if (!moves.empty())
    {
        const BoardMove& last = moves.back();
        std::string mark = last.mark == CellState::X ? "X" : "O";
        drawText(tr("last.move") + ": " + mark + " - " + cellName(last.row, last.col),
            23, sf::Vector2f(1085.0f, 512.0f), sf::Color(168, 89, 45), true);
        drawText(tr("move.count") + ": " + std::to_string(static_cast<int>(moves.size())),
            21, sf::Vector2f(1085.0f, 538.0f), sf::Color(66, 82, 90), true);
    }
    if (botThinking)
        drawText(tr("playing.botThinking"), 20, sf::Vector2f(1085.0f, 562.0f), sf::Color(128, 74, 146), true);

    addButton("play_save", tr("playing.save"), sf::FloatRect(820, 586, 250, 46));
    addButton("play_load", tr("playing.load"), sf::FloatRect(1100, 586, 250, 46));
    addButton("play_undo", tr("playing.undo"), sf::FloatRect(820, 646, 530, 48));
    addButton("play_menu", tr("playing.menu"), sf::FloatRect(960, 704, 250, 42));
    drawButtons();
}

void GameApp::drawHistory()
{
    drawBackground("history");
    drawText(tr("history.title"), 54, sf::Vector2f(WindowW * 0.5f, 82.0f),
        sf::Color(72, 78, 118), true);

    sf::FloatRect panel(120, 132, 1200, 548);
    sf::RectangleShape shadow(sf::Vector2f(panel.width, panel.height));
    shadow.setPosition(panel.left + 8.0f, panel.top + 10.0f);
    shadow.setFillColor(sf::Color(8, 12, 28, 78));
    window.draw(shadow);

    sf::VertexArray wash(sf::Quads, 4);
    wash[0].position = sf::Vector2f(panel.left, panel.top);
    wash[1].position = sf::Vector2f(panel.left + panel.width, panel.top);
    wash[2].position = sf::Vector2f(panel.left + panel.width, panel.top + panel.height);
    wash[3].position = sf::Vector2f(panel.left, panel.top + panel.height);
    wash[0].color = sf::Color(255, 255, 255, 206);
    wash[1].color = sf::Color(226, 241, 255, 188);
    wash[2].color = sf::Color(255, 225, 238, 170);
    wash[3].color = sf::Color(245, 249, 255, 192);
    window.draw(wash);

    sf::RectangleShape box(sf::Vector2f(panel.width, panel.height));
    box.setPosition(panel.left, panel.top);
    box.setFillColor(sf::Color::Transparent);
    box.setOutlineColor(sf::Color(156, 183, 226, 210));
    box.setOutlineThickness(3.0f);
    window.draw(box);

    sf::RectangleShape headerGlow(sf::Vector2f(panel.width - 66.0f, 4.0f));
    headerGlow.setPosition(panel.left + 33.0f, panel.top + 60.0f);
    headerGlow.setFillColor(sf::Color(255, 216, 113, 100));
    window.draw(headerGlow, sf::BlendAdd);

    if (historyRecords.empty())
    {
        drawTextBox(tr("history.empty"), 32,
            sf::FloatRect(panel.left + 80, panel.top + 190, panel.width - 160, 80),
            sf::Color(69, 83, 99));
    }
    else
    {
        drawTextBox(tr("history.file"), 21,
            sf::FloatRect(panel.left + 34, panel.top + 24, 240, 30), sf::Color(56, 75, 99));
        drawTextBox(tr("history.players"), 21,
            sf::FloatRect(panel.left + 294, panel.top + 24, 360, 30), sf::Color(56, 75, 99));
        drawTextBox(tr("history.date"), 21,
            sf::FloatRect(panel.left + 674, panel.top + 24, 210, 30), sf::Color(56, 75, 99));
        drawTextBox(tr("history.status"), 21,
            sf::FloatRect(panel.left + 904, panel.top + 24, 190, 30), sf::Color(56, 75, 99));

        int visible = 7;
        int count = static_cast<int>(historyRecords.size());
        if (selectedHistory < 0) selectedHistory = 0;
        if (selectedHistory >= count) selectedHistory = count - 1;
        int start = selectedHistory - visible / 2;
        start = std::max(0, std::min(start, std::max(0, count - visible)));

        for (int row = 0; row < visible && start + row < count; ++row)
        {
            int index = start + row;
            const SaveRecord& rec = historyRecords[index];
            bool selected = index == selectedHistory;
            bool playable = rec.status == "playing";
            sf::FloatRect r(panel.left + 30, panel.top + 66 + row * 62, panel.width - 60, 52);

            sf::RectangleShape rowShadow(sf::Vector2f(r.width - 16.0f, r.height * 0.45f));
            rowShadow.setPosition(r.left + 8.0f, r.top + r.height * 0.60f);
            rowShadow.setFillColor(sf::Color(18, 17, 24, selected ? 76 : 38));
            window.draw(rowShadow);

            if (hasCardTexture)
            {
                const sf::IntRect source(74, 106, 2361, 415);
                sf::Sprite rowCard(cardTexture);
                rowCard.setTextureRect(source);
                rowCard.setPosition(r.left, r.top);
                rowCard.setScale(r.width / static_cast<float>(source.width),
                    r.height / static_cast<float>(source.height));
                rowCard.setColor(selected ? sf::Color(255, 250, 216, 248)
                    : (playable ? sf::Color(229, 255, 232, 224) : sf::Color(238, 242, 248, 210)));
                window.draw(rowCard);
            }
            else
            {
                sf::RectangleShape rowBox(sf::Vector2f(r.width, r.height));
                rowBox.setPosition(r.left, r.top);
                rowBox.setFillColor(selected ? sf::Color(255, 244, 203, 236)
                    : (playable ? sf::Color(236, 255, 238, 214) : sf::Color(241, 244, 247, 202)));
                window.draw(rowBox);
            }

            sf::RectangleShape rowLine(sf::Vector2f(r.width - 86.0f, selected ? 3.0f : 2.0f));
            rowLine.setPosition(r.left + 43.0f, r.top + r.height - 9.0f);
            rowLine.setFillColor(selected ? sf::Color(224, 153, 54, 168)
                : (playable ? sf::Color(88, 189, 139, 118) : sf::Color(135, 153, 182, 92)));
            window.draw(rowLine, sf::BlendAdd);

            std::string statusLabel = tr("history.finished");
            if (rec.status == "playing") statusLabel = tr("history.playing");
            else if (rec.status == "win") statusLabel = tr("result.win");
            else if (rec.status == "lose") statusLabel = tr("result.lose");
            else if (rec.status == "draw") statusLabel = tr("result.draw");

            drawTextBox(rec.fileName, 18,
                sf::FloatRect(r.left + 12, r.top + 8, 250, 36), sf::Color(45, 68, 86));
            drawTextBox(rec.player1Name + " vs " + rec.player2Name, 18,
                sf::FloatRect(r.left + 278, r.top + 8, 370, 36), sf::Color(45, 68, 86));
            drawTextBox(rec.dateSaved, 18,
                sf::FloatRect(r.left + 668, r.top + 8, 210, 36), sf::Color(45, 68, 86));
            drawTextBox(statusLabel, 18,
                sf::FloatRect(r.left + 910, r.top + 8, 150, 36),
                playable ? sf::Color(33, 126, 83) : sf::Color(117, 123, 133));
            addButton("history_row_" + std::to_string(index), "", r);
        }
    }

    if (!historyMessage.empty())
        drawText(historyMessage, 23, sf::Vector2f(WindowW * 0.5f, 704.0f),
            sf::Color(176, 72, 68), true);

    if (!historyRecords.empty())
        addButton("history_continue", tr("history.continue"), sf::FloatRect(458, 720, 240, 50));
    addButton("history_input", tr("history.input"), sf::FloatRect(742, 720, 240, 50));
    addButton("back", tr("back"), sf::FloatRect(32, 30, 160, 48));
    drawButtons();
}

void GameApp::drawSettings()
{
    drawBackground("settings");
    drawText(tr("menu.settings"), 54, sf::Vector2f(WindowW * 0.5f, 105.0f),
        sf::Color(255, 242, 210), true);
    sf::FloatRect panel(420, 168, 600, 430);
    sf::RectangleShape shadow(sf::Vector2f(panel.width, panel.height));
    shadow.setPosition(panel.left + 8.0f, panel.top + 10.0f);
    shadow.setFillColor(sf::Color(6, 12, 28, 88));
    window.draw(shadow);

    sf::VertexArray wash(sf::Quads, 4);
    wash[0].position = sf::Vector2f(panel.left, panel.top);
    wash[1].position = sf::Vector2f(panel.left + panel.width, panel.top);
    wash[2].position = sf::Vector2f(panel.left + panel.width, panel.top + panel.height);
    wash[3].position = sf::Vector2f(panel.left, panel.top + panel.height);
    wash[0].color = sf::Color(32, 41, 70, 154);
    wash[1].color = sf::Color(40, 90, 136, 112);
    wash[2].color = sf::Color(12, 30, 70, 120);
    wash[3].color = sf::Color(30, 28, 60, 144);
    window.draw(wash);

    sf::RectangleShape box(sf::Vector2f(panel.width, panel.height));
    box.setPosition(panel.left, panel.top);
    box.setFillColor(sf::Color::Transparent);
    box.setOutlineThickness(3.0f);
    box.setOutlineColor(sf::Color(255, 212, 111, 178));
    window.draw(box);

    for (int i = 0; i < 4; ++i)
    {
        sf::RectangleShape line(sf::Vector2f(panel.width - 72.0f, 3.0f));
        line.setPosition(panel.left + 36.0f, panel.top + 74.0f + i * 94.0f);
        line.setFillColor(i % 2 == 0 ? sf::Color(255, 230, 143, 46) : sf::Color(79, 220, 232, 44));
        window.draw(line, sf::BlendAdd);
    }
    addButton("settings_lang", tr("settings.language") + ": " + (language == "vi" ? "VI" : "EN"),
        sf::FloatRect(500, 220, 440, 58));
    addSlider("music", tr("settings.music"), sf::FloatRect(510, 390, 420, 14), musicVolume);
    addSlider("sfx", tr("settings.sfx"), sf::FloatRect(510, 516, 420, 14), sfxVolume);
    addButton("back", tr("back"), sf::FloatRect(32, 30, 160, 48));
    drawButtons();
    for (size_t i = 0; i < sliders.size(); ++i)
        drawSlider(sliders[i], sliders[i].id == "music" ? tr("settings.music") : tr("settings.sfx"));
}

void GameApp::drawGuide()
{
    drawBackground("guide");
    drawText(tr("menu.guide"), 54, sf::Vector2f(WindowW * 0.5f, 120.0f),
        sf::Color(255, 242, 210), true);
    sf::FloatRect panel(160, 205, 1120, 430);
    sf::RectangleShape rect(sf::Vector2f(panel.width, panel.height));
    rect.setPosition(panel.left, panel.top);
    rect.setFillColor(sf::Color(22, 25, 42, 128));
    rect.setOutlineColor(sf::Color(255, 212, 111, 182));
    rect.setOutlineThickness(4.0f);
    window.draw(rect);
    float x = panel.left + 58.0f;
    drawText("Luật thắng: nối đủ 5 quân liên tiếp trên bàn 12x12.", 26,
        sf::Vector2f(x, panel.top + 62), sf::Color(248, 240, 214), false);
    drawText("Đi quân: click chuột hoặc dùng WASD / phím mũi tên.", 24,
        sf::Vector2f(x, panel.top + 116), sf::Color(226, 242, 244), false);
    drawText("Enter / Space để hạ quân vào ô đang chọn.", 24,
        sf::Vector2f(x + 34, panel.top + 152), sf::Color(226, 242, 244), false);
    drawText("Lưu và tải: nhấn L để lưu, T để mở lịch sử ván.", 24,
        sf::Vector2f(x, panel.top + 206), sf::Color(226, 242, 244), false);
    drawText("Chỉ ván đang chơi mới có thể tiếp tục được.", 24,
        sf::Vector2f(x + 34, panel.top + 242), sf::Color(226, 242, 244), false);
    drawText("Đi lại: Ctrl+Z gọi Nghịch Chuyển Nhân Quả.", 24,
        sf::Vector2f(x, panel.top + 296), sf::Color(226, 242, 244), false);
    drawText("Đấu bot sẽ lùi cả cặp nước người chơi + Tâm Ma.", 24,
        sf::Vector2f(x + 34, panel.top + 332), sf::Color(226, 242, 244), false);
    drawText("Khai mở khí vận: thẻ điểm 0-999 quyết định ai được đi trước.", 24,
        sf::Vector2f(x, panel.top + 386), sf::Color(255, 224, 140), false);
    addButton("back", tr("back"), sf::FloatRect(32, 30, 160, 48));
    drawButtons();
}

void GameApp::drawInfo()
{
    drawBackground("info");
    drawText(tr("menu.info"), 54, sf::Vector2f(WindowW * 0.5f, 120.0f),
        sf::Color(255, 242, 210), true);
    sf::RectangleShape box(sf::Vector2f(760, 360));
    box.setPosition(340, 218);
    box.setFillColor(sf::Color(23, 25, 42, 132));
    box.setOutlineColor(sf::Color(255, 212, 111, 180));
    box.setOutlineThickness(4.0f);
    window.draw(box);
    drawText("Nhóm 5", 34, sf::Vector2f(720, 270), sf::Color(255, 236, 190), true);
    drawText("25310004 Cao Thanh Danh", 26, sf::Vector2f(720, 330), sf::Color(226, 242, 244), true);
    drawText("25310028 Nguyễn Đình Ý Như", 26, sf::Vector2f(720, 374), sf::Color(226, 242, 244), true);
    drawText("25310031 Trần Gia Sang", 26, sf::Vector2f(720, 418), sf::Color(226, 242, 244), true);
    drawText("25310034 Huỳnh Minh Thế", 26, sf::Vector2f(720, 462), sf::Color(226, 242, 244), true);
    drawText("25310050 Trương Hoài Đức", 26, sf::Vector2f(720, 506), sf::Color(226, 242, 244), true);
    drawText("Giáo Viên Hướng Dẫn Trương Toàn Thịnh", 26, sf::Vector2f(720, 550), sf::Color(226, 242, 244), true);
    addButton("back", tr("back"), sf::FloatRect(32, 30, 160, 48));
    drawButtons();
}

void GameApp::drawResult()
{
    if (resultKind == "draw") drawBackground("draw");
    else if (resultKind == "lose") drawBackground("lose");
    else drawBackground("win");
    drawBoard();

    sf::RectangleShape side(sf::Vector2f(610.0f, 650.0f));
    side.setPosition(780.0f, 92.0f);
    side.setFillColor(sf::Color(255, 240, 188, 150));
    side.setOutlineThickness(2.0f);
    side.setOutlineColor(sf::Color(112, 76, 35, 145));
    window.draw(side);

    drawPlayerPanel(810.0f, 116.0f, player1Name, CellState::X, player1Avatar,
        currentTurn == CellState::X, false);
    drawPlayerPanel(1110.0f, 116.0f, player2Name, CellState::O, player2Avatar,
        currentTurn == CellState::O, vsBot);

    drawResultEffect();

    sf::FloatRect resultPanel(835, 424, 500, 236);
    sf::RectangleShape panel(sf::Vector2f(resultPanel.width, resultPanel.height));
    panel.setPosition(resultPanel.left, resultPanel.top);
    panel.setFillColor(resultKind == "lose" ? sf::Color(245, 232, 255, 226)
        : (resultKind == "draw" ? sf::Color(236, 252, 252, 226) : sf::Color(255, 246, 214, 226)));
    panel.setOutlineThickness(4.0f);
    panel.setOutlineColor(resultKind == "lose" ? sf::Color(150, 103, 205)
        : (resultKind == "draw" ? sf::Color(87, 173, 173) : sf::Color(220, 154, 51)));
    window.draw(panel);

    drawTextBox(resultText, 38, sf::FloatRect(resultPanel.left + 24, resultPanel.top + 26,
        resultPanel.width - 48, 74), resultKind == "lose" ? sf::Color(118, 78, 144)
        : sf::Color(176, 111, 35));

    const std::vector<BoardMove>& moves = board.getMoves();
    if (!moves.empty())
    {
        const BoardMove& last = moves.back();
        std::string mark = last.mark == CellState::X ? "X" : "O";
        drawText(tr("last.move") + ": " + mark + " - " + cellName(last.row, last.col),
            23, sf::Vector2f(resultPanel.left + resultPanel.width * 0.5f, resultPanel.top + 116),
            sf::Color(70, 83, 95), true);
    }

    addButton("again", tr("again"), sf::FloatRect(resultPanel.left + 54, resultPanel.top + 146, 180, 52));
    addButton("result_menu", tr("playing.menu"), sf::FloatRect(resultPanel.left + 266, resultPanel.top + 146, 180, 52));
    drawButtons();
}

void GameApp::drawDialog()
{
    buttons.clear();
    sliders.clear();
    sf::RectangleShape shade(sf::Vector2f(static_cast<float>(WindowW), static_cast<float>(WindowH)));
    shade.setFillColor(sf::Color(255, 255, 255, 116));
    window.draw(shade);

    sf::FloatRect panel(420, 250, 600, dialogMode == DialogMode::Overwrite ? 250.0f : 280.0f);
    sf::RectangleShape box(sf::Vector2f(panel.width, panel.height));
    box.setPosition(panel.left, panel.top);
    box.setFillColor(sf::Color(252, 253, 247, 242));
    box.setOutlineColor(sf::Color(181, 158, 96));
    box.setOutlineThickness(3.0f);
    window.draw(box);

    if (dialogMode == DialogMode::Overwrite)
    {
        drawText(tr("dialog.overwrite"), 30, sf::Vector2f(panel.left + panel.width * 0.5f, panel.top + 62),
            sf::Color(83, 79, 62), true);
        addButton("dialog_yes", tr("dialog.yes"), sf::FloatRect(panel.left + 94, panel.top + 142, 180, 52));
        addButton("dialog_no", tr("dialog.no"), sf::FloatRect(panel.left + 326, panel.top + 142, 180, 52));
        drawButtons();
        return;
    }

    drawText(dialogMode == DialogMode::Save ? tr("dialog.save.title") : tr("dialog.load.title"),
        28, sf::Vector2f(panel.left + panel.width * 0.5f, panel.top + 48),
        sf::Color(73, 80, 83), true);
    sf::RectangleShape input(sf::Vector2f(440, 46));
    input.setPosition(panel.left + 80, panel.top + 100);
    input.setFillColor(sf::Color::White);
    input.setOutlineThickness(2.0f);
    input.setOutlineColor(sf::Color(121, 171, 181));
    window.draw(input);
    drawText(dialogInput + ((static_cast<int>(animationTime * 2) % 2) ? "_" : ""),
        24, sf::Vector2f(panel.left + 96, panel.top + 108), sf::Color(47, 63, 70), false);
    if (!dialogMessage.empty())
        drawText(dialogMessage, 21, sf::Vector2f(panel.left + panel.width * 0.5f, panel.top + 166),
            sf::Color(174, 74, 66), true);
    addButton("dialog_ok", "OK", sf::FloatRect(panel.left + 94, panel.top + 210, 180, 48));
    addButton("dialog_cancel", tr("dialog.cancel"), sf::FloatRect(panel.left + 326, panel.top + 210, 180, 48));
    drawButtons();
}

void GameApp::drawParticles(float alphaScale)
{
    for (size_t i = 0; i < particles.size(); ++i)
    {
        sf::CircleShape c(particles[i].radius);
        c.setOrigin(particles[i].radius, particles[i].radius);
        c.setPosition(particles[i].pos);
        sf::Color color = particles[i].color;
        float life = particles[i].life / particles[i].maxLife;
        color.a = static_cast<sf::Uint8>(color.a * life * alphaScale);
        c.setFillColor(color);
        window.draw(c);
    }
}

void GameApp::drawButtons()
{
    for (size_t i = 0; i < buttons.size(); ++i)
    {
        if (buttons[i].id.compare(0, 12, "history_row_") == 0)
            continue;
        drawButton(buttons[i]);
    }
}

void GameApp::drawButton(const Button& button)
{
    bool hover = button.rect.contains(mousePos);
    sf::RectangleShape shadow(sf::Vector2f(button.rect.width - 10.0f, button.rect.height * 0.48f));
    shadow.setPosition(button.rect.left + 5.0f, button.rect.top + button.rect.height * 0.58f);
    shadow.setFillColor(sf::Color(20, 15, 11, hover ? 94 : 68));
    window.draw(shadow);

    if (hover)
    {
        sf::CircleShape aura(button.rect.height * 0.86f, 72);
        aura.setOrigin(aura.getRadius(), aura.getRadius());
        aura.setPosition(button.rect.left + button.rect.width * 0.5f,
            button.rect.top + button.rect.height * 0.5f);
        aura.setScale(button.rect.width / button.rect.height * 0.52f, 0.70f);
        aura.setFillColor(sf::Color(255, 231, 125, 50));
        window.draw(aura, sf::BlendAdd);
    }

    if (hasCardTexture)
    {
        const sf::IntRect source(74, 106, 2361, 415);
        sf::Sprite card(cardTexture);
        card.setTextureRect(source);
        card.setPosition(button.rect.left, button.rect.top);
        card.setScale(button.rect.width / static_cast<float>(source.width),
            button.rect.height / static_cast<float>(source.height));
        card.setColor(hover ? sf::Color(255, 255, 235, 255) : sf::Color(255, 249, 219, 242));
        window.draw(card);

        sf::RectangleShape glaze(sf::Vector2f(button.rect.width - 58.0f, button.rect.height * 0.30f));
        glaze.setPosition(button.rect.left + 29.0f, button.rect.top + button.rect.height * 0.20f);
        glaze.setFillColor(sf::Color(255, 255, 232, hover ? 76 : 44));
        window.draw(glaze, sf::BlendAdd);
    }
    else
    {
        sf::RectangleShape shape(sf::Vector2f(button.rect.width, button.rect.height));
        shape.setPosition(button.rect.left, button.rect.top);
        shape.setFillColor(hover ? sf::Color(255, 234, 153, 242) : sf::Color(238, 222, 158, 230));
        shape.setOutlineThickness(hover ? 4.0f : 3.0f);
        shape.setOutlineColor(hover ? sf::Color(50, 205, 218) : sf::Color(126, 83, 42));
        window.draw(shape);
    }

    sf::RectangleShape rim(sf::Vector2f(button.rect.width - 44.0f, 2.0f));
    rim.setPosition(button.rect.left + 22.0f, button.rect.top + button.rect.height - 10.0f);
    rim.setFillColor(hover ? sf::Color(62, 213, 226, 160) : sf::Color(166, 116, 54, 110));
    window.draw(rim, sf::BlendAdd);

    unsigned int size = 25;
    if (button.label.size() > 32) size = 19;
    else if (button.label.size() > 22) size = 21;
    drawTextBox(button.label, size, button.rect, sf::Color(45, 47, 43));
}

void GameApp::drawSlider(const Slider& slider, const std::string& label)
{
    drawText(label + ": " + std::to_string(static_cast<int>(slider.value)) + "%",
        28, sf::Vector2f(slider.track.left + slider.track.width * 0.5f, slider.track.top - 58),
        sf::Color(255, 242, 210), true);
    sf::RectangleShape track(sf::Vector2f(slider.track.width, slider.track.height));
    track.setPosition(slider.track.left, slider.track.top);
    track.setFillColor(sf::Color(220, 236, 232, 235));
    track.setOutlineColor(sf::Color(104, 157, 167));
    track.setOutlineThickness(2.0f);
    window.draw(track);

    sf::CircleShape knob(18.0f);
    knob.setOrigin(18.0f, 18.0f);
    knob.setPosition(slider.track.left + slider.track.width * slider.value / 100.0f,
        slider.track.top + slider.track.height * 0.5f);
    knob.setFillColor(sf::Color(243, 181, 87));
    knob.setOutlineColor(sf::Color::White);
    knob.setOutlineThickness(3.0f);
    window.draw(knob);
}

void GameApp::drawBoard()
{
    sf::RectangleShape base(sf::Vector2f(BoardRect.width + 44, BoardRect.height + 44));
    base.setPosition(BoardRect.left - 22, BoardRect.top - 22);
    base.setFillColor(sf::Color(255, 249, 221, 226));
    base.setOutlineColor(sf::Color(178, 118, 54));
    base.setOutlineThickness(5.0f);
    window.draw(base);

    sf::RectangleShape boardBg(sf::Vector2f(BoardRect.width, BoardRect.height));
    boardBg.setPosition(BoardRect.left, BoardRect.top);
    boardBg.setFillColor(sf::Color(238, 255, 235, 238));
    boardBg.setOutlineColor(sf::Color(74, 154, 159));
    boardBg.setOutlineThickness(4.0f);
    window.draw(boardBg);

    float cell = BoardRect.width / BoardModel::Size;
    sf::Vector2f boardCenter(BoardRect.left + BoardRect.width * 0.5f,
        BoardRect.top + BoardRect.height * 0.5f);

    const sf::Color elementColors[5] = {
        sf::Color(255, 224, 92, 165),
        sf::Color(88, 220, 133, 155),
        sf::Color(75, 214, 238, 155),
        sf::Color(248, 92, 82, 150),
        sf::Color(180, 126, 72, 150)
    };
    for (int i = 0; i < 5; ++i)
    {
        float x = BoardRect.left + BoardRect.width * (0.16f + i * 0.17f);
        for (int side = 0; side < 2; ++side)
        {
            float y = side == 0 ? BoardRect.top - 14.0f : BoardRect.top + BoardRect.height + 14.0f;
            sf::CircleShape seal(11.0f + std::sin(animationTime * 3.5f + i) * 1.5f, 6);
            seal.setOrigin(seal.getRadius(), seal.getRadius());
            seal.setPosition(x, y);
            seal.setRotation(animationTime * (side == 0 ? 34.0f : -34.0f) + i * 36.0f);
            seal.setFillColor(elementColors[i]);
            seal.setOutlineThickness(2.0f);
            seal.setOutlineColor(sf::Color(255, 255, 232, 190));
            window.draw(seal);
        }
    }

    for (int i = 0; i < 4; ++i)
    {
        float a = animationTime * 1.15f + i * 1.5707f;
        sf::RectangleShape swordRay(sf::Vector2f(150.0f, 3.0f));
        swordRay.setOrigin(75.0f, 1.5f);
        swordRay.setPosition(boardCenter.x + std::cos(a) * BoardRect.width * 0.56f,
            boardCenter.y + std::sin(a) * BoardRect.height * 0.56f);
        swordRay.setRotation(a * 57.2958f + 90.0f);
        swordRay.setFillColor(i % 2 == 0 ? sf::Color(255, 230, 118, 94) : sf::Color(66, 221, 235, 88));
        window.draw(swordRay);
    }

    for (int i = 0; i < 4; ++i)
    {
        sf::Vector2f corner(BoardRect.left + (i % 2) * BoardRect.width,
            BoardRect.top + (i / 2) * BoardRect.height);
        sf::CircleShape seal(34.0f + std::sin(animationTime * 2.8f + i) * 3.0f, 8);
        seal.setOrigin(seal.getRadius(), seal.getRadius());
        seal.setPosition(corner);
        seal.setRotation(animationTime * 28.0f + i * 45.0f);
        seal.setFillColor(sf::Color(255, 245, 177, 72));
        seal.setOutlineThickness(3.0f);
        seal.setOutlineColor(i % 2 == 0 ? sf::Color(235, 146, 55, 155) : sf::Color(48, 185, 202, 155));
        window.draw(seal);
    }

    for (int r = 0; r < 4; ++r)
    {
        sf::CircleShape ring(BoardRect.width * (0.18f + r * 0.095f));
        ring.setOrigin(ring.getRadius(), ring.getRadius());
        ring.setPosition(boardCenter);
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineThickness(r == 0 ? 3.0f : 2.0f);
        ring.setOutlineColor(r % 2 == 0 ? sf::Color(220, 157, 49, 92) : sf::Color(45, 172, 185, 92));
        window.draw(ring);
    }

    for (int s = 0; s < 6; ++s)
    {
        float a = animationTime * (0.7f + s * 0.05f) + s * 1.04719f;
        float radius = BoardRect.width * (0.22f + (s % 3) * 0.105f);
        sf::CircleShape bead(5.0f + (s % 2) * 2.0f, 16);
        bead.setOrigin(bead.getRadius(), bead.getRadius());
        bead.setPosition(boardCenter.x + std::cos(a) * radius,
            boardCenter.y + std::sin(a) * radius);
        bead.setFillColor(s % 2 == 0 ? sf::Color(255, 219, 81, 178) : sf::Color(64, 220, 230, 165));
        window.draw(bead);
    }

    for (int s = 0; s < 5; ++s)
    {
        float offset = std::fmod(animationTime * 62.0f + s * 138.0f, BoardRect.width + 220.0f) - 110.0f;
        sf::RectangleShape stream(sf::Vector2f(170.0f, 3.0f));
        stream.setOrigin(85.0f, 1.5f);
        stream.setPosition(BoardRect.left + offset, BoardRect.top + 92.0f + s * 112.0f);
        stream.setRotation(18.0f);
        stream.setFillColor(s % 2 == 0 ? sf::Color(255, 211, 88, 72) : sf::Color(68, 216, 230, 70));
        window.draw(stream);
    }

    for (int i = 0; i <= BoardModel::Size; ++i)
    {
        float x = BoardRect.left + i * cell;
        float y = BoardRect.top + i * cell;
        sf::RectangleShape v(sf::Vector2f(2.0f, BoardRect.height));
        v.setPosition(x, BoardRect.top);
        v.setFillColor(sf::Color(91, 132, 129, 185));
        window.draw(v);
        sf::RectangleShape h(sf::Vector2f(BoardRect.width, 2.0f));
        h.setPosition(BoardRect.left, y);
        h.setFillColor(sf::Color(91, 132, 129, 185));
        window.draw(h);
    }

    for (int row = 0; row < BoardModel::Size; ++row)
    {
        for (int col = 0; col < BoardModel::Size; ++col)
        {
            float x = BoardRect.left + col * cell;
            float y = BoardRect.top + row * cell;
            sf::CircleShape rune(cell * 0.17f, 6);
            rune.setOrigin(rune.getRadius(), rune.getRadius());
            rune.setPosition(x + cell * 0.5f, y + cell * 0.5f);
            rune.setRotation(static_cast<float>((row * 17 + col * 23) % 360));
            rune.setFillColor(sf::Color::Transparent);
            rune.setOutlineThickness(1.0f);
            rune.setOutlineColor(sf::Color(111, 137, 109, 48));
            window.draw(rune);

            for (int k = 0; k < 3; ++k)
            {
                sf::RectangleShape trigram(sf::Vector2f(cell * 0.18f, 1.2f));
                trigram.setOrigin(cell * 0.09f, 0.6f);
                trigram.setPosition(x + cell * (0.5f + (k - 1) * 0.03f), y + cell * (0.37f + k * 0.08f));
                trigram.setRotation(static_cast<float>((row + col) % 2 == 0 ? 0 : 90));
                trigram.setFillColor(sf::Color(116, 91, 38, 42));
                window.draw(trigram);
            }
        }
    }

    sf::Vector2i pixel = sf::Mouse::getPosition(window);
    sf::Vector2f p = window.mapPixelToCoords(pixel);
    if (BoardRect.contains(p))
    {
        int col = static_cast<int>((p.x - BoardRect.left) / cell);
        int row = static_cast<int>((p.y - BoardRect.top) / cell);
        if (board.isInside(row, col))
        {
            sf::RectangleShape hover(sf::Vector2f(cell - 4, cell - 4));
            hover.setPosition(BoardRect.left + col * cell + 2, BoardRect.top + row * cell + 2);
            hover.setFillColor(sf::Color(255, 230, 160, 88));
            window.draw(hover);
        }
    }

    int cursorRow = board.getCursorRow();
    int cursorCol = board.getCursorCol();
    sf::RectangleShape cursor(sf::Vector2f(cell - 6, cell - 6));
    cursor.setPosition(BoardRect.left + cursorCol * cell + 3, BoardRect.top + cursorRow * cell + 3);
    cursor.setFillColor(sf::Color::Transparent);
    cursor.setOutlineColor(sf::Color(228, 142, 55));
    cursor.setOutlineThickness(3.0f);
    window.draw(cursor);

    const std::vector<BoardMove>& moves = board.getMoves();
    for (int row = 0; row < BoardModel::Size; ++row)
    {
        for (int col = 0; col < BoardModel::Size; ++col)
        {
            CellState state = board.getCell(row, col);
            if (state == CellState::Empty) continue;
            sf::Vector2f center(BoardRect.left + col * cell + cell * 0.5f,
                BoardRect.top + row * cell + cell * 0.5f);
            bool isLastMove = !moves.empty() && moves.back().row == row && moves.back().col == col;
            if (isLastMove)
            {
                sf::CircleShape glow(cell * (0.38f + std::sin(animationTime * 5.0f) * 0.04f));
                glow.setOrigin(glow.getRadius(), glow.getRadius());
                glow.setPosition(center);
                glow.setFillColor(sf::Color(255, 184, 65, 116));
                window.draw(glow);
            }
            if (state == CellState::X)
            {
                sf::CircleShape aura(cell * 0.36f);
                aura.setOrigin(aura.getRadius(), aura.getRadius());
                aura.setPosition(center);
                aura.setFillColor(sf::Color(48, 206, 232, isLastMove ? 82 : 44));
                window.draw(aura);

                for (int i = 0; i < 2; ++i)
                {
                    float rot = i == 0 ? 45.0f : -45.0f;
                    sf::RectangleShape shadow(sf::Vector2f(cell * 0.72f, 12.0f));
                    shadow.setOrigin(cell * 0.36f, 6.0f);
                    shadow.setPosition(center.x + 2.0f, center.y + 3.0f);
                    shadow.setRotation(rot);
                    shadow.setFillColor(sf::Color(19, 54, 68, 128));
                    window.draw(shadow);

                    sf::RectangleShape blade(sf::Vector2f(cell * 0.70f, 8.0f));
                    blade.setOrigin(cell * 0.35f, 4.0f);
                    blade.setPosition(center);
                    blade.setRotation(rot);
                    blade.setFillColor(sf::Color(24, 160, 190, 235));
                    window.draw(blade);

                    sf::RectangleShape edge(sf::Vector2f(cell * 0.56f, 3.0f));
                    edge.setOrigin(cell * 0.28f, 1.5f);
                    edge.setPosition(center.x - std::cos(rot * 0.0174533f) * cell * 0.02f,
                        center.y - std::sin(rot * 0.0174533f) * cell * 0.02f);
                    edge.setRotation(rot);
                    edge.setFillColor(sf::Color(244, 255, 232, 230));
                    window.draw(edge);
                }

                sf::Text glyph(utf8("X"), font, 30);
                glyph.setFillColor(sf::Color(236, 255, 245, 190));
                glyph.setOutlineThickness(2.0f);
                glyph.setOutlineColor(sf::Color(17, 103, 136, 210));
                sf::FloatRect b = glyph.getLocalBounds();
                glyph.setOrigin(b.left + b.width * 0.5f, b.top + b.height * 0.5f);
                glyph.setPosition(center);
                window.draw(glyph);
            }
            else
            {
                sf::CircleShape aura(cell * 0.37f);
                aura.setOrigin(aura.getRadius(), aura.getRadius());
                aura.setPosition(center);
                aura.setFillColor(sf::Color(255, 193, 64, isLastMove ? 88 : 46));
                window.draw(aura);

                sf::CircleShape outer(cell * 0.31f, 72);
                outer.setOrigin(outer.getRadius(), outer.getRadius());
                outer.setPosition(center);
                outer.setFillColor(sf::Color(255, 255, 244, 82));
                outer.setOutlineThickness(7.0f);
                outer.setOutlineColor(sf::Color(204, 139, 34, 238));
                window.draw(outer);

                sf::CircleShape inner(cell * 0.20f, 72);
                inner.setOrigin(inner.getRadius(), inner.getRadius());
                inner.setPosition(center);
                inner.setFillColor(sf::Color::Transparent);
                inner.setOutlineThickness(2.0f);
                inner.setOutlineColor(sf::Color(255, 239, 159, 220));
                window.draw(inner);

                for (int k = 0; k < 8; ++k)
                {
                    float angle = animationTime * (isLastMove ? 1.2f : 0.35f) + k * 0.785398f;
                    sf::RectangleShape rune(sf::Vector2f(cell * 0.10f, 2.0f));
                    rune.setOrigin(cell * 0.05f, 1.0f);
                    rune.setPosition(center.x + std::cos(angle) * cell * 0.32f,
                        center.y + std::sin(angle) * cell * 0.32f);
                    rune.setRotation(angle * 57.2958f + 90.0f);
                    rune.setFillColor(sf::Color(148, 87, 24, 150));
                    window.draw(rune);
                }

                sf::Text glyph(utf8("O"), font, 30);
                glyph.setFillColor(sf::Color(255, 251, 218, 170));
                glyph.setOutlineThickness(2.0f);
                glyph.setOutlineColor(sf::Color(176, 98, 26, 200));
                sf::FloatRect b = glyph.getLocalBounds();
                glyph.setOrigin(b.left + b.width * 0.5f, b.top + b.height * 0.5f);
                glyph.setPosition(center);
                window.draw(glyph);
            }

            if (isLastMove)
            {
                sf::CircleShape ring(cell * (0.39f + std::sin(animationTime * 7.0f) * 0.03f));
                ring.setOrigin(ring.getRadius(), ring.getRadius());
                ring.setPosition(center);
                ring.setFillColor(sf::Color::Transparent);
                ring.setOutlineThickness(4.0f);
                ring.setOutlineColor(sf::Color(255, 111, 61, 220));
                window.draw(ring);

                sf::CircleShape dot(5.0f);
                dot.setOrigin(5.0f, 5.0f);
                dot.setPosition(center.x + cell * 0.28f, center.y - cell * 0.28f);
                dot.setFillColor(sf::Color(255, 97, 112, 235));
                window.draw(dot);
            }
        }
    }

    for (size_t i = 0; i < moveEffects.size(); ++i)
        drawMoveEffect(moveEffects[i], cell);
    for (size_t i = 0; i < undoEffects.size(); ++i)
        drawUndoEffect(undoEffects[i], cell);

    for (size_t i = 0; i < winningCells.size(); ++i)
    {
        const sf::Color elements[5] = {
            sf::Color(255, 223, 93, 230),
            sf::Color(91, 213, 121, 230),
            sf::Color(72, 190, 246, 230),
            sf::Color(245, 91, 74, 230),
            sf::Color(183, 126, 74, 230)
        };
        int row = winningCells[i].first;
        int col = winningCells[i].second;
        float pulse = std::sin(animationTime * 6.0f + static_cast<float>(i)) * 0.04f;
        sf::CircleShape ring(cell * (0.38f + pulse));
        ring.setOrigin(ring.getRadius(), ring.getRadius());
        ring.setPosition(BoardRect.left + col * cell + cell * 0.5f,
            BoardRect.top + row * cell + cell * 0.5f);
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineThickness(6.0f);
        ring.setOutlineColor(elements[i % 5]);
        window.draw(ring);

        sf::CircleShape glow(cell * 0.45f);
        glow.setOrigin(glow.getRadius(), glow.getRadius());
        glow.setPosition(BoardRect.left + col * cell + cell * 0.5f,
            BoardRect.top + row * cell + cell * 0.5f);
        sf::Color glowColor = elements[i % 5];
        glowColor.a = 58;
        glow.setFillColor(glowColor);
        window.draw(glow);
    }
}

void GameApp::drawMoveEffect(const MoveEffect& effect, float cell)
{
    float t = clampFloat(effect.age / effect.duration, 0.0f, 1.0f);
    float x = BoardRect.left + effect.move.col * cell;
    float y = BoardRect.top + effect.move.row * cell;
    sf::Vector2f center(x + cell * 0.5f, y + cell * 0.5f);
    sf::Uint8 alpha = static_cast<sf::Uint8>(220 * (1.0f - t));
    sf::Color main = effect.move.mark == CellState::X ? sf::Color(56, 215, 235, alpha)
        : sf::Color(255, 198, 64, alpha);
    sf::Color second = effect.move.mark == CellState::X ? sf::Color(255, 224, 101, alpha)
        : sf::Color(218, 82, 168, alpha);

    float neighborBoost = 1.0f + effect.neighborPower * 0.09f;
    for (int i = 0; i < 3; ++i)
    {
        float r = cell * (0.26f + i * 0.16f + t * 0.32f) * neighborBoost;
        sf::CircleShape ring(r);
        ring.setOrigin(r, r);
        ring.setPosition(center);
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineThickness(2.0f);
        sf::Color c = (i % 2 == 0) ? main : second;
        c.a = static_cast<sf::Uint8>(c.a * (0.72f - i * 0.16f));
        ring.setOutlineColor(c);
        window.draw(ring);
    }

    for (int i = 0; i < 8; ++i)
    {
        float angle = animationTime * 3.2f + i * 0.785398f + effect.variant * 0.35f;
        float dist = cell * (0.20f + 0.22f * (1.0f - t));
        sf::CircleShape petal(cell * 0.055f, 14);
        petal.setOrigin(petal.getRadius(), petal.getRadius());
        petal.setScale(1.0f, 0.45f);
        petal.setRotation(angle * 57.2958f);
        petal.setPosition(center.x + std::cos(angle) * dist, center.y + std::sin(angle) * dist);
        sf::Color c = (i % 2 == 0) ? sf::Color(255, 112, 172, alpha) : second;
        c.a = static_cast<sf::Uint8>(c.a * 0.72f);
        petal.setFillColor(c);
        window.draw(petal);
    }

    if (t < 0.58f)
    {
        float sealT = t / 0.58f;
        float life = 1.0f - sealT;
        float drop = life * 118.0f;
        float breathe = 1.0f + life * 0.28f + std::sin(animationTime * 11.0f) * 0.035f;
        sf::Vector2f sealCenter(center.x, center.y - drop);

        sf::CircleShape plate(cell * 0.36f * breathe, 80);
        plate.setOrigin(plate.getRadius(), plate.getRadius());
        plate.setPosition(sealCenter);
        plate.setFillColor(sf::Color(255, 255, 238, static_cast<sf::Uint8>(78 * life)));
        plate.setOutlineThickness(3.0f);
        plate.setOutlineColor(sf::Color(main.r, main.g, main.b, static_cast<sf::Uint8>(220 * life)));
        window.draw(plate);

        sf::CircleShape inner(cell * 0.24f * breathe, 72);
        inner.setOrigin(inner.getRadius(), inner.getRadius());
        inner.setPosition(sealCenter);
        inner.setFillColor(sf::Color::Transparent);
        inner.setOutlineThickness(2.0f);
        inner.setOutlineColor(sf::Color(second.r, second.g, second.b, static_cast<sf::Uint8>(190 * life)));
        window.draw(inner);

        if (effect.move.mark == CellState::X)
        {
            for (int i = 0; i < 2; ++i)
            {
                float rot = i == 0 ? 43.0f : -43.0f;
                sf::RectangleShape blade(sf::Vector2f(cell * 0.92f * breathe, 11.0f));
                blade.setOrigin(blade.getSize().x * 0.5f, 5.5f);
                blade.setPosition(sealCenter.x + 2.0f, sealCenter.y + 4.0f);
                blade.setRotation(rot);
                blade.setFillColor(sf::Color(29, 77, 91, static_cast<sf::Uint8>(115 * life)));
                window.draw(blade);

                sf::RectangleShape edge(sf::Vector2f(cell * 0.82f * breathe, 5.0f));
                edge.setOrigin(edge.getSize().x * 0.5f, 2.5f);
                edge.setPosition(sealCenter);
                edge.setRotation(rot);
                edge.setFillColor(sf::Color(233, 255, 246, static_cast<sf::Uint8>(230 * life)));
                window.draw(edge);
            }
        }
        else
        {
            for (int i = 0; i < 10; ++i)
            {
                float angle = -animationTime * 3.6f + i * 0.628318f;
                sf::RectangleShape rune(sf::Vector2f(cell * 0.12f, 3.0f));
                rune.setOrigin(cell * 0.06f, 1.5f);
                rune.setPosition(sealCenter.x + std::cos(angle) * cell * 0.34f * breathe,
                    sealCenter.y + std::sin(angle) * cell * 0.34f * breathe);
                rune.setRotation(angle * 57.2958f + 90.0f);
                rune.setFillColor(sf::Color(145, 74, 20, static_cast<sf::Uint8>(180 * life)));
                window.draw(rune);
            }
        }

        sf::Text shadow(utf8(effect.move.mark == CellState::X ? "X" : "O"), font, 58);
        shadow.setFillColor(sf::Color(45, 45, 52, static_cast<sf::Uint8>(115 * life)));
        sf::FloatRect shadowBounds = shadow.getLocalBounds();
        shadow.setOrigin(shadowBounds.left + shadowBounds.width * 0.5f,
            shadowBounds.top + shadowBounds.height * 0.5f);
        shadow.setPosition(sealCenter.x + 3.0f, sealCenter.y + 5.0f);
        shadow.setScale(breathe, breathe);
        shadow.setRotation(std::sin(animationTime * 7.0f) * 6.0f);
        window.draw(shadow);

        sf::Text seal(utf8(effect.move.mark == CellState::X ? "X" : "O"), font, 58);
        seal.setFillColor(effect.move.mark == CellState::X
            ? sf::Color(235, 255, 246, static_cast<sf::Uint8>(245 * life))
            : sf::Color(255, 248, 200, static_cast<sf::Uint8>(245 * life)));
        seal.setOutlineColor(effect.move.mark == CellState::X
            ? sf::Color(20, 151, 184, static_cast<sf::Uint8>(235 * life))
            : sf::Color(199, 106, 28, static_cast<sf::Uint8>(235 * life)));
        seal.setOutlineThickness(4.0f);
        sf::FloatRect b = seal.getLocalBounds();
        seal.setOrigin(b.left + b.width * 0.5f, b.top + b.height * 0.5f);
        seal.setPosition(sealCenter);
        seal.setScale(breathe, breathe);
        seal.setRotation(std::sin(animationTime * 7.0f) * 6.0f);
        window.draw(seal);
    }

    if (t < 0.78f)
    {
        float slashAlpha = 210 * (1.0f - t / 0.78f);
        for (int i = 0; i < 2; ++i)
        {
            sf::RectangleShape slash(sf::Vector2f(cell * (0.95f - i * 0.18f), 4.0f));
            slash.setOrigin(slash.getSize().x * 0.5f, 2.0f);
            slash.setPosition(center.x, center.y);
            slash.setRotation(effect.move.mark == CellState::X ? (35.0f - i * 74.0f) : (12.0f + i * 64.0f));
            slash.setFillColor(i == 0 ? sf::Color(255, 255, 232, static_cast<sf::Uint8>(slashAlpha))
                : sf::Color(main.r, main.g, main.b, static_cast<sf::Uint8>(slashAlpha)));
            window.draw(slash);
        }
    }
}

void GameApp::drawUndoEffect(const UndoEffect& effect, float cell)
{
    float t = clampFloat(effect.age / effect.duration, 0.0f, 1.0f);
    float fade = 1.0f - t;
    sf::Vector2f center(BoardRect.left + effect.col * cell + cell * 0.5f,
        BoardRect.top + effect.row * cell + cell * 0.5f);
    float radius = cell * (0.22f + t * 0.42f);

    sf::CircleShape outer(radius);
    outer.setOrigin(radius, radius);
    outer.setPosition(center);
    outer.setFillColor(sf::Color(255, 255, 255, static_cast<sf::Uint8>(48 * fade)));
    outer.setOutlineThickness(5.0f);
    outer.setOutlineColor(sf::Color(53, 58, 67, static_cast<sf::Uint8>(220 * fade)));
    window.draw(outer);

    float a = animationTime * 7.0f;
    sf::CircleShape dot(radius * 0.32f);
    dot.setOrigin(dot.getRadius(), dot.getRadius());
    dot.setFillColor(sf::Color(35, 42, 50, static_cast<sf::Uint8>(210 * fade)));
    dot.setPosition(center.x + std::cos(a) * radius * 0.42f, center.y + std::sin(a) * radius * 0.42f);
    window.draw(dot);
    dot.setFillColor(sf::Color(255, 255, 245, static_cast<sf::Uint8>(210 * fade)));
    dot.setPosition(center.x - std::cos(a) * radius * 0.42f, center.y - std::sin(a) * radius * 0.42f);
    window.draw(dot);

    sf::CircleShape pulse(radius * (1.15f + std::sin(animationTime * 10.0f) * 0.08f));
    pulse.setOrigin(pulse.getRadius(), pulse.getRadius());
    pulse.setPosition(center);
    pulse.setFillColor(sf::Color::Transparent);
    pulse.setOutlineThickness(2.0f);
    pulse.setOutlineColor(sf::Color(130, 225, 235, static_cast<sf::Uint8>(180 * fade)));
    window.draw(pulse);
}

void GameApp::drawFateCard(const sf::FloatRect& rect, const std::string& owner,
    int score, bool revealed, float revealProgress, sf::Color accent)
{
    revealProgress = clampFloat(revealProgress, 0.0f, 1.0f);
    float flip = std::abs(revealProgress * 2.0f - 1.0f);
    float width = rect.width * (0.16f + 0.84f * flip);
    sf::FloatRect live(rect.left + (rect.width - width) * 0.5f, rect.top, width, rect.height);
    bool face = revealed || revealProgress > 0.52f;

    sf::RectangleShape shadow(sf::Vector2f(live.width, live.height));
    shadow.setPosition(live.left + 11.0f, live.top + 14.0f);
    shadow.setFillColor(sf::Color(8, 10, 18, 112));
    window.draw(shadow);

    if (hasCardTexture)
    {
        const sf::IntRect cardSource(74, 106, 2361, 415);
        sf::Sprite card(cardTexture);
        card.setTextureRect(cardSource);
        card.setOrigin(0.0f, 0.0f);
        card.setPosition(live.left + live.width, live.top);
        card.setRotation(90.0f);
        card.setScale(live.height / static_cast<float>(cardSource.width),
            live.width / static_cast<float>(cardSource.height));
        card.setColor(face ? sf::Color(255, 255, 255, 248)
            : sf::Color(108, 126, 164, 238));
        window.draw(card);

        if (!face)
        {
            sf::RectangleShape veil(sf::Vector2f(live.width - 20.0f, live.height - 28.0f));
            veil.setPosition(live.left + 10.0f, live.top + 14.0f);
            veil.setFillColor(sf::Color(18, 24, 52, 118));
            window.draw(veil);
        }
    }
    else
    {
        sf::RectangleShape card(sf::Vector2f(live.width, live.height));
        card.setPosition(live.left, live.top);
        card.setFillColor(face ? sf::Color(255, 239, 182, 236) : sf::Color(34, 39, 66, 232));
        card.setOutlineThickness(5.0f);
        card.setOutlineColor(face ? sf::Color(accent.r, accent.g, accent.b, 218)
            : sf::Color(255, 212, 103, 190));
        window.draw(card);
    }

    sf::RectangleShape rim(sf::Vector2f(live.width, live.height));
    rim.setPosition(live.left, live.top);
    rim.setFillColor(sf::Color::Transparent);
    rim.setOutlineThickness(3.0f);
    rim.setOutlineColor(face ? sf::Color(accent.r, accent.g, accent.b, 168)
        : sf::Color(255, 221, 126, 168));
    window.draw(rim, sf::BlendAdd);

    sf::RectangleShape cardShine(sf::Vector2f(std::max(0.0f, live.width - 42.0f), 4.0f));
    cardShine.setPosition(live.left + 21.0f, live.top + 22.0f);
    cardShine.setFillColor(sf::Color(255, 255, 226, face ? 118 : 68));
    window.draw(cardShine, sf::BlendAdd);

    if (live.width < rect.width * 0.28f) return;

    sf::CircleShape sigil(70.0f + std::sin(animationTime * 4.0f) * 4.0f, 72);
    sigil.setOrigin(sigil.getRadius(), sigil.getRadius());
    sigil.setPosition(rect.left + rect.width * 0.5f, rect.top + 132.0f);
    sigil.setFillColor(sf::Color::Transparent);
    sigil.setOutlineThickness(3.0f);
    sigil.setOutlineColor(sf::Color(accent.r, accent.g, accent.b, face ? 120 : 170));
    window.draw(sigil);

    if (!face)
    {
        drawText("?", 86, sf::Vector2f(rect.left + rect.width * 0.5f, rect.top + 144.0f),
            sf::Color(255, 232, 142), true);
        drawTextBox(owner, 24, sf::FloatRect(rect.left + 24, rect.top + 232, rect.width - 48, 42),
            sf::Color(236, 246, 247));
        return;
    }

    drawTextBox(owner, 25, sf::FloatRect(rect.left + 22, rect.top + 32, rect.width - 44, 42),
        sf::Color(62, 56, 75));
    drawText(std::to_string(score), 74, sf::Vector2f(rect.left + rect.width * 0.5f, rect.top + 150.0f),
        score >= 700 ? sf::Color(210, 82, 48) : sf::Color(43, 96, 124), true);
    drawText("khí vận", 26, sf::Vector2f(rect.left + rect.width * 0.5f, rect.top + 230.0f),
        sf::Color(96, 70, 53), true);

    for (int i = 0; i < 10; ++i)
    {
        float a = animationTime * 2.5f + i * 0.628318f;
        sf::CircleShape dot(4.0f, 12);
        dot.setOrigin(4.0f, 4.0f);
        dot.setPosition(rect.left + rect.width * 0.5f + std::cos(a) * 92.0f,
            rect.top + 150.0f + std::sin(a) * 92.0f);
        dot.setFillColor(sf::Color(accent.r, accent.g, accent.b, 170));
        window.draw(dot);
    }
}

void GameApp::drawResultEffect()
{
    sf::FloatRect effectBounds(790.0f, 104.0f, 590.0f, 620.0f);
    sf::View previousView = window.getView();
    sf::View effectView(effectBounds);
    effectView.setViewport(sf::FloatRect(effectBounds.left / static_cast<float>(WindowW),
        effectBounds.top / static_cast<float>(WindowH),
        effectBounds.width / static_cast<float>(WindowW),
        effectBounds.height / static_cast<float>(WindowH)));
    window.setView(effectView);

    sf::Color primary = sf::Color(255, 205, 76, 150);
    sf::Color secondary = sf::Color(58, 220, 235, 150);
    sf::Color danger = sf::Color(226, 74, 106, 145);
    if (resultKind == "lose")
    {
        primary = sf::Color(169, 105, 255, 150);
        secondary = sf::Color(234, 78, 126, 142);
        danger = sf::Color(255, 201, 92, 135);
    }
    else if (resultKind == "draw")
    {
        primary = sf::Color(58, 220, 235, 140);
        secondary = sf::Color(255, 204, 74, 142);
        danger = sf::Color(255, 248, 210, 130);
    }

    sf::VertexArray aura(sf::Quads, 4);
    aura[0].position = sf::Vector2f(effectBounds.left, effectBounds.top);
    aura[1].position = sf::Vector2f(effectBounds.left + effectBounds.width, effectBounds.top);
    aura[2].position = sf::Vector2f(effectBounds.left + effectBounds.width, effectBounds.top + effectBounds.height);
    aura[3].position = sf::Vector2f(effectBounds.left, effectBounds.top + effectBounds.height);
    aura[0].color = sf::Color(primary.r, primary.g, primary.b, 34);
    aura[1].color = sf::Color(secondary.r, secondary.g, secondary.b, 38);
    aura[2].color = sf::Color(22, 22, 38, 8);
    aura[3].color = sf::Color(danger.r, danger.g, danger.b, 22);
    window.draw(aura, sf::BlendAdd);

    sf::RectangleShape innerFrame(sf::Vector2f(effectBounds.width - 28.0f, effectBounds.height - 28.0f));
    innerFrame.setPosition(effectBounds.left + 14.0f, effectBounds.top + 14.0f);
    innerFrame.setFillColor(sf::Color::Transparent);
    innerFrame.setOutlineThickness(2.0f);
    innerFrame.setOutlineColor(sf::Color(255, 247, 205, 48));
    window.draw(innerFrame, sf::BlendAdd);

    for (int i = 0; i < 22; ++i)
    {
        float drift = std::fmod(animationTime * (0.12f + (i % 5) * 0.025f) + i * 0.073f, 1.0f);
        float x = effectBounds.left + 34.0f + std::fmod(static_cast<float>(i * 97), effectBounds.width - 68.0f);
        float y = effectBounds.top + effectBounds.height - drift * effectBounds.height;
        float size = 4.0f + static_cast<float>(i % 4) * 1.7f;
        sf::CircleShape shard(size, i % 2 == 0 ? 4 : 3);
        shard.setOrigin(size, size);
        shard.setPosition(x + std::sin(animationTime * 1.7f + i) * 18.0f, y);
        shard.setRotation(animationTime * (22.0f + i * 2.0f) + i * 31.0f);
        sf::Color c = (i % 3 == 0) ? primary : ((i % 3 == 1) ? secondary : danger);
        c.a = static_cast<sf::Uint8>(42 + (i % 5) * 11);
        shard.setFillColor(c);
        window.draw(shard, sf::BlendAdd);
    }

    sf::Vector2f p1(935.0f, 248.0f);
    sf::Vector2f p2(1235.0f, 248.0f);
    float cycle = std::fmod(animationTime * 0.72f, 1.0f);

    if (resultKind == "draw")
    {
        sf::Vector2f clash(1085.0f, 342.0f);
        float breathe = 1.0f + std::sin(animationTime * 5.2f) * 0.05f;
        for (int h = 0; h < 2; ++h)
        {
            sf::CircleShape balance(88.0f + h * 42.0f);
            balance.setOrigin(balance.getRadius(), balance.getRadius());
            balance.setPosition(clash);
            balance.setScale(breathe, breathe * 0.68f);
            balance.setFillColor(sf::Color::Transparent);
            balance.setOutlineThickness(h == 0 ? 4.0f : 2.0f);
            balance.setOutlineColor(h == 0 ? sf::Color(255, 247, 188, 92) : sf::Color(74, 225, 235, 70));
            window.draw(balance, sf::BlendAdd);
        }

        drawFlyingSword(sf::Vector2f(842.0f, 250.0f), clash, cycle, sf::Color(55, 213, 230, 235), false);
        drawFlyingSword(sf::Vector2f(1328.0f, 250.0f), clash, cycle, sf::Color(255, 191, 64, 235), true);

        float sparkFade = cycle > 0.55f ? (1.0f - cycle) / 0.45f : cycle / 0.55f;
        sparkFade = clampFloat(sparkFade, 0.0f, 1.0f);
        for (int i = 0; i < 20; ++i)
        {
            float a = animationTime * 5.8f + i * 0.314159f;
            sf::RectangleShape spark(sf::Vector2f(28.0f + (i % 4) * 9.0f, 3.0f + (i % 2)));
            spark.setOrigin(2.0f, spark.getSize().y * 0.5f);
            spark.setPosition(clash.x + std::cos(a) * 10.0f, clash.y + std::sin(a) * 7.0f);
            spark.setRotation(a * 57.2958f);
            spark.setFillColor(i % 2 == 0 ? sf::Color(255, 244, 142, static_cast<sf::Uint8>(205 * sparkFade))
                : sf::Color(78, 225, 235, static_cast<sf::Uint8>(190 * sparkFade)));
            window.draw(spark, sf::BlendAdd);
        }

        for (int r = 0; r < 3; ++r)
        {
            sf::CircleShape ring(42.0f + r * 25.0f + std::sin(animationTime * 8.0f + r) * 4.0f);
            ring.setOrigin(ring.getRadius(), ring.getRadius());
            ring.setPosition(clash);
            ring.setFillColor(sf::Color::Transparent);
            ring.setOutlineThickness(4.0f - r);
            ring.setOutlineColor(sf::Color(255, 255, 230, static_cast<sf::Uint8>((150 - r * 34) * sparkFade)));
            window.draw(ring, sf::BlendAdd);
        }
    }
    else
    {
        bool xWinner = currentTurn == CellState::X;
        sf::Vector2f winner = xWinner ? p1 : p2;
        sf::Vector2f loser = xWinner ? p2 : p1;
        sf::Color swordColor = xWinner ? sf::Color(60, 220, 235, 238) : sf::Color(255, 190, 64, 238);
        if (resultKind == "lose") swordColor = sf::Color(189, 116, 255, 238);

        for (int r = 0; r < 4; ++r)
        {
            float radius = 72.0f + r * 22.0f + std::sin(animationTime * 4.5f + r) * 4.0f;
            sf::CircleShape halo(radius);
            halo.setOrigin(radius, radius);
            halo.setPosition(winner);
            halo.setScale(1.0f, 0.78f);
            halo.setFillColor(sf::Color::Transparent);
            halo.setOutlineThickness(r == 0 ? 5.0f : 2.0f);
            sf::Color haloColor = r % 2 == 0 ? swordColor : sf::Color(255, 235, 142, 160);
            haloColor.a = static_cast<sf::Uint8>(118 - r * 21);
            halo.setOutlineColor(haloColor);
            window.draw(halo, sf::BlendAdd);
        }

        for (int i = 0; i < 10; ++i)
        {
            float a = -0.75f + i * 0.166f + std::sin(animationTime * 1.4f + i) * 0.05f;
            sf::RectangleShape ray(sf::Vector2f(118.0f + (i % 3) * 24.0f, 3.0f));
            ray.setOrigin(2.0f, 1.5f);
            ray.setPosition(winner);
            ray.setRotation(a * 57.2958f + (xWinner ? 0.0f : 180.0f));
            ray.setFillColor(sf::Color(swordColor.r, swordColor.g, swordColor.b, 52));
            window.draw(ray, sf::BlendAdd);
        }

        drawFlyingSword(winner, loser, cycle, swordColor, !xWinner);

        float hit = clampFloat((cycle - 0.50f) / 0.32f, 0.0f, 1.0f);
        float fade = 1.0f - clampFloat((cycle - 0.70f) / 0.30f, 0.0f, 1.0f);
        if (hit > 0.0f)
        {
            for (int k = 0; k < 3; ++k)
            {
                sf::RectangleShape cut(sf::Vector2f(162.0f - k * 24.0f, 7.0f + k));
                cut.setOrigin(cut.getSize().x * 0.5f, cut.getSize().y * 0.5f);
                cut.setPosition(loser.x + (k - 1) * 6.0f, loser.y + (k - 1) * 14.0f);
                cut.setRotation((xWinner ? -24.0f : 204.0f) + k * 15.0f);
                cut.setFillColor(k == 1 ? sf::Color(255, 255, 226, static_cast<sf::Uint8>(230 * hit * fade))
                    : sf::Color(danger.r, danger.g, danger.b, static_cast<sf::Uint8>(178 * hit * fade)));
                window.draw(cut, sf::BlendAdd);
            }

            for (int i = 0; i < 18; ++i)
            {
                float a = animationTime * 7.4f + i * 0.349066f;
                float dist = 22.0f + hit * (52.0f + (i % 4) * 11.0f);
                sf::CircleShape chip(4.0f + (i % 3) * 1.5f, i % 2 == 0 ? 4 : 3);
                chip.setOrigin(chip.getRadius(), chip.getRadius());
                chip.setPosition(loser.x + std::cos(a) * dist, loser.y + std::sin(a) * dist * 0.72f);
                chip.setRotation(a * 57.2958f);
                sf::Color chipColor = i % 2 == 0 ? sf::Color(255, 238, 152, static_cast<sf::Uint8>(205 * fade))
                    : sf::Color(danger.r, danger.g, danger.b, static_cast<sf::Uint8>(180 * fade));
                chip.setFillColor(chipColor);
                window.draw(chip, sf::BlendAdd);
            }

            sf::CircleShape shock(42.0f + hit * 72.0f);
            shock.setOrigin(shock.getRadius(), shock.getRadius());
            shock.setPosition(loser);
            shock.setScale(1.0f, 0.62f);
            shock.setFillColor(sf::Color::Transparent);
            shock.setOutlineThickness(5.0f * fade);
            shock.setOutlineColor(sf::Color(255, 248, 215, static_cast<sf::Uint8>(170 * fade)));
            window.draw(shock, sf::BlendAdd);
        }
    }

    window.setView(previousView);
}

void GameApp::drawFlyingSword(const sf::Vector2f& start, const sf::Vector2f& end,
    float progress, sf::Color color, bool reverseBlade)
{
    progress = clampFloat(progress, 0.0f, 1.0f);
    float eased = progress * progress * (3.0f - 2.0f * progress);
    sf::Vector2f delta(end.x - start.x, end.y - start.y);
    float angle = std::atan2(delta.y, delta.x) * 57.2958f;
    sf::Vector2f pos(start.x + delta.x * eased, start.y + delta.y * eased);
    float rad = angle * 0.0174533f;
    sf::Vector2f dir(std::cos(rad), std::sin(rad));
    sf::Vector2f normal(-dir.y, dir.x);

    float trailFade = 1.0f - std::abs(progress - 0.5f) * 1.3f;
    trailFade = clampFloat(trailFade, 0.0f, 1.0f);
    for (int i = 0; i < 5; ++i)
    {
        sf::ConvexShape trail(4);
        float length = 136.0f + i * 36.0f;
        float width = 28.0f - i * 3.5f;
        trail.setPoint(0, sf::Vector2f(-length, -width * 0.50f));
        trail.setPoint(1, sf::Vector2f(28.0f, -width * 0.18f));
        trail.setPoint(2, sf::Vector2f(36.0f, width * 0.18f));
        trail.setPoint(3, sf::Vector2f(-length, width * 0.50f));
        trail.setPosition(pos - dir * (18.0f + i * 7.0f));
        trail.setRotation(angle);
        trail.setFillColor(sf::Color(color.r, color.g, color.b,
            static_cast<sf::Uint8>((82 - i * 12) * trailFade)));
        window.draw(trail, sf::BlendAdd);
    }

    for (int i = 0; i < 7; ++i)
    {
        float back = 38.0f + i * 24.0f;
        float side = (reverseBlade ? -1.0f : 1.0f) * ((i % 2 == 0) ? 1.0f : -1.0f);
        sf::CircleShape spark(3.0f + (i % 3), 4);
        spark.setOrigin(spark.getRadius(), spark.getRadius());
        spark.setPosition(pos - dir * back + normal * side * (10.0f + i * 2.5f));
        spark.setRotation(angle + i * 35.0f);
        spark.setFillColor(i % 2 == 0 ? sf::Color(255, 255, 224, static_cast<sf::Uint8>(150 * trailFade))
            : sf::Color(color.r, color.g, color.b, static_cast<sf::Uint8>(165 * trailFade)));
        window.draw(spark, sf::BlendAdd);
    }

    sf::RectangleShape glow(sf::Vector2f(142.0f, 24.0f));
    glow.setOrigin(34.0f, 12.0f);
    glow.setPosition(pos);
    glow.setRotation(angle);
    glow.setFillColor(sf::Color(color.r, color.g, color.b, static_cast<sf::Uint8>(88 * trailFade)));
    window.draw(glow, sf::BlendAdd);

    sf::ConvexShape blade(5);
    blade.setPoint(0, sf::Vector2f(-26.0f, -6.0f));
    blade.setPoint(1, sf::Vector2f(76.0f, -8.0f));
    blade.setPoint(2, sf::Vector2f(118.0f, 0.0f));
    blade.setPoint(3, sf::Vector2f(76.0f, 8.0f));
    blade.setPoint(4, sf::Vector2f(-26.0f, 6.0f));
    blade.setPosition(pos);
    blade.setRotation(angle);
    blade.setFillColor(sf::Color(245, 255, 250, 235));
    window.draw(blade);

    sf::RectangleShape core(sf::Vector2f(104.0f, 4.0f));
    core.setOrigin(22.0f, 2.0f);
    core.setPosition(pos + normal * (reverseBlade ? -2.0f : 2.0f));
    core.setRotation(angle);
    core.setFillColor(sf::Color(color.r, color.g, color.b, 210));
    window.draw(core, sf::BlendAdd);

    sf::RectangleShape edge(sf::Vector2f(86.0f, 2.0f));
    edge.setOrigin(14.0f, 1.0f);
    edge.setPosition(pos - normal * (reverseBlade ? -4.0f : 4.0f));
    edge.setRotation(angle);
    edge.setFillColor(sf::Color(255, 255, 255, 232));
    window.draw(edge, sf::BlendAdd);

    sf::RectangleShape guard(sf::Vector2f(42.0f, 8.0f));
    guard.setOrigin(21.0f, 4.0f);
    guard.setPosition(pos - dir * 31.0f);
    guard.setRotation(angle + 90.0f);
    guard.setFillColor(sf::Color(44, 65, 82, 235));
    window.draw(guard);

    sf::RectangleShape guardLight(sf::Vector2f(34.0f, 3.0f));
    guardLight.setOrigin(17.0f, 1.5f);
    guardLight.setPosition(pos - dir * 31.0f);
    guardLight.setRotation(angle + 90.0f);
    guardLight.setFillColor(sf::Color(255, 238, 156, 210));
    window.draw(guardLight, sf::BlendAdd);

    sf::RectangleShape handle(sf::Vector2f(44.0f, 8.0f));
    handle.setOrigin(0.0f, 4.0f);
    handle.setPosition(pos - dir * 36.0f);
    handle.setRotation(angle + 180.0f);
    handle.setFillColor(sf::Color(42, 37, 47, 236));
    window.draw(handle);

    sf::CircleShape pommel(7.0f, 16);
    pommel.setOrigin(7.0f, 7.0f);
    pommel.setPosition(pos - dir * 82.0f);
    pommel.setFillColor(sf::Color(color.r, color.g, color.b, 220));
    pommel.setOutlineThickness(2.0f);
    pommel.setOutlineColor(sf::Color(255, 247, 205, 210));
    window.draw(pommel, sf::BlendAdd);
}

void GameApp::drawPlayerPanel(float x, float y, const std::string& name,
    CellState mark, int avatarIndex, bool active, bool botAvatar)
{
    sf::RectangleShape panel(sf::Vector2f(250, 280));
    panel.setPosition(x, y);
    panel.setFillColor(active ? sf::Color(255, 226, 139, 224) : sf::Color(224, 235, 220, 190));
    panel.setOutlineColor(active ? sf::Color(61, 211, 225) : sf::Color(126, 92, 45));
    panel.setOutlineThickness(active ? 5.0f : 2.0f);
    window.draw(panel);
    if (active)
    {
        sf::CircleShape halo(122.0f + std::sin(animationTime * 4.0f) * 5.0f);
        halo.setOrigin(halo.getRadius(), halo.getRadius());
        halo.setPosition(x + 125.0f, y + 104.0f);
        halo.setFillColor(sf::Color::Transparent);
        halo.setOutlineThickness(5.0f);
        halo.setOutlineColor(sf::Color(255, 232, 95, 145));
        window.draw(halo);
    }
    sf::FloatRect imageRect(x + 36, y + 22, 178, 160);
    if (botAvatar && hasTamma) drawSpriteContain(window, tammaTexture, imageRect);
    else if (avatarIndex >= 0 && avatarIndex < static_cast<int>(avatars.size()))
        drawSpriteContain(window, avatars[avatarIndex].texture, imageRect);
    drawTextBox(name, 24, sf::FloatRect(x + 12, y + 194, 226, 40), sf::Color(53, 68, 74));

    sf::Vector2f markCenter(x + 125.0f, y + 246.0f);
    sf::CircleShape markAura(30.0f + (active ? std::sin(animationTime * 6.0f) * 3.0f : 0.0f), 72);
    markAura.setOrigin(markAura.getRadius(), markAura.getRadius());
    markAura.setPosition(markCenter);
    markAura.setFillColor(mark == CellState::X ? sf::Color(59, 211, 230, active ? 84 : 48)
        : sf::Color(255, 196, 76, active ? 92 : 52));
    markAura.setOutlineThickness(2.0f);
    markAura.setOutlineColor(mark == CellState::X ? sf::Color(24, 135, 166, 180)
        : sf::Color(189, 109, 28, 180));
    window.draw(markAura);

    if (mark == CellState::X)
    {
        for (int i = 0; i < 2; ++i)
        {
            sf::RectangleShape blade(sf::Vector2f(46.0f, 7.0f));
            blade.setOrigin(23.0f, 3.5f);
            blade.setPosition(markCenter);
            blade.setRotation(i == 0 ? 45.0f : -45.0f);
            blade.setFillColor(sf::Color(238, 255, 247, 235));
            window.draw(blade);
        }
    }
    else
    {
        sf::CircleShape ring(20.0f, 72);
        ring.setOrigin(20.0f, 20.0f);
        ring.setPosition(markCenter);
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineThickness(6.0f);
        ring.setOutlineColor(sf::Color(255, 248, 205, 235));
        window.draw(ring);
    }

    sf::Text markGlyph(utf8(mark == CellState::X ? "X" : "O"), font, 30);
    markGlyph.setFillColor(mark == CellState::X ? sf::Color(31, 125, 154) : sf::Color(183, 95, 25));
    markGlyph.setOutlineColor(sf::Color(255, 255, 242, 210));
    markGlyph.setOutlineThickness(2.0f);
    sf::FloatRect glyphBounds = markGlyph.getLocalBounds();
    markGlyph.setOrigin(glyphBounds.left + glyphBounds.width * 0.5f,
        glyphBounds.top + glyphBounds.height * 0.5f);
    markGlyph.setPosition(markCenter);
    window.draw(markGlyph);
}

void GameApp::clearUi()
{
    buttons.clear();
    sliders.clear();
}

void GameApp::addButton(const std::string& id, const std::string& label,
    const sf::FloatRect& rect)
{
    Button b;
    b.id = id;
    b.label = label;
    b.rect = rect;
    buttons.push_back(b);
}

void GameApp::addSlider(const std::string& id, const std::string& label,
    const sf::FloatRect& track, float value)
{
    Slider s;
    s.id = id;
    s.track = track;
    s.value = value;
    sliders.push_back(s);
}

void GameApp::drawText(const std::string& value, unsigned int size,
    sf::Vector2f pos, sf::Color color, bool centered)
{
    sf::Text sfText(utf8(value), font, size);
    sfText.setFillColor(color);
    int brightness = (static_cast<int>(color.r) + static_cast<int>(color.g) + static_cast<int>(color.b)) / 3;
    sfText.setOutlineColor(brightness > 150 ? sf::Color(18, 26, 38, 210) : sf::Color(255, 255, 255, 190));
    sfText.setOutlineThickness(size <= 30 ? 2.0f : 2.5f);
    if (centered)
    {
        sf::FloatRect bounds = sfText.getLocalBounds();
        sfText.setOrigin(bounds.left + bounds.width * 0.5f, bounds.top + bounds.height * 0.5f);
    }
    sfText.setPosition(pos);
    window.draw(sfText);
}

void GameApp::drawTextBox(const std::string& value, unsigned int size,
    const sf::FloatRect& rect, sf::Color color)
{
    sf::Text sfText(utf8(value), font, size);
    sfText.setFillColor(color);
    int brightness = (static_cast<int>(color.r) + static_cast<int>(color.g) + static_cast<int>(color.b)) / 3;
    sfText.setOutlineColor(brightness > 150 ? sf::Color(18, 26, 38, 185) : sf::Color(255, 255, 255, 160));
    sfText.setOutlineThickness(size <= 28 ? 1.6f : 2.0f);
    setTextCenter(sfText, rect);
    window.draw(sfText);
}

void GameApp::drawSpriteCover(sf::RenderTarget& target, sf::Texture& texture,
    const sf::FloatRect& rect, sf::Color color)
{
    sf::Sprite sprite(texture);
    sf::Vector2u size = texture.getSize();
    if (size.x == 0 || size.y == 0) return;
    float sx = rect.width / static_cast<float>(size.x);
    float sy = rect.height / static_cast<float>(size.y);
    float scale = sx > sy ? sx : sy;
    sprite.setScale(scale, scale);
    sprite.setPosition(rect.left + (rect.width - size.x * scale) * 0.5f,
        rect.top + (rect.height - size.y * scale) * 0.5f);
    sprite.setColor(color);
    target.draw(sprite);
}

void GameApp::drawSpriteContain(sf::RenderTarget& target, sf::Texture& texture,
    const sf::FloatRect& rect, sf::Color color)
{
    sf::Sprite sprite(texture);
    sf::Vector2u size = texture.getSize();
    if (size.x == 0 || size.y == 0) return;
    float sx = rect.width / static_cast<float>(size.x);
    float sy = rect.height / static_cast<float>(size.y);
    float scale = sx < sy ? sx : sy;
    sprite.setScale(scale, scale);
    sprite.setPosition(rect.left + (rect.width - size.x * scale) * 0.5f,
        rect.top + (rect.height - size.y * scale) * 0.5f);
    sprite.setColor(color);
    target.draw(sprite);
}

void GameApp::setTextCenter(sf::Text& sfText, const sf::FloatRect& rect)
{
    sf::FloatRect bounds = sfText.getLocalBounds();
    float scale = 1.0f;
    if (bounds.width > rect.width - 8.0f && bounds.width > 0)
        scale = (rect.width - 8.0f) / bounds.width;
    sfText.setScale(scale, scale);
    bounds = sfText.getLocalBounds();
    sfText.setOrigin(bounds.left + bounds.width * 0.5f, bounds.top + bounds.height * 0.5f);
    sfText.setPosition(rect.left + rect.width * 0.5f, rect.top + rect.height * 0.5f);
}

void GameApp::beginModeSelect(bool botMode, BotLevel level)
{
    vsBot = botMode;
    botLevel = level;
    player1Name = tr("player.one");
    player2Name = vsBot ? tr("player.bot") : tr("player.two");
    focusedNameField = 0;
    selectingPlayer = 1;
    selectedAvatar = std::min(player1Avatar, std::max(0, static_cast<int>(avatars.size()) - 1));
    screen = Screen::Names;
}

void GameApp::startFateOpening()
{
    fateScoreX = std::rand() % 1000;
    do
    {
        fateScoreO = std::rand() % 1000;
    } while (fateScoreO == fateScoreX);
    startingTurn = fateScoreX > fateScoreO ? CellState::X : CellState::O;
    fateTimer = 0.0f;
    fateOpening = true;
    fateXRevealed = false;
    fateORevealed = false;
    chessSound.play();
}

void GameApp::beginMatch()
{
    board.reset();
    winningCells.clear();
    moveEffects.clear();
    undoEffects.clear();
    currentTurn = startingTurn;
    botThinking = vsBot && currentTurn == CellState::O;
    botThinkTimer = 0.0f;
    matchFinished = false;
    loadedElapsedSeconds = 0;
    matchClock.restart();
    activeSaveName = createAutoSaveName();
    persistCurrentGame("playing");
    screen = Screen::Playing;
}

void GameApp::makeMove(int row, int col)
{
    if (matchFinished) return;
    if (vsBot && currentTurn == CellState::O) return;
    if (!board.placeMove(row, col, currentTurn)) return;
    chessSound.play();
    BoardMove move(row, col, currentTurn);
    addMoveEffect(move);
    checkAfterMove(move);
    if (!matchFinished)
    {
        currentTurn = opposite(currentTurn);
        if (vsBot && currentTurn == CellState::O)
        {
            botThinking = true;
            botThinkTimer = 0.0f;
        }
        persistCurrentGame("playing");
    }
}

void GameApp::performBotMove()
{
    botThinking = false;
    BoardMove move = bot.chooseMove(board, botLevel, CellState::O, CellState::X);
    if (board.placeMove(move.row, move.col, CellState::O))
    {
        chessSound.play();
        addMoveEffect(move);
        checkAfterMove(move);
        if (!matchFinished) currentTurn = CellState::X;
        if (!matchFinished) persistCurrentGame("playing");
    }
}

void GameApp::checkAfterMove(const BoardMove& move)
{
    if (board.checkWin(move.row, move.col, move.mark, &winningCells))
    {
        killSound.play();
        if (move.mark == CellState::X)
            finishMatch("win", tr("result.win") + " - " + player1Name);
        else
            finishMatch(vsBot ? "lose" : "win", (vsBot ? tr("result.lose") : tr("result.win")) + " - " + player2Name);
    }
    else if (board.isFull())
    {
        finishMatch("draw", tr("result.draw"));
    }
}

void GameApp::finishMatch(const std::string& kind, const std::string& message)
{
    matchFinished = true;
    resultKind = kind;
    resultText = message;
    if (!activeSaveName.empty())
    {
        persistCurrentGame(kind);
    }
    screen = Screen::Result;
}

void GameApp::undoMove()
{
    if (matchFinished) return;
    if (vsBot)
    {
        if (currentTurn == CellState::X && board.moveCount() > 0)
        {
            const std::vector<BoardMove>& moves = board.getMoves();
            if (!moves.empty()) addUndoEffect(moves.back());
            if (moves.size() >= 2) addUndoEffect(moves[moves.size() - 2]);
            board.undoBotPair();
            currentTurn = CellState::X;
            botThinking = false;
            persistCurrentGame("playing");
        }
    }
    else
    {
        const std::vector<BoardMove>& moves = board.getMoves();
        if (!moves.empty()) addUndoEffect(moves.back());
        if (board.undoLast())
        {
            currentTurn = opposite(currentTurn);
            persistCurrentGame("playing");
        }
    }
    winningCells.clear();
}

void GameApp::addMoveEffect(const BoardMove& move)
{
    MoveEffect effect;
    effect.move = move;
    effect.age = 0.0f;
    effect.duration = 1.15f;
    effect.variant = (move.row * 31 + move.col * 17 + static_cast<int>(move.mark)) % 5;
    effect.neighborPower = countFriendlyNeighbors(move.row, move.col, move.mark);
    moveEffects.push_back(effect);
}

void GameApp::addUndoEffect(const BoardMove& move)
{
    if (!board.isInside(move.row, move.col)) return;
    UndoEffect effect;
    effect.row = move.row;
    effect.col = move.col;
    effect.age = 0.0f;
    effect.duration = 0.9f;
    undoEffects.push_back(effect);
}

int GameApp::countFriendlyNeighbors(int row, int col, CellState mark) const
{
    int total = 0;
    for (int dr = -1; dr <= 1; ++dr)
    {
        for (int dc = -1; dc <= 1; ++dc)
        {
            if (dr == 0 && dc == 0) continue;
            int r = row + dr;
            int c = col + dc;
            if (board.isInside(r, c) && board.getCell(r, c) == mark)
                ++total;
        }
    }
    return total;
}

void GameApp::persistCurrentGame(const std::string& status)
{
    if (activeSaveName.empty()) return;
    SaveGameData data = collectSaveData(status);
    saveManager.saveGame(activeSaveName, data, true);
}

std::string GameApp::createAutoSaveName() const
{
    time_t now = time(0);
    tm localTime;
    localtime_s(&localTime, &now);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "thien_co_%Y%m%d_%H%M%S", &localTime);
    std::ostringstream out;
    out << buffer << "_" << (std::rand() % 1000);
    return out.str();
}

void GameApp::openSaveDialog()
{
    if (screen != Screen::Playing) return;
    dialogMode = DialogMode::Save;
    dialogInput.clear();
    dialogMessage.clear();
}

void GameApp::openLoadDialog()
{
    previousScreen = screen;
    dialogMode = DialogMode::Load;
    dialogInput.clear();
    dialogMessage.clear();
}

void GameApp::openHistory()
{
    previousScreen = screen;
    dialogMode = DialogMode::None;
    dialogInput.clear();
    dialogMessage.clear();
    historyMessage.clear();
    historyRecords = saveManager.getAllRecords();
    selectedHistory = historyRecords.empty() ? -1 : 0;
    screen = Screen::History;
}

void GameApp::acceptSaveName(bool overwrite)
{
    if (dialogInput.empty() && pendingSaveName.empty())
    {
        dialogMessage = "Empty name";
        return;
    }
    std::string name = pendingSaveName.empty() ? dialogInput : pendingSaveName;
    if (!overwrite && saveManager.exists(name))
    {
        pendingSaveName = name;
        dialogMode = DialogMode::Overwrite;
        return;
    }
    SaveGameData data = collectSaveData("playing");
    if (saveManager.saveGame(name, data, overwrite))
    {
        activeSaveName = saveManager.normalizeName(name);
        dialogMode = DialogMode::None;
        dialogInput.clear();
        pendingSaveName.clear();
        dialogMessage.clear();
    }
    else
    {
        dialogMessage = "Save failed";
        dialogMode = DialogMode::Save;
    }
}

void GameApp::acceptLoadName()
{
    SaveGameData data;
    std::string error;
    if (!saveManager.loadGame(dialogInput, data, true, &error))
    {
        dialogMessage = tr("dialog.error");
        return;
    }
    activeSaveName = saveManager.normalizeName(dialogInput);
    applySaveData(data);
    dialogMode = DialogMode::None;
    dialogInput.clear();
    dialogMessage.clear();
}

void GameApp::loadSelectedHistory()
{
    if (selectedHistory < 0 || selectedHistory >= static_cast<int>(historyRecords.size()))
    {
        historyMessage = tr("history.empty");
        return;
    }

    const SaveRecord& rec = historyRecords[selectedHistory];
    if (rec.status != "playing")
    {
        historyMessage = tr("history.locked");
        return;
    }

    SaveGameData data;
    std::string error;
    if (!saveManager.loadGame(rec.fileName, data, true, &error))
    {
        historyMessage = tr("dialog.error");
        historyRecords = saveManager.getAllRecords();
        if (selectedHistory >= static_cast<int>(historyRecords.size()))
            selectedHistory = static_cast<int>(historyRecords.size()) - 1;
        return;
    }

    activeSaveName = rec.fileName;
    historyMessage.clear();
    applySaveData(data);
}

SaveGameData GameApp::collectSaveData(const std::string& status) const
{
    SaveGameData data;
    data.mode = vsBot ? "bot" : "two";
    data.botLevel = botLevel;
    data.player1Name = player1Name;
    data.player2Name = player2Name;
    data.player1Avatar = player1Avatar;
    data.player2Avatar = player2Avatar;
    data.currentTurn = currentTurn;
    data.elapsedSeconds = elapsedSeconds();
    data.status = status;
    data.grid = board.serializeGrid();
    data.moves = board.getMoves();
    return data;
}

void GameApp::applySaveData(const SaveGameData& data)
{
    vsBot = data.mode == "bot";
    botLevel = data.botLevel;
    player1Name = data.player1Name;
    player2Name = vsBot ? tr("player.bot") : data.player2Name;
    player1Avatar = data.player1Avatar;
    player2Avatar = data.player2Avatar;
    currentTurn = data.currentTurn;
    loadedElapsedSeconds = data.elapsedSeconds;
    board.loadGrid(data.grid, data.moves);
    winningCells.clear();
    moveEffects.clear();
    undoEffects.clear();
    matchClock.restart();
    botThinking = vsBot && currentTurn == CellState::O;
    botThinkTimer = 0.0f;
    matchFinished = false;
    screen = Screen::Playing;
}

int GameApp::elapsedSeconds() const
{
    if (screen == Screen::Playing || screen == Screen::Result)
        return loadedElapsedSeconds + static_cast<int>(matchClock.getElapsedTime().asSeconds());
    return loadedElapsedSeconds;
}

void GameApp::returnToMenu()
{
    if (!activeSaveName.empty())
        persistCurrentGame(matchFinished ? resultKind : "playing");
    dialogMode = DialogMode::None;
    matchFinished = false;
    botThinking = false;
    screen = Screen::Menu;
}

void GameApp::resetParticles()
{
    particles.clear();
    for (int i = 0; i < 90; ++i)
    {
        Particle p;
        p.pos = sf::Vector2f(static_cast<float>(std::rand() % WindowW),
            static_cast<float>(std::rand() % WindowH));
        p.vel = sf::Vector2f(8.0f + static_cast<float>(std::rand() % 24),
            -8.0f + static_cast<float>(std::rand() % 18));
        p.radius = 1.5f + static_cast<float>(std::rand() % 35) / 10.0f;
        p.maxLife = 4.0f + static_cast<float>(std::rand() % 500) / 100.0f;
        p.life = p.maxLife;
        p.color = (i % 3 == 0) ? sf::Color(255, 177, 198, 120) : sf::Color(255, 255, 236, 150);
        particles.push_back(p);
    }
}

void GameApp::updateParticles(float dt)
{
    for (size_t i = 0; i < particles.size(); ++i)
    {
        particles[i].pos += particles[i].vel * dt;
        particles[i].life -= dt;
        if (particles[i].life <= 0.0f || particles[i].pos.x > WindowW + 30 || particles[i].pos.y < -30)
        {
            particles[i].pos = sf::Vector2f(-20.0f, static_cast<float>(std::rand() % WindowH));
            particles[i].life = particles[i].maxLife;
        }
    }
}

void GameApp::updateEffects(float dt)
{
    for (size_t i = 0; i < moveEffects.size();)
    {
        moveEffects[i].age += dt;
        if (moveEffects[i].age >= moveEffects[i].duration)
            moveEffects.erase(moveEffects.begin() + i);
        else
            ++i;
    }

    for (size_t i = 0; i < undoEffects.size();)
    {
        undoEffects[i].age += dt;
        if (undoEffects[i].age >= undoEffects[i].duration)
            undoEffects.erase(undoEffects.begin() + i);
        else
            ++i;
    }
}
