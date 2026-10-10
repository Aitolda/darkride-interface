//
//  MixedAudioStream.h
//  libraries/audio/src
//
//  Created by Yixin Wang on 8/4/14.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_MixedAudioStream_h
#define hifi_MixedAudioStream_h

#include "InboundAudioStream.h"

class MixedAudioStream : public InboundAudioStream {
public:
    MixedAudioStream(int numFramesCapacity, int numStaticJitterFrames = -1);

    float getNextOutputFrameLoudness() const { return _ringBuffer.getNextOutputFrameLoudness(); }

    // Every sample waiting in the buffer, interleaved stereo, appended to `out`, bypassing the
    // jitter buffer (an AI resident's hearing, AI-FIRST-CLIENT.md §11.6). Returns the number read.
    int readAllSamples(QByteArray& out);
};

#endif // hifi_MixedAudioStream_h
