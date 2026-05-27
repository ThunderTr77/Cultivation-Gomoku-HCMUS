#pragma once

#include "BoardModel.h"
#include "BotEngine.h"
#include "SaveManager.h"
#include "VideoPlayer.h"

#include <SFML/Audio.hpp>
#include <SFML/Graphics.hpp>

#include <map>
#include <string>
#include <vector>

class GameApp
{
public:
    GameApp();
    int run();

private:
    enum class Screen
    {
        Menu,
        Mode,
        Names,
        Avatar,
        Fate,
        Playing,
        History,
        Settings,
        Guide,
        Info,
        Result
    };

    enum class DialogMode
    {
        None,
        Save,
        Load,
        Overwrite
    };

    struct Button
    {
        std::string id;
        std::string label;
        sf::FloatRect rect;
    };

    struct Slider
    {
        std::string id;
        sf::FloatRect track;
        float value;
    };

    struct AvatarItem
    {
        std::string name;
        std::string path;
        sf::Texture texture;
    };

    struct Particle
    {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float radius;
        float life;
        float maxLife;
        sf::Color color;
    };

    struct MoveEffect
    {
        BoardMove move;
        float age;
        float duration;
        int variant;
        int neighborPower;
    };

    struct UndoEffect
    {
        int row;
        int col;
        float age;
        float duration;
    };

    sf::RenderWindow window;
    sf::Font font;
    std::map<std::string, sf::Texture> backgrounds;
    std::vector<AvatarItem> avatars;
    sf::Texture tammaTexture;
    sf::Texture cardTexture;
    bool hasTamma;
    bool hasCardTexture;

    sf::Music music;
    sf::SoundBuffer chessBuffer;
    sf::SoundBuffer killBuffer;
    sf::Sound chessSound;
    sf::Sound killSound;
    float musicVolume;
    float sfxVolume;

    VideoPlayer menuVideo;
    BoardModel board;
    BotEngine bot;
    SaveManager saveManager;

    Screen screen;
    Screen previousScreen;
    DialogMode dialogMode;
    std::vector<Button> buttons;
    std::vector<Slider> sliders;
    std::vector<Particle> particles;
    std::vector<MoveEffect> moveEffects;
    std::vector<UndoEffect> undoEffects;
    std::vector<std::pair<int, int> > winningCells;

    std::map<std::string, std::string> text;
    std::string language;
    std::string dialogInput;
    std::string pendingSaveName;
    std::string dialogMessage;
    std::string draggingSlider;
    std::string historyMessage;
    std::string activeSaveName;
    sf::Vector2f mousePos;
    std::vector<SaveRecord> historyRecords;
    int selectedHistory;

    bool vsBot;
    BotLevel botLevel;
    CellState currentTurn;
    std::string player1Name;
    std::string player2Name;
    int player1Avatar;
    int player2Avatar;
    int selectedAvatar;
    int selectingPlayer;
    int focusedNameField;
    int fateScoreX;
    int fateScoreO;
    float fateTimer;
    bool fateOpening;
    bool fateXRevealed;
    bool fateORevealed;
    CellState startingTurn;
    bool botThinking;
    float botThinkTimer;
    bool matchFinished;
    std::string resultText;
    std::string resultKind;
    sf::Clock matchClock;
    int loadedElapsedSeconds;
    float animationTime;

    void initialize();
    void configureWorkingDirectory();
    void loadSettings();
    void saveSettings();
    void loadLocalization(const std::string& lang);
    std::string tr(const std::string& key) const;

    void loadAssets();
    void loadBackground(const std::string& key, const std::string& path);
    void loadAvatars();
    void startMusic();
    void applyVolumes();

    void handleEvent(const sf::Event& event);
    void handleButton(const std::string& id);
    void handleKeyPressed(const sf::Event::KeyEvent& key);
    void handleDialogEvent(const sf::Event& event);
    void update(float dt);
    void draw();

    void drawBackground(const std::string& key);
    void drawMenu();
    void drawMode();
    void drawNames();
    void drawAvatar();
    void drawFate();
    void drawPlaying();
    void drawHistory();
    void drawSettings();
    void drawGuide();
    void drawInfo();
    void drawResult();
    void drawDialog();
    void drawParticles(float alphaScale);
    void drawButtons();
    void drawButton(const Button& button);
    void drawSlider(const Slider& slider, const std::string& label);
    void drawBoard();
    void drawMoveEffect(const MoveEffect& effect, float cell);
    void drawUndoEffect(const UndoEffect& effect, float cell);
    void drawResultEffect();
    void drawFlyingSword(const sf::Vector2f& start, const sf::Vector2f& end,
        float progress, sf::Color color, bool reverseBlade);
    void drawFateCard(const sf::FloatRect& rect, const std::string& owner,
        int score, bool revealed, float revealProgress, sf::Color accent);
    void drawPlayerPanel(float x, float y, const std::string& name,
        CellState mark, int avatarIndex, bool active, bool botAvatar);

    void clearUi();
    void addButton(const std::string& id, const std::string& label,
        const sf::FloatRect& rect);
    void addSlider(const std::string& id, const std::string& label,
        const sf::FloatRect& track, float value);
    void drawText(const std::string& value, unsigned int size,
        sf::Vector2f pos, sf::Color color, bool centered = false);
    void drawTextBox(const std::string& value, unsigned int size,
        const sf::FloatRect& rect, sf::Color color);
    void drawSpriteCover(sf::RenderTarget& target, sf::Texture& texture,
        const sf::FloatRect& rect, sf::Color color = sf::Color::White);
    void drawSpriteContain(sf::RenderTarget& target, sf::Texture& texture,
        const sf::FloatRect& rect, sf::Color color = sf::Color::White);
    void setTextCenter(sf::Text& sfText, const sf::FloatRect& rect);

    void beginModeSelect(bool botMode, BotLevel level);
    void startFateOpening();
    void beginMatch();
    void makeMove(int row, int col);
    void performBotMove();
    void checkAfterMove(const BoardMove& move);
    void finishMatch(const std::string& kind, const std::string& message);
    void undoMove();
    void addMoveEffect(const BoardMove& move);
    void addUndoEffect(const BoardMove& move);
    int countFriendlyNeighbors(int row, int col, CellState mark) const;
    void persistCurrentGame(const std::string& status);
    std::string createAutoSaveName() const;
    void openSaveDialog();
    void openLoadDialog();
    void openHistory();
    void acceptSaveName(bool overwrite);
    void acceptLoadName();
    void loadSelectedHistory();
    SaveGameData collectSaveData(const std::string& status) const;
    void applySaveData(const SaveGameData& data);
    int elapsedSeconds() const;
    void returnToMenu();

    void resetParticles();
    void updateParticles(float dt);
    void updateEffects(float dt);
};
