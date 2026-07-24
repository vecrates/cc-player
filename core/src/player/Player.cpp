#include "Player.h"
#include "player/PlayerImpl.h"

namespace ccplayer {

Player::Player() : m_impl(new PlayerImpl()) {}
Player::~Player() { delete m_impl; }

void Player::setDataSource(const char* path) { m_impl->setDataSource(path); }
void Player::setSurface(void* nativeWindow) { m_impl->setSurface(nativeWindow); }
void Player::setSurfaceSize(int width, int height) { m_impl->setSurfaceSize(width, height); }

void Player::prepare() { m_impl->prepare(); }
void Player::prepareAsync() { m_impl->prepareAsync(); }
void Player::start() { m_impl->start(); }
void Player::pause() { m_impl->pause(); }
void Player::resume() { m_impl->resume(); }
void Player::stop() { m_impl->stop(); }
void Player::release() { m_impl->release(); }
void Player::seekTo(int64_t positionMs) { m_impl->seekTo(positionMs); }

int64_t Player::getCurrentPosition() { return m_impl->getCurrentPosition(); }
int64_t Player::getDuration() { return m_impl->getDuration(); }
PlayerState Player::getState() const { return m_impl->getState(); }

void Player::setCallbacks(const PlayerCallbacks& callbacks) { m_impl->setCallbacks(callbacks); }
void Player::setUserData(void* userData) { m_impl->setUserData(userData); }

} // namespace ccplayer
