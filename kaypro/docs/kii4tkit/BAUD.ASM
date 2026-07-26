*******************************************************************************
*                                                                             *
*                  Non-Linear Systems / Baud-rate Changer                     *
*                                                                             *
*                       Tyler Sperry  /  7 June 1982                          *
*                                                                             *
*******************************************************************************



*******************************    EQUATES    *********************************

WARM	EQU	0H		; CP/M warm start
FDOS	EQU	5H		; BDOS functions subroutine
CHAR	EQU	2H		; BDOS function # for character output
DIRECT	EQU	6H		; BDOS function # for direct console I/O
STRING	EQU	9H		; BDOS function # for string output

SIODPA	EQU	4H		; Serial channel A data port
SIODPB	EQU	5H		; Serial channel B data port
SIOCPA	EQU	6H		; Serial channel A control/status port
SIOCPB	EQU	7H		; Serial channel B control/status port

LF	EQU	0AH
FF	EQU	0CH
CR	EQU	0DH
ESC	EQU	1BH
CRLF	EQU	0D0AH

BAUDA	EQU	0H		; COM8116 channel A baud rate port
B110	EQU	02H		; COM8116 baud rate divisor values
B300	EQU	05H
B600	EQU	06H
B1200	EQU	07H
B2400	EQU	0AH
B4800	EQU	0CH
B9600	EQU	0EH
B19K2	EQU	0FH


****************************   MAIN PROGRAM   *********************************


	ORG	100H		; CP/M .COM file

	CALL	CLS		; Clear the screen
	CALL	HELLO		; and announce yourself, choices, etc.

INPUT:	LD	E,0FFH		; Input a character using
	LD	C,DIRECT	; Direct console I/O
	CALL	FDOS
	RES	5,A		; Convert to upper case and
	LD	(ENTRY),A	; save the entered character
	OR	A		; Was it a 0 (no key pressed?)
	JR	Z,INPUT		; If so, get another character
	CP	ESC
	JR	NZ,TESTA	; If not an escape...
	RET			;   else exit gracefully w/o a warm boot

TESTA:	CP	'A'		; First choice = 110 baud
	JR	NC,AOK		; If byte not < 'A'
	LD	A,04		; Else make a long beep
	OUT	(SIODPB),A
	JR	INPUT		; and ask for another choice...

AOK:	JR	NZ,TESTB	; Not 'A' means try higher
	LD	A,B110
	OUT	(BAUDA),A
	CALL	DONE		; Tell customer OK
	RET

TESTB:	CP	'B'
	JR	NZ,TESTC	; Not 'B' means try higher
	LD	A,B300
	OUT	(BAUDA),A
	CALL	DONE
	RET

TESTC:	CP	'C'
	JR	NZ,TESTD	; Not 'C' ...
	LD	A,B600
	OUT	(BAUDA),A
	CALL	DONE
	RET

TESTD:	CP	'D'
	JR	NZ,TESTE	; Not 'D'...
	LD	A,B1200
	OUT	(BAUDA),A
	CALL	DONE
	RET

TESTE:	CP	'E'
	JR	NZ,TESTF
	LD	A,B2400
	OUT	(BAUDA),A
	CALL	DONE
	RET

TESTF:	CP	'F'
	JR	NZ,TESTG
	LD	A,B4800
	OUT	(BAUDA),A
	CALL	DONE
	RET

TESTG:	CP	'G'
	JR	NZ,TESTH
	LD	A,B9600
	OUT	(BAUDA),A
	CALL	DONE
	RET

TESTH:	CP	'H'
	JR	Z,HOK
	CALL	BEEP		; > 'H' means ignore it
	JP	INPUT
HOK:	LD	A,B19K2
	OUT	(BAUDA),A
	CALL	DONE
	RET


ENTRY:	DS	1		; Immune storage of entered character

****************************    SUBROUTINES    ********************************

BEEP:	LD	A,04		; Make it a long beep
	OUT	(SIODPB),A	; send it to the keyboard
	RET			; Short enough for you

CLS:	LD	E,1AH		; ^Z to console clears screen
	LD	C,CHAR
	CALL	FDOS
	RET

HELLO:	LD	C,STRING
	LD	DE,MSG1
	CALL	FDOS		; Print the message below as a prompt
	RET

MSG1:	DW	CRLF
	DB	'KAYPRO Baud Rate'
	DB	' Adjustment Program'
	DW	CRLF
	DW	CRLF
	DB	'Enter a letter below to change the baud'
	DB	' rate of your RS-232C serial port,'
	DW	CRLF
	DB	' or press <ESC> to exit'
	DB	' without changing anything.'
	DW	CRLF
	DW	CRLF
	DB	' A =  110 baud'
	DW	CRLF
	DB	' B =  300 baud'
	DW	CRLF
	DB	' C =  600 baud'
	DW	CRLF
	DB	' D = 1200 baud'
	DW	CRLF
	DB	' E = 2400 baud'
	DW	CRLF
	DB	' F = 4800 baud'
	DW	CRLF
	DB	' G = 9600 baud'
	DW	CRLF
	DB	' H = 19.2 Kbaud'
	DW	CRLF
	DW	CRLF
	DB	' Enter new baud rate :  '
	DB	'$'

DONE:	LD	C,CHAR
	LD	A,(ENTRY)
	LD	E,A
	CALL	FDOS			; Repeat the chosen letter...
	LD	C,STRING
	LD	DE,MSG2
	CALL	FDOS			; Announce success 
	RET				; And return to CP/M

MSG2:	DB	'   So be it.'
	DW	CRLF
	DB	'$'

	END