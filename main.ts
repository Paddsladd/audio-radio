/**
 * Send and receive audio clips recorded with the Record extension over the radio. micro:bit (V2) only
 */
//% weight=4 color=#015f85 icon="" block="Audio Radio"
//% groups='["Send", "Receive", "Settings"]'
namespace audioRadio {
    // All packets go through radio.sendBuffer, whose payload is at most 19 bytes.
    //
    // START/END: | type | session | length u16 | sample rate u16 |
    // DATA:      | type | chunk u16 | 16 bytes of audio |
    // NACK:      | type | session | first chunk u16 | 15 byte bitmap of missing chunks from first |
    // DONE:      | type | session |
    //
    // The sender streams every chunk between START and END, then repeats END until the receiver
    // answers DONE. The receiver answers END with NACK while chunks are missing and the sender
    // resends those. DATA carries no session id; it is only accepted while a transfer is active.
    const PACKET_START = 0xA0
    const PACKET_DATA = 0xA1
    const PACKET_END = 0xA2
    const PACKET_NACK = 0xA3
    const PACKET_DONE = 0xA4

    const CHUNK_SIZE = 16
    const NACK_BITMAP_BYTES = 15
    const REPLY_TIMEOUT_MS = 300
    const MAX_ROUNDS = 100
    const MAX_SILENT_ROUNDS = 5
    const RECEIVE_TIMEOUT_MS = 3000

    let initialized = false
    let packetGapMicros = 1500

    // Sender state
    let sending = false
    let txSession = -1
    let txReply = 0
    let txNack: Buffer = null
    let lastSendOk = false

    // Receiver state
    let receiving = false
    let rxSession = -1
    let rxLength = 0
    let rxChunks = 0
    let rxRate = 0
    let rxReceived = 0
    let rxBitmap: Buffer = null
    let rxLastPacket = 0
    let rxDoneSession = -1
    let rxDoneLength = 0
    let onReceivedHandler: () => void = null

    // Wrappers around the C++ in audioradio.cpp; the bodies only run in the simulator.
    //% shim=audioRadio::recordingLength
    function nativeRecordingLength(): number {
        return 0
    }

    //% shim=audioRadio::recordingSampleRate
    function nativeRecordingSampleRate(): number {
        return 0
    }

    //% shim=audioRadio::readChunk
    function nativeReadChunk(offset: number, length: number): Buffer {
        return control.createBuffer(0)
    }

    //% shim=audioRadio::beginWrite
    function nativeBeginWrite(length: number): boolean {
        return false
    }

    //% shim=audioRadio::writeChunk
    function nativeWriteChunk(offset: number, bytes: Buffer): void {
    }

    function init() {
        if (initialized) return
        initialized = true
        radio.onReceivedBuffer(handlePacket)
        control.inBackground(watchReceiveTimeout)
    }

    function handlePacket(packet: Buffer) {
        if (packet.length < 2) return
        switch (packet[0]) {
            case PACKET_START:
            case PACKET_END:
                handleStartOrEnd(packet)
                break
            case PACKET_DATA:
                handleData(packet)
                break
            case PACKET_NACK:
            case PACKET_DONE:
                if (sending && packet[1] == txSession) {
                    txNack = packet
                    txReply = packet[0]
                }
                break
        }
    }

    function handleStartOrEnd(packet: Buffer) {
        if (packet.length < 6) return
        const session = packet[1]
        if (sending && session == txSession) return

        const length = packet.getNumber(NumberFormat.UInt16LE, 2)
        if (!receiving || session != rxSession) {
            if (session == rxDoneSession && length == rxDoneLength) {
                // Our DONE got lost, so the sender is still asking.
                if (packet[0] == PACKET_END) sendReply(PACKET_DONE, session)
                return
            }
            if (!nativeBeginWrite(length)) return
            receiving = true
            rxSession = session
            rxLength = length
            rxRate = packet.getNumber(NumberFormat.UInt16LE, 4)
            rxChunks = Math.idiv(length + CHUNK_SIZE - 1, CHUNK_SIZE)
            rxReceived = 0
            rxBitmap = control.createBuffer((rxChunks + 7) >> 3)
        }
        rxLastPacket = control.millis()

        if (packet[0] == PACKET_END) {
            if (rxReceived >= rxChunks) {
                sendReply(PACKET_DONE, session)
                finishReceive()
            } else {
                sendNack()
            }
        }
    }

    function handleData(packet: Buffer) {
        if (!receiving || packet.length < 4) return
        const chunk = packet.getNumber(NumberFormat.UInt16LE, 1)
        if (chunk >= rxChunks) return
        rxLastPacket = control.millis()
        const bit = 1 << (chunk & 7)
        if (rxBitmap[chunk >> 3] & bit) return
        rxBitmap[chunk >> 3] |= bit
        rxReceived++
        nativeWriteChunk(chunk * CHUNK_SIZE, packet.slice(3))
    }

    function sendNack() {
        let first = 0
        while (first < rxChunks && (rxBitmap[first >> 3] & (1 << (first & 7)))) first++
        const nack = control.createBuffer(4 + NACK_BITMAP_BYTES)
        nack[0] = PACKET_NACK
        nack[1] = rxSession
        nack.setNumber(NumberFormat.UInt16LE, 2, first)
        for (let i = 0; i < NACK_BITMAP_BYTES * 8; i++) {
            const chunk = first + i
            if (chunk >= rxChunks) break
            if (!(rxBitmap[chunk >> 3] & (1 << (chunk & 7))))
                nack[4 + (i >> 3)] |= 1 << (i & 7)
        }
        radio.sendBuffer(nack)
    }

