# Music Macro Language Compiler

[日本語](/README.ja.md)

## Description

A class library that compiles (converts) MML into standard MIDI files. Implementation in C++17.
- It is in a form that can be built as a command line application.
- The MML grammar does not have a fixed standard, but it is implemented to follow something like an implicit standard.

## Demo Page

https://rlib-mml.thinkridge.jp/

It summarizes the process from MML creation to playback on the browser.

## Requirement

It can be compiled in a C++17 environment. The build requires boost.

We have confirmed the operation in the following environment.

- linux g++
- windows VisualStudio 2019

# MML (Music Macro Language)

## notes

|  notation  |  description  |example|
| ---- | ---- | ---- |
|a～[+-][length]| It is a note.<br>Immediately after, press +-to raise or lower a semitone (optional).<br>You can then specify the note length (optional). | "f4" → It is a quarter note F.<br>"cdefgab" → It is notes. The note length is the default value.<br>"a-16." → dotted quarter note B♭.
|r[length]|It is a rest. You can specify the note length immediately after it (optional).| "r16" → 16th rest.
|^[length]|It's Tie. You can specify the note length immediately after it (optional).<br>Add the note length of the previous note. Same as rest if immediately preceding is not a note.| "c4^4^4" → It is the length of three quarter notes.<br>"c4 t50 ^4 t20 ^4" → It is the length of three quarter notes. However, the tempo is changed on the way. It is possible to write another instruction between the note and ^.
|l[length]|This is the default note length.<br>This value is used when the note length is omitted for notes and rests.| "l8 cde" → This is an 8th note CDE.
|<<br>> | octave up(<) and down(>).<br>The range of one octave is 12 tones from C to B.| "c\<c>c\<c" → C and C one octave higher are repeated twice.
|o[octave]|Specify the octave. Values ​​are -2-8.<br>MIDI note number 60 is C3. Write "o3c"| "o3 cde"  → MIDI note number 60(C3)、62(D3)、64(E3)|
|t[tempo]|Specify the tempo. The value can be specified from 1.0 to 999.0. A small number can be written.| "t120 cde"  → CDE at tempo 120.|
|@[program no]|Specify the program number (tone). Values ​​are 0-127.| "@3 cde"  →  CDE atprogram number 3.|
|v[velocity]|Specifies the velocity (the strength with which you hit the keyboard on a piano). Values ​​are 0-127.| "v123 cde"  → CDE at velocity 123.|

### length
- This is how to specify the length of the note.

| notation  |  description  |example|
| ---- | ---- | ---- |
|1～192<br>note notation|Specify the value of the minute note.<br>It is also possible to write 12th notes and 24th notes.| "c4" → It is a quarter note C.|
|!1～!99999<br>Step number notation|Specify by the number of steps. The quarter note is 480.| "c!240" → 8th note C.|
|.<br>point notation| It means a dotted point called a minute note.<br>By writing more than one, it becomes the meaning of a double-dotted note (add half of the previous note length).| "c4." → dotted quarter note C.<br>"c4..." → note length that is the sum of quarter notes, 8th notes, 16th notes, and 32th notes.|
|+ - length<br>subtraction of numerical note length|Adds or subtracts the note length value.|"c8+4" → note length of 8th note plus quarter note.<br>"c32d32e1-32-32" → 32th note C and D, shortened by 2 32th notes from a whole note.<br><br>"d-4" →  In this case, it is a quarter note D♭. be careful.|

## function

|  notation  |  description  |example|
| ---- | ---- | ---- |
|createPort(<br>&emsp;name:[port name]<br>&emsp;channel:[channel number]<br>)| Define (declare) the port<br>Channel numbers are 1-16.|"createPort(name:Piano, channel:3)" <br>→ Declare the port of MIDI channel 3 with the name "Piano".|
|port([port name])| It is port switching|"port(Piano) cde"  → CDE at Port "Piano"|
|V([volume value])<br>volume([volume value])|The volume. Values ​​are 0-127.| "V(120) cde"  → CDE at Volume 120.|
|pan([pan value])<br>panpot([pan value])|It is pan (panpot).<br>Values ​​range from 0 (far left) to 127 (far right), with 64 in the center.| "pan(0) cde"  → CDE at Pan 0 (far left).|

## comments

|  notation  |  description  |example|
| ---- | ---- | ---- |
| // ○○○| This is a one-line comment. // After that, the comment is up to the line break.| // its comments. |
| /* ○○○ */ | It is a range comment.<br>Comments are from / * to * /. Comments that span multiple lines are also possible.| /* its<br>comments. */|

## Licence

[LICENSE](/LICENSE)

