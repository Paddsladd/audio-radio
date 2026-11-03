// Flash the same program to two micro:bits (V2).
// Hold A: talk live, the other micro:bit plays it right away.
// B: record a clip and send it. The other micro:bit plays it when it has arrived.
radio.setGroup(7)

audioRadio.pushToTalk(Button.A)

input.onButtonPressed(Button.B, function () {
    basic.showIcon(IconNames.SmallDiamond)
    record.startRecording(record.BlockingState.Blocking)
    basic.showArrow(ArrowNames.North)
    audioRadio.sendRecording()
    if (audioRadio.wasDelivered()) {
        basic.showIcon(IconNames.Yes)
    } else {
        basic.showIcon(IconNames.No)
    }
})

audioRadio.onReceived(function () {
    basic.showIcon(IconNames.EigthNote)
    record.playAudio(record.BlockingState.Blocking)
    basic.clearScreen()
})

let live = false
basic.forever(function () {
    if (audioRadio.isTalking()) {
        basic.showIcon(IconNames.Heart, 0)
        live = true
    } else if (audioRadio.someoneIsTalking()) {
        basic.showIcon(IconNames.EigthNote, 0)
        live = true
    } else if (live) {
        basic.clearScreen()
        live = false
    }
    basic.pause(50)
})
