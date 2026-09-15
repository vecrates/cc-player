#import "CCPlayer.h"
#include "Player.h"
#include "platform/log.h"

static void iosLogFunc(ccplayer::LogLevel level, const char* tag, const char* msg) {
    os_log_type_t type;
    switch (level) {
        case ccplayer::LogLevel::Debug: type = OS_LOG_TYPE_DEBUG; break;
        case ccplayer::LogLevel::Info:  type = OS_LOG_TYPE_INFO;  break;
        case ccplayer::LogLevel::Warn:  type = OS_LOG_TYPE_DEFAULT; break;
        case ccplayer::LogLevel::Error: type = OS_LOG_TYPE_ERROR; break;
        default: type = OS_LOG_TYPE_DEFAULT;
    }
    os_log_with_type(OS_LOG_DEFAULT, type, "[%{public}s] %{public}s", tag, msg);
}

@interface CCPlayer ()
@property (nonatomic) ccplayer::Player* cppPlayer;
@end

@implementation CCPlayer

- (instancetype)init {
    self = [super init];
    if (self) {
        ccplayer::setLogFunction(iosLogFunc);
        _cppPlayer = new ccplayer::Player();
    }
    return self;
}

- (void)dealloc {
    [self release];
    delete _cppPlayer;
}

- (void)setDataSource:(NSString *)path {
    _cppPlayer->setDataSource([path UTF8String]);
}

- (void)setSurface:(void *)eaglLayer {
    _cppPlayer->setSurface(eaglLayer);
}

- (void)setSurfaceSize:(CGSize)size {
    _cppPlayer->setSurfaceSize((int)size.width, (int)size.height);
}

- (void)prepare {
    _cppPlayer->prepare();
}

- (void)prepareAsync {
    _cppPlayer->prepareAsync();
}

- (void)start {
    _cppPlayer->start();
}

- (void)pause {
    _cppPlayer->pause();
}

- (void)resume {
    _cppPlayer->resume();
}

- (void)stop {
    _cppPlayer->stop();
}

- (void)seekTo:(NSTimeInterval)positionMs {
    _cppPlayer->seekTo((int64_t)positionMs);
}

- (void)setSpeed:(float)speed {
    _cppPlayer->setSpeed(speed);
}

- (float)speed {
    return _cppPlayer->getSpeed();
}

- (void)release {
    _cppPlayer->release();
}

- (CCPlayerState)state {
    return (CCPlayerState)_cppPlayer->getState();
}

- (NSTimeInterval)duration {
    return (NSTimeInterval)_cppPlayer->getDuration();
}

- (NSTimeInterval)currentPosition {
    return (NSTimeInterval)_cppPlayer->getCurrentPosition();
}

@end
