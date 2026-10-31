#include "pxt.h"
#include "MicroBit.h"

#if MICROBIT_CODAL
#include "StreamRecording.h"
#include "StreamSplitter.h"
#endif

using namespace pxt;

// Defined in the audio-recording extension (recording.cpp). Creates its StreamRecording on first use.
namespace record {
void checkEnv();
}

namespace audioRadio {

#if MICROBIT_CODAL
using codal::StreamRecording;

// The audio-recording extension keeps its StreamRecording in a file-static variable and
// StreamRecording keeps its samples in private members. The explicit template instantiations
// below are the standard-conforming way to obtain pointers to those private members.
template <typename Tag, typename Tag::type M> struct Steal {
    friend typename Tag::type field(Tag) { return M; }
};

struct RecData {
    typedef ManagedBuffer (StreamRecording::*type)[CODAL_STREAM_RECORDING_SIZE];
    friend type field(RecData);
};
struct RecLength {
    typedef uint32_t StreamRecording::*type;
    friend type field(RecLength);
};
struct RecWriteOffset {
    typedef int StreamRecording::*type;
    friend type field(RecWriteOffset);
};

template struct Steal<RecData, &StreamRecording::data>;
template struct Steal<RecLength, &StreamRecording::totalBufferLength>;
template struct Steal<RecWriteOffset, &StreamRecording::writeOffset>;

#define BLOCK_SIZE CODAL_STREAM_RECORDING_BUFFER_SIZE
#define MAX_LENGTH (CODAL_STREAM_RECORDING_SIZE * CODAL_STREAM_RECORDING_BUFFER_SIZE)

static StreamRecording *recording = NULL;

/**
 * Locate the StreamRecording owned by the audio-recording extension. It is connected as the
 * downstream of one of the channels on uBit.audio.splitter; we identify it by comparing the
 * vtable pointer of each channel's downstream with that of a throwaway StreamRecording.
 */
static StreamRecording *getRecording() {
    if (recording)
        return recording;

    record::checkEnv();

    DataSource dummy;
    StreamRecording *probe = new StreamRecording(dummy);
    void *vtable = *(void **)static_cast<DataSink *>(probe);
    delete probe;

    StreamSplitter *splitter = uBit.audio.splitter;
    for (int i = 0; i < CONFIG_MAX_CHANNELS; i++) {
        SplitterChannel *channel = splitter->outputChannels[i];
        if (channel && channel->downStream && *(void **)channel->downStream == vtable) {
            recording = static_cast<StreamRecording *>(channel->downStream);
            break;
        }
    }
    return recording;
}
#endif

/**
 * Number of bytes (8-bit samples) in the current recording
 */
//%
int recordingLength() {
#if MICROBIT_CODAL
    StreamRecording *rec = getRecording();
    if (!rec)
        return 0;
    return rec->*field(RecLength());
#else
    return 0;
#endif
}

/**
 * Sample rate the recording was captured at
 */
//%
int recordingSampleRate() {
#if MICROBIT_CODAL
    StreamRecording *rec = getRecording();
    if (!rec)
        return 0;
    return (int)rec->getSampleRate();
#else
    return 0;
#endif
}

/**
 * Read part of the recording. offset must be a multiple of length, and length must divide the
 * internal block size (256), so a chunk never spans two blocks.
 */
//%
Buffer readChunk(int offset, int length) {
#if MICROBIT_CODAL
    StreamRecording *rec = getRecording();
    if (!rec || offset < 0 || length <= 0)
        return mkBuffer(NULL, 0);

    int total = rec->*field(RecLength());
    if (offset >= total)
        return mkBuffer(NULL, 0);
    if (offset + length > total)
        length = total - offset;

    ManagedBuffer &block = (rec->*field(RecData()))[offset / BLOCK_SIZE];
    int o = offset % BLOCK_SIZE;
    if (o + length > block.length())
        return mkBuffer(NULL, 0);
    return mkBuffer(block.getBytes() + o, length);
#else
    return mkBuffer(NULL, 0);
#endif
}

/**
 * Erase the recording and allocate room for length bytes of silence, ready for writeChunk
 */
//%
bool beginWrite(int length) {
#if MICROBIT_CODAL
    StreamRecording *rec = getRecording();
    if (!rec || length <= 0 || length > MAX_LENGTH)
        return false;

    rec->erase();

    ManagedBuffer *data = rec->*field(RecData());
    int blocks = (length + BLOCK_SIZE - 1) / BLOCK_SIZE;
    for (int i = 0; i < blocks; i++)
        data[i] = ManagedBuffer(BLOCK_SIZE); // zero-filled, i.e. silence for 8-bit signed audio

    rec->*field(RecLength()) = length;
    rec->*field(RecWriteOffset()) = length;
    return true;
#else
    return false;
#endif
}

/**
 * Write received bytes into the recording at offset. Same alignment rules as readChunk.
 */
//%
void writeChunk(int offset, Buffer bytes) {
#if MICROBIT_CODAL
    StreamRecording *rec = getRecording();
    if (!rec || offset < 0)
        return;

    int total = rec->*field(RecLength());
    int length = bytes->length;
    if (offset >= total)
        return;
    if (offset + length > total)
        length = total - offset;

    ManagedBuffer &block = (rec->*field(RecData()))[offset / BLOCK_SIZE];
    int o = offset % BLOCK_SIZE;
    if (o + length > block.length())
        return;
    memcpy(block.getBytes() + o, bytes->data, length);
#endif
}

} // namespace audioRadio
