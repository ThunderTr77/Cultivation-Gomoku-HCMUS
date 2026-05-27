#include "VideoPlayer.h"

#include <algorithm>
#include <mfapi.h>
#include <mferror.h>
#include <propvarutil.h>
#include <windows.h>

VideoPlayer::VideoPlayer()
    : reader(0), width(0), height(0), stride(0), accumulator(0.0f),
    frameIndex(0), ready(false), mediaStarted(false), comStarted(false),
    frameSequenceMode(false)
{
}

VideoPlayer::~VideoPlayer()
{
    releaseReader();
    if (mediaStarted) MFShutdown();
    if (comStarted) CoUninitialize();
}

bool VideoPlayer::open(const std::string& filePath)
{
    releaseReader();
    ready = false;
    frameSequenceMode = false;
    fallbackFrames.clear();
    frameIndex = 0;
    accumulator = 0.0f;
    HRESULT coHr = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
    comStarted = SUCCEEDED(coHr);

    if (!mediaStarted)
    {
        if (FAILED(MFStartup(MF_VERSION))) return false;
        mediaStarted = true;
    }

    IMFAttributes* attributes = 0;
    if (FAILED(MFCreateAttributes(&attributes, 2))) return false;
    attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

    std::wstring wide = widenPath(filePath);
    DWORD fullLength = GetFullPathNameW(wide.c_str(), 0, 0, 0);
    if (fullLength > 0)
    {
        std::wstring fullPath(fullLength, L'\0');
        DWORD written = GetFullPathNameW(wide.c_str(), fullLength, &fullPath[0], 0);
        if (written > 0 && written < fullLength)
        {
            fullPath.resize(written);
            wide = fullPath;
        }
    }
    HRESULT readerHr = MFCreateSourceReaderFromURL(wide.c_str(), attributes, &reader);
    attributes->Release();
    if (FAILED(readerHr))
        return false;
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);

    IMFMediaType* desiredType = 0;
    if (FAILED(MFCreateMediaType(&desiredType))) return false;
    desiredType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    desiredType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    MFSetAttributeSize(desiredType, MF_MT_FRAME_SIZE, 1280, 720);
    HRESULT setTypeHr = reader->SetCurrentMediaType(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, desiredType);
    desiredType->Release();

    if (FAILED(setTypeHr))
    {
        desiredType = 0;
        if (FAILED(MFCreateMediaType(&desiredType))) return false;
        desiredType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        desiredType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        setTypeHr = reader->SetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, desiredType);
        desiredType->Release();
    }
    if (FAILED(setTypeHr)) return false;

    IMFMediaType* currentType = 0;
    if (FAILED(reader->GetCurrentMediaType(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &currentType)))
        return false;

    UINT32 w = 0;
    UINT32 h = 0;
    HRESULT sizeHr = MFGetAttributeSize(currentType, MF_MT_FRAME_SIZE, &w, &h);
    UINT32 strideValue = 0;
    stride = 0;
    if (SUCCEEDED(currentType->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideValue)))
        stride = static_cast<int>(strideValue);
    if (stride == 0)
    {
        LONG computedStride = 0;
        if (SUCCEEDED(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, w, &computedStride)))
            stride = computedStride;
    }
    currentType->Release();
    if (FAILED(sizeHr) || w == 0 || h == 0) return false;
    if (stride == 0) stride = static_cast<int>(w) * 4;

    width = w;
    height = h;
    if (!texture.create(width, height)) return false;
    rgbaPixels.assign(width * height * 4, 255);
    ready = false;
    for (int i = 0; i < 12 && !ready; ++i)
        ready = readFrame();
    sprite.setTexture(texture, true);
    return ready;
}

bool VideoPlayer::openFrameSequence(const std::string& directory)
{
    releaseReader();
    ready = false;
    frameSequenceMode = false;
    fallbackFrames.clear();
    frameIndex = 0;
    accumulator = 0.0f;

    std::string dir = directory;
    if (!dir.empty() && dir[dir.size() - 1] != '\\' && dir[dir.size() - 1] != '/')
        dir += "\\";

    std::vector<std::string> files;
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA((dir + "frame_*.jpg").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                files.push_back(dir + data.cFileName);
        } while (FindNextFileA(find, &data));
        FindClose(find);
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) return false;

    for (size_t i = 0; i < files.size(); ++i)
    {
        sf::Image image;
        if (!image.loadFromFile(files[i])) continue;
        sf::Vector2u size = image.getSize();
        if (size.x == 0 || size.y == 0) continue;
        if (fallbackFrames.empty())
        {
            width = size.x;
            height = size.y;
        }
        if (size.x == width && size.y == height)
            fallbackFrames.push_back(image);
    }

    if (fallbackFrames.empty()) return false;
    if (!texture.create(width, height)) return false;
    texture.update(fallbackFrames[0]);
    sprite.setTexture(texture, true);
    ready = true;
    frameSequenceMode = true;
    return true;
}

