#pragma once

#include <stddef.h>

/*
 * The built-in Montserrat fonts are ASCII plus a couple of symbols, so text
 * from APIs is folded before it reaches a label: accented Latin letters lose
 * the accent, typographic quotes and dashes become plain ones, and anything
 * else outside ASCII becomes '?'. Always NUL-terminates. Pure C, host-tested.
 */
void text_fold_ascii(char *dst, size_t size, const char *utf8);
