/* input_parser.h - Terminal input parsing for boba TUI library
 *
 * Parses raw terminal input bytes into TuiMsg structures.
 * Handles ANSI escape sequences, UTF-8, and control characters.
 */

#ifndef BOBA_INPUT_PARSER_H
#define BOBA_INPUT_PARSER_H

#include "msg.h"
#include <stddef.h>

/* Input parser state */
typedef struct TuiInputParser TuiInputParser;

/* Create a new input parser */
TuiInputParser *tui_input_parser_create(void);

/* Free input parser */
void tui_input_parser_free(TuiInputParser *parser);

/* Reset parser state (clear any partial sequences) */
void tui_input_parser_reset(TuiInputParser *parser);

/* Parse input bytes and return messages
 *
 * Parameters:
 *   parser: Parser state
 *   input: Input bytes to parse
 *   input_len: Number of bytes in input
 *   msgs: Output array for messages (caller allocated)
 *   max_msgs: Maximum number of messages to return
 *
 * Returns: Number of messages parsed
 */
int tui_input_parser_parse(TuiInputParser *parser, const unsigned char *input,
                           size_t input_len, TuiMsg *msgs, int max_msgs);

/* Parse a single byte and return message if complete
 *
 * Parameters:
 *   parser: Parser state
 *   byte: Input byte
 *   msg: Output message (set if complete)
 *
 * Returns: 1 if message complete, 0 if more input needed
 */
int tui_input_parser_feed(TuiInputParser *parser, unsigned char byte,
                          TuiMsg *msg);

#endif /* BOBA_INPUT_PARSER_H */
