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

// ---------------------------------------------------------------------------------------------
// Live audio: microphone -> IMA ADPCM packets -> radio -> jitter buffer -> speaker
//
// Packet (19 bytes, the largest payload radio.sendBuffer allows):
//   | 0xA8 | stream | seq | predictor (int8) | step index | 14 bytes = 28 4-bit ADPCM samples |
// Each packet carries the decoder state it starts from, so a lost packet only loses its own
// 3.5 ms of audio. The radio is driven from TypeScript, so only packet building and audio
// I/O live here.
// ---------------------------------------------------------------------------------------------

#define LIVE_SAMPLE_RATE 8000
#define LIVE_TYPE 0xA8
#define LIVE_HEADER 5
#define LIVE_PAYLOAD 14
#define LIVE_SAMPLES (LIVE_PAYLOAD * 2)
#define LIVE_PACKET (LIVE_HEADER + LIVE_PAYLOAD)
#define LIVE_MAX_LOST_FILL 4

#if MICROBIT_CODAL
#define LIVE_TX_QUEUE 16
#define LIVE_RX_RING 4096 // power of two
#define LIVE_RX_MASK (LIVE_RX_RING - 1)
#define LIVE_RX_PREBUFFER 800  // 100 ms buffered before playback starts
#define LIVE_RX_MAX_LEVEL 2400 // 300 ms; beyond this incoming audio is dropped to cap latency
#define LIVE_PLAY_BLOCK 128

