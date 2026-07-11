#include "mic_permission.h"

#import <AVFoundation/AVFoundation.h>

#include <atomic>

namespace draxul
{
namespace scoreview
{

MicPermission query_mic_permission()
{
    const AVAuthorizationStatus status =
        [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    switch (status)
    {
    case AVAuthorizationStatusAuthorized:
        return MicPermission::Granted;
    case AVAuthorizationStatusDenied:
    case AVAuthorizationStatusRestricted:
        return MicPermission::Denied;
    case AVAuthorizationStatusNotDetermined:
    default:
    {
        static std::atomic<bool> requested{ false };
        if (!requested.exchange(true))
        {
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                     completionHandler:^(BOOL granted) {
                                         (void)granted; // status re-queried by polling
                                     }];
        }
        return MicPermission::Pending;
    }
    }
}

} // namespace scoreview
} // namespace draxul
