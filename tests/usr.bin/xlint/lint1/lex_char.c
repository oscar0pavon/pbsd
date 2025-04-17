/*	$NetBSD: lex_char.c,v 1.9 2024/02/02 19:07:58 rillig Exp $	*/
# 3 "lex_char.c"

/*
 * Tests for lexical analysis of character constants.
 *
 * C99 6.4.4.4 "Character constants"
 */

/* lint1-extra-flags: -X 351 */

void sink(char);

void
test(void)
{
	/* expect+1: error: empty character constant [73] */
	sink('');

	sink('a');

	sink('\0');

	/* UTF-8 */
	/* expect+2: warning: multi-character character constant [294] */
	/* expect+1: warning: conversion of 'int' to 'char' is out of range, arg #1 [295] */
	sink('ä');

	/* GCC extension */
	sink('\e');

	/* expect+1: warning: dubious escape \y [79] */
	sink('\y');

	/* since C99 */
	sink('\x12');

	/* octal */
	sink('\177');

	/* expect+1: error: empty character constant [73] */
	sink('');

	/* U+0007 alarm/bell */
	sink('\a');

	/* U+0008 backspace */
	sink('\b');

	/* U+0009 horizontal tabulation */
	sink('\t');

	/* U+000A line feed */
	sink('\n');

	/* U+000B vertical tabulation */
	sink('\v');

	/* U+000C form feed */
	sink('\f');

	/* U+000D carriage return */
	sink('\r');

	/* A double quote may be escaped or not, since C90. */
	sink('"');
	sink('\"');

	/* A question mark may be escaped or not, since C90. */
	sink('?');
	sink('\?');

	sink('\\');

	sink('\'');
}

/*
 * The sequence backslash-newline is handled in an early stage of
 * translation (C90 5.1.1.2 item 2, C99 5.1.1.2 item 2, C11 5.1.1.2 item 2),
 * which allows it in character literals as well.  This doesn't typically
 * occur in practice though.
 */
char ch = '\
\
\
\
\
x';
