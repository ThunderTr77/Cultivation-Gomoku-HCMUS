#pragma once

#include <SFML/Graphics.hpp>

#include <mfidl.h>
#include <mfreadwrite.h>
#include <string>
#include <vector>

class VideoPlayer
{
public:
    VideoPlayer();
    ~VideoPlayer();

    bool open(const std::string& filePath);
    bool openFrameSequence(const std::string& directory);
    void update(float dt);
    bool isReady() const;
    void drawCover(sf::RenderTarget& target, const sf::FloatRect& bounds);

private:
    IMFSourceReader* reader;
    sf::Texture texture;
    sf::Sprite sprite;
    std::vector<sf::Uint8> rgbaPixels;
    std::vector<sf::Image> fallbackFrames;
    unsigned int width;
    unsigned int height;
    int stride;
    float accumulator;
    size_t frameIndex;
    bool ready;
    bool mediaStarted;
    bool comStarted;
    bool frameSequenceMode;

    bool readFrame();
    void restart();
    void releaseReader();
    std::wstring widenPath(const std::string& value) const;
};
