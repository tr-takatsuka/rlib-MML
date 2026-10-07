# **Music Macro Language Compiler**

[日本語](/README.ja.md)

## Description

This is a class library for compiling (converting) MML to Standard MIDI Files (SMF).

It also supports converting Standard MIDI Files back to MML.

- Implemented in C++20.  
- Includes built-in command-line applications:  
  - mmltosmf: MML → Standard MIDI File  
  - smftomml: Standard MIDI File → MML  
- While there is no fixed standard for MML syntax, we do try to follow what amounts to an unwritten standard.

## Demo Page

[https://rlib-mml.thinkridge.jp/](https://rlib-mml.thinkridge.jp/)

An integrated environment where you can create and play MML directly in your browser.

## Requirement

It can be compiled in a C++20 environment. The build requires boost.

We have confirmed the operation in the following environment.

- linux g++
- windows VisualStudio 2019,2022,2026

### WebAssembly

WebAssembly is also supported.

# **MML (Music Macro Language) Syntax**

## Notes
|  notation  |  description  |example|
| ---- | ---- | ---- |
|a～[+-][length]| It is a note.<br>Immediately after, press +-to raise or lower a semitone (optional).<br>You can then specify the note length (optional). | // It is a quarter note F.<br>f4<br><br>// It is notes. The note length is the default value.<br>cdefgab <br><br>// dotted quarter note B♭.<br>a-16.|
|r[length]|It is a rest. You can specify the note length immediately after it (optional).| // 16th rest.<br>r16|
|^[length]|It's Tie. You can specify the note length immediately after it (optional).<br>Add the note length of the previous note. Same as rest if immediately preceding is not a note.|// It is the length of three quarter notes.<br>c4^4^4<br><br>// It is the length of three quarter notes. However, the tempo is changed on the way.<br>// It is possible to write another instruction between the note and ^.<br>c4 t50 ^4 t20 ^4|
|l[length]|This is the default note length.<br>This value is used when the note length is omitted for notes and rests.| // This is an 8th note CDE.<br>l8 cde|
|<<br>> | octave up(<) and down(>).<br>The range of one octave is 12 tones from C to B.| // C and C one octave higher are repeated twice.<br>c\<c>c\<c |
|o[octave]|Specify the octave. Values ​​are -2-8.<br>MIDI note number 60 is C3. Write "o3c"| // MIDI note number 60(C3)、62(D3)、64(E3)<br>o3 cde|
|t[tempo]|Specify the tempo. The value can be specified from 1.0 to 999.0. Decimal values can be written.<br>Relative specification is supported.| // CDE at tempo 120, then CDE at tempo 90.<br>t120 cde t-30 cde|
|@[program no]|Specify the program number (tone). Values ​​are 0-127.| // CDE atprogram number 3.<br>@3 cde|
|v[velocity]|Specifies the velocity (the strength with which you hit the keyboard on a piano). Values ​​are 0-127.<br>Relative specification is supported.| // CDE at velocity 123, FGA at velocity 113.<br>v123 cde v-=10 fga|
|'[switch chord mode]|Switches to a mode where the current position is not advanced by notes. Use rests to advance the current position. Use when two or more notes are pronounced simultaneously, e.g. chords.| // 8th note C major chord and D miner chords.<br>l8 'cegr dfar'|

### Note Lengths

- This is how to specify the length of the note.

| notation  |  description  |example|
| ---- | ---- | ---- |
|1～192<br>note notation|Specify the value of the minute note.<br>It is also possible to write 12th notes and 24th notes.<br>0 cannot be specified (it results in an error).| // It is a quarter note C.<br>c4|
|!1～!99999<br>Step number notation|Specify by the number of steps. The quarter note is 480.| // 8th note C.<br>c!240|
|.<br>point notation| It means a dotted point called a minute note.<br>By writing more than one, it becomes the meaning of a double-dotted note (add half of the previous note length).| // dotted quarter note C.<br>c4.<br><br>// note length that is the sum of quarter notes, 8th notes, 16th notes, and 32th notes.<br>c4...|
|+ - length<br>subtraction of numerical note length|Adds or subtracts the note length value.| //note length of 8th note plus quarter note.<br>c8+4<br><br>// 32th note C and D, shortened by 2 32th notes from a whole note.<br>c32d32e1-32-32<br><br>// In this case, it is a quarter note D♭. be careful.<br>d-4|

## Functions

|  表記  |  説明  |例|
| ---- | ---- | ---- |
|CreatePort(<br>&emsp;name:[port name],<br>&emsp;instrument:[instrument name]\(optional),<br>&emsp;channel:[channel number],<br>)|Defines (declares) a port and switches to it.<br>Channel numbers are 1-16.<br>If instrument is omitted, the current instrument is inherited.|// Declare the port of MIDI channel 3 with the name "Piano".<br>// "instrument" specifies the name of the instrument. It can be omitted.<br>CreatePort(name:Piano, instrument:"fm", channel:3)|
|Port([port name])|It is port switching|// CDE at Port "Piano"<br>Port(Piano) cde|
|V([volume value])<br>ailias: Volume| The Volume. Values ​​are 0-127.<br>Relative specification is supported.|// volume 120 it's CDE, 90 it's FGA<br>V(120) cde V(-=30) fga|
|Ep([value])<br>ailias: Expression|The Expression. Values ​​are 0-127.<br>Relative specification is supported.|// Expression 120 it's CDE, 90 it's FGA.<br>Ep(120) cde Ep(-=30) fga|
|Pan([pan value])<br>ailias: Panpot| It is pan (panpot).<br>Values ​​range from 0 (far left) to 127 (far right), with 64 in the center.<br>Relative specification is supported.|// CDE at Pan 10, FGA at Pan 60<br>Pan(10) cde Pan(+=50) fga|
|PitchBend([value])|Pitch bend.<br>Values range from -8192 (two notes down) to 8191 (two notes up), with the centre at 0.<br>Relative specification is supported.|// CDE at scale lowered by a half, and the standard scale<br>PitchBend(-4096) cde PitchBend(+=4096) cde|
|FineTune([sent value])|FineTune.<br>Specify the cent value, with a semitone set to 100. The <br> value supports decimal values ranging from -100.0 to 100.0. The midpoint is 0.<br>Relative specification is supported.|// These are the notes CDE 20.5 cents<br>// above the reference pitch and 9.5 cents below it.<br>FineTune(20.5) cde FineTune(-=30) cde|
|CoarseTune([value])|CoarseTune.<br>Adjust the key in semitones. The <br> value ranges from -64 to 63, with 0 being the center.<br>Relative specification is supported.|// These are the CDE, one two steps higher<br>// and one two steps lower than the standard pitch<br>CoarseTune(2) cde CoarseTune(-=3) cde|
|CC([contorl change no],[value])<br>ailias: ContorlChange| Contorl change.<br>The first argument is the control number and the first argument is the value|// Bank-selected programme change.<br>CC(0,10)CC(32,130)@2|
|CreateSeq(<br>&emsp;name:[sequence name],<br>&emsp;mml:[MML],<br>&emsp;align:[unit(optional)],<br>)<br>alias: CreateSequence|Defines a sequence (sub-sequence).<br>Define a piece of music (MML) as a part, which can then be expanded (pasted) in subsequent MML.<br>A sequence with the same name cannot be defined more than once.<br>If align is specified, the length by which Seq advances the current position is rounded up to a multiple of that unit (same notation as note length).<br>If omitted, the position advances by exactly the length of the sequence.|// Defined rhythm pattern<br/>CreateSeq(name:drum, mml:"<br/>&emsp;CreatePort(name:kick, channel:10) l8 o1 c^^c ^c^^<br/>  &emsp;CreatePort(name:snare, channel:10) l8 o1 ^^d^ ^^d^<br/>")<br/><br/>// Defines a sequence that advances in whole-bar units<br/>CreateSeq(name:fill, mml:"c4 d4", align:"1")|
|Seq(<br>&emsp;[sequence name],<br>&emsp;length:[length(optional)]<br>)<br>alias: Sequence|Expands a defined sequence (sub-sequence) at the current position.<br>If length is omitted, the current position advances by the length of the sequence (rounded up to a multiple of align, if CreateSeq specified it).<br>If length is specified, events at or after that length are discarded, and the current position advances by exactly that length.<br>See [Sequence Details](#sequence-details) for more information.|// Defined rhythmic pattern is repeated three times.<br>//Only half-note minutes are used for the third round of the sequence.<br/>Seq(drum) Seq(drum) Seq(drum,length:"2")|
|MasterVolume([value])|The Master Volume. Values ​​are 0-16383. Relative specification is supported.<br>This is an alias for the GM Master Volume in System Exclusive.|// Set the master volume to 10000, then raise it by 1000<br>MasterVolume(10000) r MasterVolume(+=1000)|
|SysEx(<br>&emsp;[data]...<br>)<br>|It is SystemExclusive<br>Specify data with a variable-length argument with no name.<br>Specify either 0xf0 or 0xf7 as the first byte, and (generally) 0xf7 as the last byte.|// Reset Roland GS （GS Reset）<br/>SysEx(0xf0,0x41,0x10,0x42,0x12,0x40,0x00,0x7f,0x00,0x41,0xf7)|
|Meta(<br>&emsp;type:[event type],<br>&emsp;[data]...<br>)|Meta Event.<br>type specifies the event type.<br>Specify data with a variable-length argument with no name. It is not necessary to describe the data length.| // title info<br/>Meta(type:0x1,"The Lost King's Scepter")// SMPTE offset<br>Meta(type:0x54,96,0,0,0,0)|
|DefinePresetFM(<br>&emsp;no:[program no],<br>&emsp;name:[name],<br>&emsp;[data]...<br>)|Sequencer specific meta event that defines FM sound tone in rlib-MML.|DefinePresetFM(no:4, name:"piano",<br>// AR  DR  SR  RR  SL  TL KS  ML DT<br>&emsp;29,  8,  0,  8,  3, 31, 2,  1, 3,<br>&emsp;31,  3,  1,  6, 10,  0, 0,  2, 7,<br>&emsp;29, 20,  0,  9,  2, 44, 0,  4, 2,  <br>&emsp;31,  7,  2,  6,  6,  0, 0,  1, 5,<br>// AL  FB<br>&emsp;4,  7,<br>)|
|DefinePresetPSG(<br>&emsp;no:[program no],<br>&emsp;name:[name],<br>&emsp;[data]...<br>)|Sequencer specific meta event that defines PSG sound tone in rlib-MML.|DefinePresetPSG(no:5, name:"piano",<br><br>// AR,HR,DR,SL,RR : Software envelope parameters AR,HR,DR,RR:(sec) SL:0.0～1.0<br>// noise : noise freq(1～31) 0=OFF<br>// tone : tone 1=ON/0=OFF<br>DefinePresetPSG(no:0, name:"piano",<br>&emsp;// AR   HR    DR   SL   RR     noise  tone<br>&emsp;0.0, 0.0,  1.0, 0.3, 1.0,        0,    1,<br>)|


## Sequence Details

A sequence defined with CreateSeq is expanded with Seq.

### Nested sequences

- A sequence can call another sequence.
- A sequence defined inside a sequence can be used only within that sequence (and within the sequences called from it).

### Ports inside a sequence

- Port(port name) switches to a port of the caller so that you can play on it. That port's octave, default note length and velocity are also inherited.
- A port created with CreatePort inside a sequence can be used only within that sequence (and within the sequences called from it).

````
// Rhythm pattern A
CreateSeq(name:drum, mml:"
 Port(kick)   c ^ ^ c ^ c ^ ^   // You can switch to a port defined by the caller
 Port(snare)  ^ ^ d ^ ^ ^ d ^
 Port(hat)    f+f+f+f+f+f+f+f+
")

CreatePort(name:kick,  channel:10)  l8 o1 v127
CreatePort(name:snare, channel:10)  l8 o1 v105
CreatePort(name:hat,   channel:10)  l8 o1 v125
Seq(drum) Seq(drum) Seq(drum) Seq(drum) // Expand the rhythm pattern here

// Bass
CreateSeq(name:baseE, mml:"o1 e^<e>e^e<e^") // Bass E (by default, the caller's port is inherited)
CreateSeq(name:baseD, mml:"o1 d^<d>d^d<d^") // Bass D
CreateSeq(name:baseC, mml:"o1 c^<c>c^c<c^") // Bass C
CreateSeq(name:baseA, mml:"o0 a^<a>a^a<a^") // Bass A

CreatePort(name:bass, channel:1) @33 v127 l8
Seq(baseE) Seq(baseD) Seq(baseC) Seq(baseA) // Bass E => D => C => A
````

### Where a sequence is expanded and how far the position advances

- A sequence is expanded from the current position of the port in which Seq is written.
- The current position of the port in which Seq is written advances by an amount determined in the following order of priority.
  1. If Seq specifies length: it advances by exactly that length. Events at or after that length are discarded.
  2. If CreateSeq specifies align: it advances by the length of the sequence rounded up to a multiple of align. If the length is already a multiple, it is used as is.
  3. If both are omitted: it advances by exactly the length of the sequence.
- The length of a sequence is the maximum of the current positions of all ports used inside the sequence. It includes not only the end of the last note but also trailing rests. The length of an empty sequence is 0, so the current position does not advance.
- align uses the same notation as note length. `"1"` is a whole note (one bar in 4/4), `"4"` is a quarter note, `"2."` is a dotted half note (one bar in 3/4), and `"!240"` is a number of steps. 0 or an invalid notation is an error, detected at the point of CreateSeq.
- align rounds up the length measured from the position where Seq is called. It does not snap to bar lines.
- In the mode that does not advance the position (`'`), Seq does not advance the current position either.

```
CreateSeq(name:X, mml:"c4 d4")                // length is 960 (two quarter notes)
CreateSeq(name:Y, mml:"c4 d4", align:"1")     // length is 1920 (rounded up to one bar)
CreateSeq(name:Z, mml:"c4 r2.")               // 1920, including the trailing rest

Seq(X) e                // e sounds at position 960
Seq(Y) e                // e sounds at position 1920
Seq(Y,length:"4") e     // length takes priority, so e sounds at position 480
```

## Strings

Where a string is specified, the following formats can be used.

|  notation  |  description  |example|
| ---- | ---- | ---- |
| ○○○ | This designation is possible in alphanumeric characters only.| trumpet  |
| "○○○" | " to " is a string. You can also use spaces and non-symbols.　| "drum part"
| R"\*\*(○○○)\*\*" | It can also be used for whitespace and symbols.<br>If you are defining a sequence within a sequence, you can use ** as a unique string to deal with cases where you have a string definition within a string definition　| R"(drum)"<br><br> R"ch1( mml:R"(drum)" )ch1"

## Relative Specification

Parameters supporting relative adjustment allow math operations on the current value:

| Notation | Description | Example |
| ---- | ---- | ---- |
| +=n | Addition | // increased the velocity by 10.<br> v+=10 cde  |
| -=n | Subtraction |  // Expression 100 → 96 (rounded from 95.6) → 95 (rounded from 95.2)<br>Ep(100) c Ep(-=4.4) d Ep(-=0.4) e|
| \*=n | Multiplication | // Volume 100 → 127 (130 is clipped within the range) → 117 (multiply the current value of 130 by 0.9)<br>V(100) c V(\*=1.3) d V(\*=0.9) e |
| /=n | Division | // Volume 100 → 50 → 25<br>V(100) c V(/=2) d V(/=2) e |

### How current values are held

The current value used as the base for relative specification is held differently depending on the target.

| Target | Where the current value is held | Initial value |
| ---- | ---- | ---- |
| v (velocity) | Per port | 100 |
| V (volume)<br>Ep (expression)<br>Pan<br>PitchBend<br>FineTune<br>CoarseTune | Per port | V:100<br>Ep:127<br>Pan:64<br>PitchBend:0<br>FineTune:0<br>CoarseTune:0 |
| t (tempo) | One for the whole song | 120 |
| MasterVolume | Per instrument | 16383 |

- Per-port values are not shared with other ports, even if the instrument and channel are the same.
- A port created with CreatePort inside a sequence starts from the initial values each time the sequence is called.<br>A caller's port switched to with Port(port name) continues from the caller's values.
- Tempo and MasterVolume are processed in song-time order, regardless of which port they are written in. If several are at the same position, they are processed in the order the ports were defined.

```
// Volume is held per port. b becomes 110 (the initial value 100 plus 10), not based on a's 80.
CreatePort(name:a, instrument:fm, channel:1) V(80) c
CreatePort(name:b, instrument:fm, channel:1) V(+=10) c
```

```
// Tempo is a single value for the whole song.
// A ritardando (gradually slowing down) is defined as a part and called from another port.
CreateSeq(name:rit, mml:"t-=10 r1 t-=10")

CreatePort(name:melody, instrument:fm, channel:1) t100 l1 c d e
// The tempo becomes 90 at the 2nd bar and 80 at the 3rd bar.
CreatePort(name:conductor, instrument:fm, channel:16) r1 Seq(rit)
```

```
// MasterVolume is held per instrument.
// fm goes 8000 -> 9000. psg becomes 15383 (the initial value 16383 minus 1000).
CreatePort(name:a, instrument:fm, channel:1) MasterVolume(8000)
CreatePort(name:b, instrument:fm, channel:2) MasterVolume(+=1000)
CreatePort(name:c, instrument:psg, channel:1) MasterVolume(-=1000)
```


## comments

|  notation  |  description  |example|
| ---- | ---- | ---- |
| // ○○○| This is a one-line comment. // After that, the comment is up to the line break.| // its comments. |
| /* ○○○ */ | It is a range comment.<br>Comments are from / * to * /. Comments that span multiple lines are also possible.| /* its<br>comments. */|

## Licence

[LICENSE](/LICENSE)

