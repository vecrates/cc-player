#pragma once

#include "PlayerTypes.h"
#include <cstdint>

namespace ccplayer {

class PlayerImpl;

class Player {
public:
    Player();
    ~Player();

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    void setDataSource(const char* path);
    void setSurface(void* nativeWindow);
    void setSurfaceSize(int width, int height);

    void prepare();
    void prepareAsync();

    void start();
    void pause();
    void resume();
    void stop();
    void release();

    void seekTo(int64_t positionMs);

    int64_t getCurrentPosition();
    int64_t getDuration();

    PlayerState getState() const;

    void setCallbacks(const PlayerCallbacks& callbacks);
    void setUserData(void* userData);

private:
    PlayerImpl* m_impl;
};

} // namespace ccplayer
