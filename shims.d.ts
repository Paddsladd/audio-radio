// Auto-generated. Do not edit.
declare namespace audioRadio {

    /**
     * Number of bytes (8-bit samples) in the current recording
     */
    //% shim=audioRadio::recordingLength
    function recordingLength(): int32;

    /**
     * Sample rate the recording was captured at
     */
    //% shim=audioRadio::recordingSampleRate
    function recordingSampleRate(): int32;

    /**
     * Read part of the recording. offset must be a multiple of length, and length must divide the
     * internal block size (256), so a chunk never spans two blocks.
     */
    //% shim=audioRadio::readChunk
    function readChunk(offset: int32, length: int32): Buffer;

    /**
     * Erase the recording and allocate room for length bytes of silence, ready for writeChunk
     */
    //% shim=audioRadio::beginWrite
    function beginWrite(length: int32): boolean;

    /**
     * Write received bytes into the recording at offset. Same alignment rules as readChunk.
     */
    //% shim=audioRadio::writeChunk
    function writeChunk(offset: int32, bytes: Buffer): void;
}

// Auto-generated. Do not edit. Really.
