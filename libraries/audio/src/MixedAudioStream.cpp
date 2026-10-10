//
//  MixedAudioStream.cpp
//  libraries/audio/src
//
//  Created by Yixin Wang on 8/4/14.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "MixedAudioStream.h"

#include "AudioConstants.h"

MixedAudioStream::MixedAudioStream(int numFramesCapacity, int numStaticJitterFrames) :
    InboundAudioStream(AudioConstants::STEREO, AudioConstants::NETWORK_FRAME_SAMPLES_PER_CHANNEL,
        numFramesCapacity, numStaticJitterFrames) {}

int MixedAudioStream::readAllSamples(QByteArray& out) {
    int numSamples = _ringBuffer.samplesAvailable();
    if (numSamples <= 0) {
        return 0;
    }
    int oldSize = out.size();
    out.resize(oldSize + numSamples * (int)sizeof(int16_t));
    return _ringBuffer.readSamples(reinterpret_cast<int16_t*>(out.data() + oldSize), numSamples);
}