    function sendReply(type: number, session: number) {
        const reply = control.createBuffer(2)
        reply[0] = type
        reply[1] = session
        radio.sendBuffer(reply)
    }

    function finishReceive() {
        receiving = false
        rxDoneSession = rxSession
        rxDoneLength = rxLength
        rxBitmap = null
        if (rxRate >= 1000 && rxRate <= 22000)
            record.setSampleRate(rxRate, record.AudioSampleRateScope.Playback)
        if (onReceivedHandler) control.runInParallel(onReceivedHandler)
    }

    function watchReceiveTimeout() {
        while (true) {
            basic.pause(500)
            if (receiving && control.millis() - rxLastPacket > RECEIVE_TIMEOUT_MS) {
                // The sender gave up. Keep what arrived; the gaps play as silence.
                if (rxReceived > 0) finishReceive()
                else receiving = false
            }
        }
    }

    function sendChunk(chunk: number) {
        const packet = control.createBuffer(3)
        packet[0] = PACKET_DATA
        packet.setNumber(NumberFormat.UInt16LE, 1, chunk)
        radio.sendBuffer(packet.concat(nativeReadChunk(chunk * CHUNK_SIZE, CHUNK_SIZE)))
        control.waitMicros(packetGapMicros)
        // Let other fibers (screen, radio handler) run now and then
        if ((chunk & 31) == 31) basic.pause(1)
    }

    function sendHeader(type: number, length: number, rate: number) {
        const packet = control.createBuffer(6)
        packet[0] = type
        packet[1] = txSession
        packet.setNumber(NumberFormat.UInt16LE, 2, length)
        packet.setNumber(NumberFormat.UInt16LE, 4, rate)
        radio.sendBuffer(packet)
    }

    function waitForReply(): number {
        const start = control.millis()
        while (txReply == 0 && control.millis() - start < REPLY_TIMEOUT_MS) basic.pause(5)
        return txReply
    }

    /**
     * Send the recorded audio clip over radio to other micro:bits in the same radio group.
     * Waits until the clip has been sent.
     */
    //% block="send audio clip"
    //% blockId="audioradio_send"
    //% group="Send"
    //% weight=100
    //% parts="microphone"
    export function sendRecording(): void {
        init()
        if (sending) return
        lastSendOk = false

        while (record.audioStatus(record.AudioStatus.Recording)) basic.pause(20)
        const length = nativeRecordingLength()
        if (length <= 0) return

        sending = true
        let session = randint(0, 255)
        while (session == txSession) session = randint(0, 255)
        txSession = session
        const rate = nativeRecordingSampleRate()
        const chunks = Math.idiv(length + CHUNK_SIZE - 1, CHUNK_SIZE)

        sendHeader(PACKET_START, length, rate)
        for (let chunk = 0; chunk < chunks; chunk++) sendChunk(chunk)

        let silentRounds = 0
        for (let round = 0; round < MAX_ROUNDS && silentRounds < MAX_SILENT_ROUNDS; round++) {
            txReply = 0
            sendHeader(PACKET_END, length, rate)
            const reply = waitForReply()
            if (reply == PACKET_DONE) {
                lastSendOk = true
                break
            } else if (reply == PACKET_NACK) {
                silentRounds = 0
                const nack = txNack
                const first = nack.getNumber(NumberFormat.UInt16LE, 2)
                for (let i = 0; i < NACK_BITMAP_BYTES * 8; i++) {
                    const chunk = first + i
                    if (chunk >= chunks) break
                    if (nack[4 + (i >> 3)] & (1 << (i & 7))) sendChunk(chunk)
                }
            } else {
                silentRounds++
            }
        }
        sending = false
    }

    /**
     * True if the last audio clip that was sent was received completely
     */
    //% block="audio clip was delivered"
    //% blockId="audioradio_last_send_ok"
    //% group="Send"
    //% weight=90
    export function wasDelivered(): boolean {
        return lastSendOk
    }

    /**
     * Run code when an audio clip has been received over radio.
     * The clip replaces the current recording and can be played with "play audio clip".
     */
    //% block="on audio clip received"
    //% blockId="audioradio_on_received"
    //% group="Receive"
    //% weight=80
    //% parts="microphone"
    export function onReceived(handler: () => void): void {
        init()
        onReceivedHandler = handler
    }

    /**
     * True while an audio clip is being received
     */
    //% block="is receiving audio clip"
    //% blockId="audioradio_is_receiving"
    //% group="Receive"
    //% weight=70
    export function isReceiving(): boolean {
        return receiving
    }

    /**
     * Start listening for audio clips without an "on audio clip received" block
     */
    //% block="listen for audio clips"
    //% blockId="audioradio_listen"
    //% group="Receive"
    //% weight=60
    //% advanced=true
    export function listen(): void {
        init()
    }

    /**
     * Pause between radio packets when sending. A longer pause loses fewer packets but sends slower.
     * @param micros pause in microseconds, eg: 1500
     */
    //% block="set pause between packets to $micros µs"
    //% blockId="audioradio_set_packet_gap"
    //% group="Settings"
    //% micros.min=0 micros.max=10000 micros.defl=1500
    //% weight=50
    //% advanced=true
    export function setPacketGap(micros: number): void {
        packetGapMicros = Math.max(0, micros)
    }
}
