# Audio Radio

Prata live mellan micro:bits över radio, som i en telefon eller walkie-talkie, eller skicka
inspelade ljudklipp. Klippen spelas in med den inbyggda
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
| `start talking` | Börjar skicka mikrofonen live. Alla i samma radiogrupp hör direkt |
| `stop talking` | Slutar skicka mikrofonen |
| `push to talk with button` | Prata medan knappen hålls nere, som en walkie-talkie. Börjar också lyssna |
| `is talking` | Sant medan den här micro:biten skickar live |
| `someone is talking` | Sant medan live-ljud från någon annan tas emot |
| `hear others while talking` | Om andra hörs medan man själv pratar, standard på (avancerat) |
| `send audio clip` | Skickar den senaste inspelningen. Väntar tills den skickats klart |
| `audio clip was delivered` | Sant om mottagaren fick hela klippet vid senaste sändningen |
| `on audio clip received` | Körs när ett klipp tagits emot. Klippet ersätter den egna inspelningen |
| `is receiving audio clip` | Sant medan ett klipp tas emot |
| `listen for audio clips` | Börja lyssna (klipp och live) utan något annat block från extensionen (avancerat) |
| `set pause between packets to µs` | Paus mellan radiopaket när man skickar, standard 1500 µs (avancerat) |

## Exempel

### Prata live

Ladda samma program på två micro:bits. Håll in A och prata – den andra micro:biten spelar upp direkt.

```blocks
radio.setGroup(7)
audioRadio.pushToTalk(Button.A)
```

Vill man prata samtidigt åt båda hållen, som i telefon, kan båda köra `start talking`:

```blocks
radio.setGroup(7)
input.onButtonPressed(Button.A, function () {
    audioRadio.startTalking()
})
input.onButtonPressed(Button.B, function () {
    audioRadio.stopTalking()
})
```

### Ljudklipp

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

### Live

- Live-ljudet är 8 kHz och fördröjs ca 0,1–0,2 s. Paket som försvinner skickas inte om, de blir
  korta tystnader.
- En mottagare som inte själv pratar måste lyssna: använd `push to talk with button`,
  `start talking`, `on audio clip received` eller `listen for audio clips` någonstans i programmet.
- Om flera pratar samtidigt spelar en tredje micro:bit upp den som började först.
- Prata båda samtidigt med högtalaren på och micro:bitarna nära varandra kan det eka eller tjuta,
  eftersom mikrofonen hör högtalaren. Använd hörlurar på P0/GND, håll dem isär, eller slå av
  `hear others while talking` för att bara höra när man inte pratar själv.
- Full duplex (båda pratar) tappar fler paket än när bara en pratar, eftersom radion inte kan
  sända och ta emot samtidigt.
- `push to talk with button` använder knappens *down*/*up*-händelser. Använd inte
  `on button A pressed` för samma knapp i samma program.
- Låt inte andra delar av programmet gå i tighta loopar utan `pause`, då hinner radiopaketen
  inte hanteras.

### Ljudklipp

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

### Live

`audioradio.cpp` kopplar en egen kanal på micro:bitens mikrofon-splitter och räknar om ljudet
från 11 kHz till 8 kHz. Det kodas sedan med 4-bitars IMA ADPCM, 28 sampel per paket:

| Paket | Innehåll |
| --- | --- |
| LIVE | typ, ström-id, löpnummer, ADPCM-tillstånd (2 byte), 14 byte ljud |

Varje paket har med avkodarens starttillstånd, så ett tappat paket påverkar bara sina egna 3,5 ms.
Det blir ca 290 paket i sekunden. Mottagaren lägger ljudet i en buffert och spelar upp när
0,1 s har samlats, via en egen kanal i micro:bitens ljudmixer. Luckor på upp till fyra paket
fylls med tystnad så att takten hålls.

### Ljudklipp

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