static const int16_t adpcmSteps[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t adpcmIndexStep[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

static inline int clampInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Apply a 4-bit code to the decoder state. Shared by encoder and decoder so they stay in step.
static inline void adpcmStep(int code, int &pred, int &index) {
    int step = adpcmSteps[index];
    int delta = step >> 3;
    if (code & 4)
        delta += step;
    if (code & 2)
        delta += step >> 1;
    if (code & 1)
        delta += step >> 2;
    pred = clampInt((code & 8) ? pred - delta : pred + delta, -32768, 32767);
    index = clampInt(index + adpcmIndexStep[code & 7], 0, 88);
}

static inline int adpcmEncode(int sample, int &pred, int &index) {
    int step = adpcmSteps[index];
    int diff = sample - pred;
    int code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    if (diff >= step) {
        code |= 4;
        diff -= step;
    }
    if (diff >= (step >> 1)) {
        code |= 2;
        diff -= step >> 1;
    }
    if (diff >= (step >> 2))
        code |= 1;
    adpcmStep(code, pred, index);
    return code;
}

/**
 * Receives 8-bit signed microphone audio from its own channel on uBit.audio.splitter, resamples
 * it to LIVE_SAMPLE_RATE and packs it into ready-to-send packets. pullRequest() runs in interrupt
 * context; packets are handed to the TypeScript fiber through a single-producer/single-consumer
 * queue.
 */
class LiveMic : public DataSink {
  public:
    SplitterChannel *channel;
    volatile bool active;
    uint8_t stream;
    uint8_t seq;
    int pred;
    int index;
    int fill;
    int phase;
    int prevSample;
    uint8_t packet[LIVE_PACKET];
    uint8_t queue[LIVE_TX_QUEUE][LIVE_PACKET];
    volatile uint32_t head; // written by pullRequest
    volatile uint32_t tail; // written by the fiber

    LiveMic() : active(false), head(0), tail(0) {
        channel = uBit.audio.splitter->createChannel();
        channel->connect(*this);
    }

    void start(int streamId) {
        active = false;
        stream = streamId;
        seq = 0;
        pred = 0;
        index = 0;
        fill = 0;
        phase = 0;
        prevSample = 0;
        tail = head;
        active = true;
        channel->dataWanted(DATASTREAM_WANTED); // switches the microphone on
    }

    void stop() {
        active = false;
        channel->dataWanted(DATASTREAM_DONT_CARE);
    }

    virtual int pullRequest() {
        ManagedBuffer buf = channel->pull();
        if (!active)
            return DEVICE_OK;
        int8_t *samples = (int8_t *)buf.getBytes();
        int n = buf.length();
        int inRate = (int)channel->getSampleRate();
        if (inRate <= LIVE_SAMPLE_RATE) {
            for (int i = 0; i < n; i++)
                encode(samples[i]);
            return DEVICE_OK;
        }
        // Decimate to LIVE_SAMPLE_RATE (the mic runs at 11 kHz), averaging neighbours as a
        // cheap low-pass filter.
        for (int i = 0; i < n; i++) {
            int s = samples[i];
            phase += LIVE_SAMPLE_RATE;
            if (phase >= inRate) {
                phase -= inRate;
                encode((s + prevSample) >> 1);
            }
            prevSample = s;
        }
        return DEVICE_OK;
    }

    void encode(int sample) {
        if (fill == 0) {
            // Round the predictor to what fits in the header so the decoder starts identically.
            int p8 = clampInt(pred >> 8, -128, 127);
            pred = p8 * 256;
            packet[0] = LIVE_TYPE;
            packet[1] = stream;
            packet[2] = seq++;
            packet[3] = (uint8_t)p8;
            packet[4] = index;
            memset(packet + LIVE_HEADER, 0, LIVE_PAYLOAD);
        }
        int code = adpcmEncode(sample * 256, pred, index);
        packet[LIVE_HEADER + (fill >> 1)] |= (fill & 1) ? code << 4 : code;
        if (++fill == LIVE_SAMPLES) {
            fill = 0;
            if (head - tail < LIVE_TX_QUEUE) { // otherwise the radio can't keep up: drop it
                memcpy(queue[head % LIVE_TX_QUEUE], packet, LIVE_PACKET);
                head = head + 1;
            }
        }
    }

    Buffer next() {
        if (tail == head)
            return mkBuffer(NULL, 0);
        Buffer b = mkBuffer(queue[tail % LIVE_TX_QUEUE], LIVE_PACKET);
        tail = tail + 1;
        return b;
    }
};

/**
 * Jitter buffer feeding a channel of uBit.audio.mixer. Received audio is written from a fiber,
 * pull() is called by the mixer in interrupt context.
 */
class LiveSpeaker : public DataSource {
  public:
    DataSink *downStream;
    int8_t ring[LIVE_RX_RING];
    volatile uint32_t wr; // written by the fiber
    volatile uint32_t rd; // written by pull()
    volatile bool flowing;

    LiveSpeaker() : downStream(NULL), wr(0), rd(0), flowing(false) {
        uBit.audio.enable();
        uBit.audio.mixer.addChannel(*this, LIVE_SAMPLE_RATE, 256);
    }

    virtual ManagedBuffer pull() {
        uint32_t level = wr - rd;
        int n = level < LIVE_PLAY_BLOCK ? level : LIVE_PLAY_BLOCK;
        if (n == 0) {
            flowing = false;
            return ManagedBuffer();
        }
        ManagedBuffer out(n);
        uint8_t *o = out.getBytes();
        for (int i = 0; i < n; i++)
            o[i] = (uint8_t)ring[(rd + i) & LIVE_RX_MASK];
        rd = rd + n;
        if (wr != rd && downStream)
            downStream->pullRequest();
        else
            flowing = false; // ran dry: wait for the prebuffer to fill again
        return out;
    }

    virtual void connect(DataSink &sink) { downStream = &sink; }
    virtual bool isConnected() { return downStream != NULL; }
    virtual void disconnect() { downStream = NULL; }
    virtual int getFormat() { return DATASTREAM_FORMAT_8BIT_SIGNED; }
    virtual float getSampleRate() { return LIVE_SAMPLE_RATE; }

    void write(const int8_t *samples, int n) {
        if (wr - rd + n > LIVE_RX_MAX_LEVEL)
            return;
        for (int i = 0; i < n; i++)
            ring[(wr + i) & LIVE_RX_MASK] = samples ? samples[i] : 0;
        wr = wr + n;

        target_disable_irq();
        if (!flowing && wr - rd >= LIVE_RX_PREBUFFER && downStream) {
            flowing = true;
            downStream->pullRequest();
        }
        target_enable_irq();
    }
};

static LiveMic *liveMic = NULL;
static LiveSpeaker *liveSpeaker = NULL;
#endif

/**
 * Start streaming the microphone into live packets tagged with streamId
 */
//%
void liveStart(int streamId) {
#if MICROBIT_CODAL
    if (!liveMic)
        liveMic = new LiveMic();
    liveMic->start(streamId);
#endif
}

/**
 * Stop streaming the microphone
 */
//%
void liveStop() {
#if MICROBIT_CODAL
    if (liveMic)
        liveMic->stop();
#endif
}

/**
 * Next live packet ready to send, or an empty buffer if there is none
 */
//%
Buffer liveNextPacket() {
#if MICROBIT_CODAL
    if (liveMic)
        return liveMic->next();
#endif
    return mkBuffer(NULL, 0);
}

/**
 * Decode a received live packet and queue it for the speaker. lost is the number of packets
 * missing just before this one; short gaps are filled with silence to keep the timing.
 */
//%
void livePlay(Buffer packet, int lost) {
#if MICROBIT_CODAL
    if (!packet || packet->length < LIVE_PACKET || packet->data[0] != LIVE_TYPE)
        return;
    int index = packet->data[4];
    if (index > 88)
        return;
    if (!liveSpeaker)
        liveSpeaker = new LiveSpeaker();

    if (lost > 0 && lost <= LIVE_MAX_LOST_FILL && liveSpeaker->flowing)
        liveSpeaker->write(NULL, lost * LIVE_SAMPLES);

    int pred = (int8_t)packet->data[3] * 256;
    int8_t samples[LIVE_SAMPLES];
    for (int i = 0; i < LIVE_SAMPLES; i++) {
        int code = (packet->data[LIVE_HEADER + (i >> 1)] >> ((i & 1) * 4)) & 15;
        adpcmStep(code, pred, index);
        samples[i] = (int8_t)(pred >> 8);
    }
    liveSpeaker->write(samples, LIVE_SAMPLES);
#endif
}

} // namespace audioRadio
