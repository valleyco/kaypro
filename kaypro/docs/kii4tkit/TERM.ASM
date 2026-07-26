*******************************************************************************
*                                                                             *
*                     KAYPRO Software / Terminal Version 2.1                  *
*                                                                             *
*                       Tyler Sperry  /  9 June 1982                          *
*                       Steven Fabian /  4 May  1983                          *
*******************************************************************************
*                  Copyright (c)1982 Non-Linear Systems, Inc.                 *
*                                                                             *
*******************************************************************************
* EQUATES    ******************************************************************
*******************************************************************************


WARM	EQU	0H		; CP/M warm start
FDOS	EQU	5H		; BDOS functions subroutine
CONOUT	EQU	2H		; BDOS function # for character output
DIRECT	EQU	6H		; BDOS function # for direct console I/O
STRING	EQU	9H		; BDOS function # for string output

SIODPA	EQU	4H		; Serial channel A data port
SIODPB	EQU	5H		; Serial channel B data port
SIOCPA	EQU	6H		; Serial channel A control/status port
SIOCPB	EQU	7H		; Serial channel B control/status port

EOT	EQU	04H
LF	EQU	0AH
CR	EQU	0DH
ESC	EQU	1BH
CRLF	EQU	0D0AH
BIOSDSP EQU	40H

BAUDA	EQU	0H		; Channel A baud rate port
B110	EQU	02H		; Baud rate IC divisor values
B300	EQU	05H
B600	EQU	06H
B1200	EQU	07H
B2400	EQU	0AH
B4800	EQU	0CH
B9600	EQU	0EH
B19K2	EQU	0FH


*******************************************************************************
* MAIN PROGRAM ****************************************************************
*******************************************************************************


	ORG	100H		; CP/M .COM file

INIT:	LD	HL,(1)		; Store value of BIOS
	LD	DE,BIOSDSP	; Store value of displacement
	ADD	HL,DE		; Add displacement and bios value
	LD	A,(HL)	        ; Load hyphen's exisiting value
	LD 	(HYCHAR),A	; Store existing value
	LD	A,0ADH		; Store value of Numeric Hypehen 
	LD	(HL),A		; Replace value in bios
	CALL	CLS		; Clear the screen
	CALL	HELLO		; and announce choices, etc.
	LD	C,SIOCPA	; Point to the SIO, channel A
	LD	A,(TABLE)	; 1st entry in TABLE is size thereof
	LD	B,A
	LD	HL,TABLE+1
	OTIR			; loop: init the SIO from TABLE

ENTRY:	LD	E,0FFH		; Input a character using
	LD	C,DIRECT	; Direct console I/O
	CALL	FDOS
	OR	A		; Was a key pressed?
	JR	Z,ENTRY		; If not, keep waiting
	CP	ESC
	JR	NZ,CRTEST	; Did they press <ESC>?
	JP	FINISH		; If so, return to CP/M
CRTEST:	CP	CR		; Did they press <CR>?
	JR	Z,MAIN  	; If so, do your terminal stuff
	CALL	BEEP		; Else, signal error
	JR	ENTRY		;  and wait for another entry

MAIN:	CALL	CLS
	CALL	REMIND		; Tell user how to get out
LOOP:	IN	A,(SIOCPA)	; Get SIO/a status byte
	BIT	0,A		; Rx char avail = carry flag
	JR	Z,KEYIN		; Nobody home, check keyboard
	IN	A,(SIODPA)	; Get the character
	AND	7FH		; Mask off possible parity bits
	LD	E,A		; Put the character
	LD	C,CONOUT	; to the console device
	CALL	FDOS
KEYIN:	CALL	KEYS		; Look for keyboard character
	JR	Z,LOOP		; If no character, do it again...
	LD	C,A
	CP	0ADH		; Check for termination key
	JP	Z,FINISH	; Over and out
	CALL	XMITA		; Else send the key to the modem
       	JR	LOOP		; Exit loop only on EOT from modem

	;end of program.

*******************************************************************************
* SUBROUTINES *****************************************************************
*******************************************************************************


BEEP:	LD	A,04		; Make it a long beep
	OUT	(SIODPB),A	; send it to the keyboard
	RET			; Short enough for you?

CLS:	LD	E,1AH		; ^Z to console clears screen
	LD	C,CONOUT
	CALL	FDOS
	RET

KEYS:	LD	E,0FFH			; Direct input request
	LD	C,DIRECT
	CALL	FDOS			; A = 0 if no key pressed and
	AND	A			; A = char available otherwise
	RET

XMITA:	IN	A,(SIOCPA)		; Get SIO/a status byte
	BIT	2,A
	JR	Z,XMITA			; Loop until SIO is ready for byte
	LD	A,C			; Data passed in C
	OUT	(SIODPA),A
	RET

HELLO:	LD	C,STRING
	LD	DE,MSG1
	CALL	FDOS		; Print the message below as a prompt
	RET
MSG1:	DW	CRLF
	DB	'KAYPRO Terminal Emulator'
	DW	CRLF	
	DW	CRLF
        DB      'This program allows your Kaypro computer to '
	DB      'imitate a dumb terminal.  What you  '
	DB	'type at the keyboard is sent through the RS'  
        DB      '-232 connector to the modem (or other'
        DB      'peripheral) and data from the modem is '
        DB      'put on this video display.  '
        DB      'Technically, this program is like a Lear-Siegler '
        DB      'ADM-3A terminal and is set for '
	DB	'full-duplex, no parity check and 8 bits per '
	DB	'character.'
	DW	CRLF
	DW	CRLF
	DB	'Press either <RETURN> to start using the '
        DB	'terminal or <Esc> to return to CPM:.'
 	DB	'$'

REMIND:	LD	C,STRING	; Tell user how to exit terminal program
	LD	DE,MSG2
	CALL	FDOS
	RET
MSG2:	DW	CRLF
	DB	'Press the Numeric Pads Hyphen Key '
	DB	 'when you are done using TERM.COM'
	DW	CRLF
	DB	'and the program will exit to CP/M.'
	DW	CRLF
	DB	LF
	DB	'$'  
FINISH:	LD	HL,(1)	    ;FOR THESE COMMENTS SEE INIT
	LD	DE,BIOSDSP
	ADD	HL,DE
	LD	A,(HYCHAR)  ;RESET BIOS HYPHEN CHARACTER VALUE	
	LD	(HL),A
	JP	WARM 


*******************************************************************************
* STORAGE *********************************************************************
*******************************************************************************


TABLE:	DB	9		; Size of TABLE

	DB	18H		; Channel RESET to SIO
	DB	4,44H		; WR4: Clock divider, 1 stop bit,no parity
	DB	1,0		; WR1: no interrupts
	DB	3,0C1H		; WR3: 8 bits/ch, no auto enable, enable RCVR
	DB	5,0EAH		; WR5: 8 bits/ch, set DTR & RTS

HYCHAR: DB	0		; Size  and initial value
	END

*******************************************************************************
 terminal program
	LD	DE,MSG2
	CALL	FDOS
	RET
MSG2:	DW	CRLF
	DB	'Pr