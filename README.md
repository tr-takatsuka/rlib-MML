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
|t[tempo]|Specify the tempo. The value can be specified from 1.0 to 999.0. A small number can be written.| // CDE at tempo 120.<br>t120 cde|
|@[program no]|Specify the program number (tone). Values ​​are 0-127.| // CDE atprogram number 3.<br>@3 cde|
|v[velocity]|Specifies the velocity (the strength with which you hit the keyboard on a piano). Values ​​are 0-127.| // CDE at velocity 123.<br>v123 cde|
|'[switch chord mode]|Switches to a mode where the current position is not advanced by notes. Use rests to advance the current position. Use when two or more notes are pronounced simultaneously, e.g. chords.| // 8th note C major chord and D miner chords.<br>l8 'cegr dfar'|

### Note Lengths

- This is how to specify the length of the note.

| notation  |  description  |example|
| ---- | ---- | ---- |
|1～192<br>note notation|Specify the value of the minute note.<br>It is also possible to write 12th notes and 24th notes.| // It is a quarter note C.<br>c4|
|!1～!99999<br>Step number notation|Specify by the number of steps. The quarter note is 480.| // 8th note C.<br>c!240|
|.<br>point notation| It means a dotted point called a minute note.<br>By writing more than one, it becomes the meaning of a double-dotted note (add half of the previous note length).| // dotted quarter note C.<br>c4.<br><br>// note length that is the sum of quarter notes, 8th notes, 16th notes, and 32th notes.<br>c4...|
|+ - length<br>subtraction of numerical note length|Adds or subtracts the note length value.| //note length of 8th note plus quarter note.<br>c8+4<br><br>// 32th note C and D, shortened by 2 32th notes from a whole note.<br>c32d32e1-32-32<br><br>// In this case, it is a quarter note D♭. be careful.<br>d-4|

## Functions

