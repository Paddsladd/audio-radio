// Flash the same program to two micro:bits (V2).
// A: record a clip.  B: send it.  A+B: play the latest clip.
radio.setGroup(7)

input.onButtonPressed(Button.A, function () {
    basic.showIcon(IconNames.SmallDiamond)
    record.startRecording(record.BlockingState.Blocking)
    basic.clearScreen()
})

input.onButtonPressed(Button.B, function () {
    basic.showArrow(ArrowNames.North)
    audioRadio.sendRecording()
    if (audioRadio.wasDelivered()) {
        basic.showIcon(IconNames.Yes)
    } else {
        basic.showIcon(IconNames.No)
    }
})

input.onButtonPressed(Button.AB, function () {
    record.playAudio(record.BlockingState.Blocking)
})

audioRadio.onReceived(function () {
    basic.showIcon(IconNames.EigthNote)
    record.playAudio(record.BlockingState.Blocking)
    basic.clearScreen()
})
