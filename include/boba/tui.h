/* tui.h - Main public API for boba TUI library
 *
 * boba is a C library for building terminal user interfaces using
 * the Elm Architecture pattern (Model-View-Update).
 *
 * Include this header to get access to all boba functionality.
 */

#ifndef BOBA_TUI_H
#define BOBA_TUI_H

/* Core types and utilities */
#include "ansi_sequences.h"
#include "dynamic_buffer.h"
#include "style.h"
#include "unicode.h"

/* Elm Architecture types */
#include "cmd.h"
#include "component.h"
#include "msg.h"

/* Input parsing */
#include "input_parser.h"

/* Components */
#include "components/statusbar.h"

/* Runtime (optional, for standalone applications) */
#include "runtime.h"

#endif /* BOBA_TUI_H */
