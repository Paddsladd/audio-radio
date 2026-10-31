# Audio Radio

Skicka och ta emot ljudklipp mellan micro:bits över radio. Ljudet spelas in med den inbyggda
**Record**-extensionen ([audio-recording](https://github.com/microsoft/pxt-microbit/tree/master/libs/audio-recording))
och skickas när inspelningen är klar. Mottagaren kan sedan spela upp klippet med
**play audio clip**.

Kräver micro:bit **V2** (mikrofon och högtalare).

## Lägg till i MakeCode

1. Öppna ett projekt på https://makecode.microbit.org
2. Klicka på **Extensions** och klistra in adressen till det här GitHub-repot
3. Extensionen drar automatiskt in **Record** och **Radio**

## Block

| Block | Vad det gör |
| --- | --- |
| `send audio clip` | Skickar den senaste inspelningen. Väntar tills den skickats klart |
| `audio clip was delivered` | Sant om mottagaren fick hela klippet vid senaste sändningen |
| `on audio clip received` | Körs när ett klipp tagits emot. Klippet ersätter den egna inspelningen |
| `is receiving audio clip` | Sant medan ett klipp tas emot |
| `listen for audio clips` | Börja lyssna utan ett `on audio clip received`-block (avancerat) |
| `set pause between packets to µs` | Paus mellan radiopaket när man skickar, standard 1500 µs (avancerat) |

## Exempel

Ladda samma program på två micro:bits. A spelar in, B skickar, A+B spelar upp.

```blocks
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
```

## Bra att veta

- Båda micro:bits måste ha samma **radio set group**.
- Ett klipp på 3 sekunder är ca 33 kB och tar några sekunder att skicka.
- Paket som försvinner skickas om. Om sändningen ändå misslyckas spelas det som kom fram,
  och luckorna blir tystnad.
- Extensionen använder **on radio received buffer**. Använd inte det blocket själv i samma program,
  men tal och text (`on radio received number/string`) fungerar som vanligt.
- Efter att ett klipp tagits emot säger Record-blocket `audio is empty` fortfarande *sant*
  på mottagaren, eftersom Record inte vet om klippet. Uppspelning fungerar ändå.
- Mottagaren spelar upp med samma samplingsfrekvens som sändaren spelade in med.
- Fungerar inte i simulatorn – där skickas inget.

## Hur det fungerar

Record-extensionen visar inte själva ljuddatan, så `audioradio.cpp` letar upp dess
`StreamRecording`-objekt i micro:bitens ljudkedja och läser/skriver ljudblocken direkt.
Det beror på hur Record och CODAL ser ut på insidan och kan behöva uppdateras om de ändras.

Klippet delas upp i bitar om 16 byte som skickas med `radio.sendBuffer`:

| Paket | Innehåll |
| --- | --- |
| START / END | typ, sessions-id, längd, samplingsfrekvens |
| DATA | typ, bitnummer, 16 byte ljud |
| NACK | typ, sessions-id, första saknade bit, bitkarta över de 120 följande |
| DONE | typ, sessions-id |

Sändaren skickar START, alla DATA och sedan END. Mottagaren svarar NACK så länge något saknas och
sändaren skickar då om de bitarna, följt av ett nytt END. När allt finns svarar mottagaren DONE.

## Licens

MIT

#### Metadata (används för sökning och rendering)

* for PXT/microbit