void VideoPlayer::update(float dt)
{
    if (!ready) return;
    accumulator += dt;
    if (frameSequenceMode)
    {
        const float frameStep = 1.0f / 12.0f;
        while (accumulator >= frameStep)
        {
            frameIndex = (frameIndex + 1) % fallbackFrames.size();
            texture.update(fallbackFrames[frameIndex]);
            accumulator -= frameStep;
        }
        return;
    }

    const float frameStep = 1.0f / 24.0f;
    if (accumulator >= frameStep)
    {
        if (!readFrame())
        {
            restart();
            readFrame();
        }
        accumulator = 0.0f;
    }
}

bool VideoPlayer::isReady() const
{
    return ready;
}

void VideoPlayer::drawCover(sf::RenderTarget& target, const sf::FloatRect& bounds)
{
    if (!ready) return;
    sf::Vector2u texSize = texture.getSize();
    if (texSize.x == 0 || texSize.y == 0) return;
    float sx = bounds.width / static_cast<float>(texSize.x);
    float sy = bounds.height / static_cast<float>(texSize.y);
    float scale = (sx > sy) ? sx : sy;
    sprite.setScale(scale, scale);
    sprite.setPosition(
        bounds.left + (bounds.width - texSize.x * scale) * 0.5f,
        bounds.top + (bounds.height - texSize.y * scale) * 0.5f);
    target.draw(sprite);
}

bool VideoPlayer::readFrame()
{
    if (!reader) return false;
    IMFSample* sample = 0;
    DWORD flags = 0;
    LONGLONG timestamp = 0;
    for (int attempt = 0; attempt < 8 && !sample; ++attempt)
    {
        HRESULT hr = reader->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM),
            0, 0, &flags, &timestamp, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
        {
            if (sample) sample->Release();
            return false;
        }
    }
    if (!sample) return false;

    IMFMediaBuffer* buffer = 0;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
    {
        sample->Release();
        return false;
    }

    BYTE* data = 0;
    DWORD maxLength = 0;
    DWORD currentLength = 0;
    if (SUCCEEDED(buffer->Lock(&data, &maxLength, &currentLength)))
    {
        size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
        size_t sourceStride = static_cast<size_t>(stride < 0 ? -stride : stride);
        size_t requiredBytes = (static_cast<size_t>(height) - 1) * sourceStride + static_cast<size_t>(width) * 4;
        if (currentLength >= requiredBytes && sourceStride >= static_cast<size_t>(width) * 4)
        {
            for (size_t y = 0; y < static_cast<size_t>(height); ++y)
            {
                const BYTE* row = stride >= 0
                    ? data + y * sourceStride
                    : data + (static_cast<size_t>(height) - 1 - y) * sourceStride;
                for (size_t x = 0; x < static_cast<size_t>(width); ++x)
                {
                    size_t src = x * 4;
                    size_t dst = (y * static_cast<size_t>(width) + x) * 4;
                    rgbaPixels[dst + 0] = row[src + 2];
                    rgbaPixels[dst + 1] = row[src + 1];
                    rgbaPixels[dst + 2] = row[src + 0];
                    rgbaPixels[dst + 3] = 255;
                }
            }
            texture.update(&rgbaPixels[0]);
        }
        buffer->Unlock();
    }

    buffer->Release();
    sample->Release();
    return true;
}

void VideoPlayer::restart()
{
    if (!reader) return;
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = 0;
    reader->SetCurrentPosition(GUID_NULL, var);
    PropVariantClear(&var);
}

void VideoPlayer::releaseReader()
{
    if (reader)
    {
        reader->Release();
        reader = 0;
    }
}

std::wstring VideoPlayer::widenPath(const std::string& value) const
{
    int len = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, 0, 0);
    UINT codePage = CP_UTF8;
    if (len <= 0)
    {
        codePage = CP_ACP;
        len = MultiByteToWideChar(codePage, 0, value.c_str(), -1, 0, 0);
    }
    std::wstring result;
    result.resize(len > 0 ? len - 1 : 0);
    if (len > 0)
        MultiByteToWideChar(codePage, 0, value.c_str(), -1, &result[0], len);
    return result;
}