|  表記  |  説明  |例|
| ---- | ---- | ---- |
|CreatePort(<br>&emsp;name:[port name],<br>&emsp;instrument:[instrument name]\(optional),<br>&emsp;channel:[channel number],<br>)|Define (declare) the port<br>Channel numbers are 1-16.|// Declare the port of MIDI channel 3 with the name "Piano".<br>// "instrument" specifies the name of the instrument. It can be omitted.<br>CreatePort(name:Piano, instrument:"fm", channel:3)|
|Port([port name])|It is port switching|// CDE at Port "Piano"<br>Port(Piano) cde|
|V([volume value])<br>ailias: Volume| The Volume. Values ​​are 0-127.<br>Relative paths are supported.|// volume 120 it's CDE, 90 it's FGA<br>V(120) cde V(-=30) fga|
|Ep([value])<br>ailias: Expression|The Expression. Values ​​are 0-127.<br>Relative paths are supported.|// Expression 120 it's CDE, 90 it's FGA.<br>Ep(120) cde Ep(-=30) fga|
|Pan([pan value])<br>ailias: Panpot| It is pan (panpot).<br>Values ​​range from 0 (far left) to 127 (far right), with 64 in the center.<br>Relative paths are supported.|// CDE at Pan 10, FGA at Pan 60<br>Pan(10) cde Pan(+=50) fga|
|PitchBend([value])|Pitch bend.<br>Values range from -8192 (two notes down) to 8191 (two notes up), with the centre at 0.<br>Relative paths are supported.|// CDE at scale lowered by a half, and the standard scale<br>PitchBend(-4096) cde PitchBend(+=4096) cde|
|FineTune([sent value])|FineTune.<br>Specify the cent value, with a semitone set to 100. The <br> value supports decimal values ranging from -100.0 to 100.0. The midpoint is 0.<br>Relative paths are supported.|// These are the notes CDE 20.5 cents<br>// above the reference pitch and 9.5 cents below it.<br>FineTune(20.5) cde FineTune(-=30) cde|
|CoarseTune([value])|CoarseTune.<br>Adjust the key in semitones. The <br> value ranges from -64 to 63, with 0 being the center.<br>Relative paths are supported.|// These are the CDE, one two steps higher<br>// and one two steps lower than the standard pitch<br>CoarseTune(2) cde CoarseTune(-=3) cde|
|CC([contorl change no],[value])<br>ailias: ContorlChange| Contorl change.<br>The first argument is the control number and the first argument is the value|// Bank-selected programme change.<br>CC(0,10)CC(32,130)@2|
|CreateSequence(<br>&emsp;name:[sequence name],<br>&emsp;mml:[MML],<br>)|Defined Sequence(sub Sequence).<br>Define songs (MML) as parts and call them in subsequent MMLs.|// Defined rhythm pattern<br/>CreateSequence(name:drum, mml:"<br/>&emsp;CreatePort(name:kick, channel:10) l8 o1 c^^c ^c^^<br/>  &emsp;CreatePort(name:snare, channel:10) l8 o1 ^^d^ ^^d^<br/>")|
|Seq(<br>&emsp;[sequence name],<br>&emsp;length:[length(optional)]<br>)<br>ailias Sequence|Calls a predefined sequence (sub-sequence).|// Defined rhythmic pattern is repeated three times.<br>//Only half-note minutes are used for the third round of the sequence.<br/>Seq(drum) Seq(drum) Seq(drum,length:"2")|
|MasterVolume([値])|The Master Volume. Values ​​are 0-16383.<br>This is an alias for the GM Master Volume in System Exclusive.|// Master Volume 10000<br>MasterVolume(10000)|
|SysEx(<br>&emsp;[data]...<br>)<br>|It is SystemExclusive<br>Specify data with a variable-length argument with no name.<br>Specify either 0xf0 or 0xf7 as the first byte, and (generally) 0xf7 as the last byte.|// Reset Roland GS （GS Reset）<br/>SysEx(0xf0,0x41,0x10,0x42,0x12,0x40,0x00,0x7f,0x00,0x41,0xf7)|
|Meta(<br>&emsp;type:[event type],<br>&emsp;[data]...<br>)|Meta Event.<br>type specifies the event type.<br>Specify data with a variable-length argument with no name. It is not necessary to describe the data length.| // title info<br/>Meta(type:0x1,"The Lost King's Scepter")// SMPTE offset<br>Meta(type:0x54,96,0,0,0,0)|
|DefinePresetFM(<br>&emsp;no:[program no],<br>&emsp;name:[name],<br>&emsp;[data]...<br>)|Sequencer specific meta event that defines FM sound tone in rlib-MML.|DefinePresetFM(no:4, name:"piano",<br>// AR  DR  SR  RR  SL  TL KS  ML DT<br>&emsp;29,  8,  0,  8,  3, 31, 2,  1, 3,<br>&emsp;31,  3,  1,  6, 10,  0, 0,  2, 7,<br>&emsp;29, 20,  0,  9,  2, 44, 0,  4, 2,  <br>&emsp;31,  7,  2,  6,  6,  0, 0,  1, 5,<br>// AL  FB<br>&emsp;4,  7,<br>)|
|DefinePresetPSG(<br>&emsp;no:[program no],<br>&emsp;name:[name],<br>&emsp;[data]...<br>)|Sequencer specific meta event that defines PSG sound tone in rlib-MML.|DefinePresetPSG(no:5, name:"piano",<br><br>// AR,HR,DR,SL,RR : Software envelope parameters AR,HR,DR,RR:(sec) SL:0.0～1.0<br>// noise : noise freq(1～31) 0=OFF<br>// tone : tone 1=ON/0=OFF<br>DefinePresetPSG(no:0, name:"piano",<br>&emsp;// AR   HR    DR   SL   RR     noise  tone<br>&emsp;0.0, 0.0,  1.0, 0.3, 1.0,        0,    1,<br>)|


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

## comments

|  notation  |  description  |example|
| ---- | ---- | ---- |
| // ○○○| This is a one-line comment. // After that, the comment is up to the line break.| // its comments. |
| /* ○○○ */ | It is a range comment.<br>Comments are from / * to * /. Comments that span multiple lines are also possible.| /* its<br>comments. */|

## Licence

[LICENSE](/LICENSE)

