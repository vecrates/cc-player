#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, CCPlayerState) {
    CCPlayerStateIdle = 0,
    CCPlayerStateInitialized,
    CCPlayerStatePrepared,
    CCPlayerStateStarted,
    CCPlayerStatePaused,
    CCPlayerStateStopped,
    CCPlayerStateError
};

@protocol CCPlayerDelegate <NSObject>
@optional
- (void)playerDidPrepare:(id)sender;
- (void)playerDidComplete:(id)sender;
- (void)player:(id)sender didFailWithError:(NSInteger)errorCode;
- (void)player:(id)sender didUpdateProgress:(NSTimeInterval)currentMs duration:(NSTimeInterval)durationMs;
- (void)playerDidSeekComplete:(id)sender;
@end

@interface CCPlayer : NSObject

@property (nonatomic, weak, nullable) id<CCPlayerDelegate> delegate;
@property (nonatomic, readonly) CCPlayerState state;
@property (nonatomic, readonly) NSTimeInterval duration;
@property (nonatomic, readonly) NSTimeInterval currentPosition;

- (void)setDataSource:(NSString *)path;
- (void)setSurface:(void *)eaglLayer;
- (void)setSurfaceSize:(CGSize)size;

- (void)prepare;
- (void)prepareAsync;
- (void)start;
- (void)pause;
- (void)resume;
- (void)stop;
- (void)seekTo:(NSTimeInterval)positionMs;
- (void)release;

@end

NS_ASSUME_NONNULL_END
