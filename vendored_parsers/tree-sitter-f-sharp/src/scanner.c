#include "tree_sitter/alloc.h"
#include "tree_sitter/array.h"
#include "tree_sitter/parser.h"

enum TokenType {
  NEWLINE,
  INDENT,
  DEDENT,
  THEN,
  ELSE,
  ELIF,
  TRIPLE_QUOTE_CONTENT,
  BLOCK_COMMENT_CONTENT,
  LINE_COMMENT,
  ERROR_SENTINEL
};

typedef struct {
  Array(uint16_t) indents;
} Scanner;

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static inline bool scan_block_comment(TSLexer *lexer) {
  lexer->mark_end(lexer);
  if (lexer->lookahead != '(')
    return false;

  advance(lexer);
  if (lexer->lookahead != '*')
    return false;

  advance(lexer);

  while (true) {
    switch (lexer->lookahead) {
    case '(':
      scan_block_comment(lexer);
      break;
    case '*':
      advance(lexer);
      if (lexer->lookahead == ')') {
        advance(lexer);
        return true;
      }
      break;
    case '\0':
      return true;
    default:
      advance(lexer);
    }
  }
}

static inline bool is_infix_op_start(TSLexer *lexer) {
  switch (lexer->lookahead) {
  case '+':
  case '-':
  case '%':
  case '&':
  case '=':
  case '?':
  case '<':
  case '>':
  case '^':
    return true;
  case '/':
    skip(lexer);
    return lexer->lookahead != '/';
  case '.':
    skip(lexer);
    return lexer->lookahead != '.';
  case '!':
    skip(lexer);
    return lexer->lookahead == '=';
  case ':':
    skip(lexer);
    return lexer->lookahead == '=' || lexer->lookahead == ':';
  case 'o':
    skip(lexer);
    return lexer->lookahead == 'r';
  case '@':
  case '$':
    skip(lexer);
    return lexer->lookahead != '"';
  default:
    return false;
  }
}

static inline bool is_bracket_end(TSLexer *lexer) {
  switch (lexer->lookahead) {
  case ')':
  case ']':
  case '}':
    return true;
  default:
    return false;
  }
}

bool tree_sitter_fsharp_external_scanner_scan(void *payload, TSLexer *lexer,
                                              const bool *valid_symbols) {
  Scanner *scanner = (Scanner *)payload;

  bool error_recovery_mode = valid_symbols[ERROR_SENTINEL];

  if (valid_symbols[TRIPLE_QUOTE_CONTENT] && !error_recovery_mode) {
    lexer->mark_end(lexer);
    while (true) {
      if (lexer->lookahead == '\0') {
        break;
      }
      if (lexer->lookahead != '"') {
        advance(lexer);
      } else {
        lexer->mark_end(lexer);
        skip(lexer);
        if (lexer->lookahead == '"') {
          skip(lexer);
          if (lexer->lookahead == '"') {
            skip(lexer);
            break;
          }
        }
        lexer->mark_end(lexer);
      }
    }
    lexer->result_symbol = TRIPLE_QUOTE_CONTENT;
    return true;
  }

  lexer->mark_end(lexer);

  bool found_end_of_line = false;
  bool found_start_of_infix_op = false;
  bool found_bracket_end = false;
  uint32_t indent_length = lexer->get_column(lexer);

  for (;;) {
    if (lexer->lookahead == '\n') {
      found_end_of_line = true;
      indent_length = 0;
      skip(lexer);
    } else if (lexer->lookahead == ' ') {
      indent_length++;
      skip(lexer);
    } else if (lexer->lookahead == '\r' || lexer->lookahead == '\f') {
      indent_length = 0;
      skip(lexer);
    } else if (lexer->lookahead == '\t') {
      indent_length += 8;
      skip(lexer);
    } else if (lexer->eof(lexer)) {
      found_end_of_line = true;
      break;
    } else {
      break;
    }
  }

  // printf("lexer->lookahead = %c\n", lexer->lookahead);
  // printf("valid_symbols[INDENT] = %d\n", valid_symbols[INDENT]);
  // printf("valid_symbols[DEDENT] = %d\n", valid_symbols[DEDENT]);

  if (valid_symbols[NEWLINE] && lexer->lookahead == ';') {
    advance(lexer);
    lexer->mark_end(lexer);
    lexer->result_symbol = NEWLINE;
    return true;
  }

  if (valid_symbols[THEN] && lexer->lookahead == 't') {
    advance(lexer);
    if (lexer->lookahead == 'h') {
      advance(lexer);
      if (lexer->lookahead == 'e') {
        advance(lexer);
        if (lexer->lookahead == 'n') {
          advance(lexer);
          lexer->mark_end(lexer);
          lexer->result_symbol = THEN;
          return true;
        }
      }
    }
    return false;
  } else if (lexer->lookahead == 'e' &&
             (valid_symbols[ELSE] || valid_symbols[ELIF])) {
    advance(lexer);
    if (lexer->lookahead == 'l') {
      advance(lexer);
      if (lexer->lookahead == 's' && valid_symbols[ELSE]) {
        advance(lexer);
        if (lexer->lookahead == 'e') {
          advance(lexer);
          lexer->mark_end(lexer);
          lexer->result_symbol = ELSE;
          array_pop(&scanner->indents);
          return true;
        }
      } else if (lexer->lookahead == 'i' && valid_symbols[ELIF]) {
        advance(lexer);
        if (lexer->lookahead == 'f') {
          advance(lexer);
          lexer->mark_end(lexer);
          lexer->result_symbol = ELIF;
          array_pop(&scanner->indents);
          return true;
        }
      }
    }
    return false;
  } else if (is_infix_op_start(lexer)) {
    found_start_of_infix_op = true;
  } else if (lexer->lookahead == '|') {
    skip(lexer);
    switch (lexer->lookahead) {
    case ']':
    case '}':
      found_bracket_end = true;
      break;
    case ' ':
      break;
    default:
      found_start_of_infix_op = true;
      break;
    }
  } else if (is_bracket_end(lexer)) {
    found_bracket_end = true;
  }

  if (error_recovery_mode && scanner->indents.size > 0) {
    array_pop(&scanner->indents);
    lexer->result_symbol = DEDENT;
    return true;
  }

  if (valid_symbols[INDENT] && !found_start_of_infix_op && !found_bracket_end &&
      !error_recovery_mode) {
    array_push(&scanner->indents, indent_length);
    lexer->result_symbol = INDENT;
    return true;
  }

  if (scanner->indents.size > 0) {
    uint16_t current_indent_length = *array_back(&scanner->indents);

    if (found_bracket_end && valid_symbols[DEDENT]) {
      array_pop(&scanner->indents);
      lexer->result_symbol = DEDENT;
      return true;
    }

    if (found_end_of_line) {
      if (indent_length == current_indent_length && indent_length > 0 &&
          !found_start_of_infix_op && !found_bracket_end) {
        if (valid_symbols[NEWLINE] && !error_recovery_mode) {
          lexer->result_symbol = NEWLINE;
          return true;
        }
      }

      if (indent_length < current_indent_length && !found_bracket_end) {
        array_pop(&scanner->indents);
        lexer->result_symbol = DEDENT;
        return true;
      }
    }
  }

  if (valid_symbols[BLOCK_COMMENT_CONTENT] && !error_recovery_mode) {
    lexer->mark_end(lexer);
    while (true) {
      if (lexer->lookahead == '\0') {
        break;
      }
      if (lexer->lookahead != '(' && lexer->lookahead != '*') {
        advance(lexer);
      } else if (lexer->lookahead == '*') {
        lexer->mark_end(lexer);
        advance(lexer);
        if (lexer->lookahead == ')') {
          break;
        }
      } else if (scan_block_comment(lexer)) {
        lexer->mark_end(lexer);
        advance(lexer);
        if (lexer->lookahead == '*') {
          break;
        }
      }
    }
    lexer->result_symbol = BLOCK_COMMENT_CONTENT;
    return true;
  }

  return false;
}

unsigned tree_sitter_fsharp_external_scanner_serialize(void *payload,
                                                       char *buffer) {
  Scanner *scanner = (Scanner *)payload;
  size_t size = 0;

  uint32_t iter = 1;
  for (; iter < scanner->indents.size &&
         size < TREE_SITTER_SERIALIZATION_BUFFER_SIZE;
       ++iter) {
    buffer[size++] = (char)*array_get(&scanner->indents, iter);
  }

  return size;
}

void tree_sitter_fsharp_external_scanner_deserialize(void *payload,
                                                     const char *buffer,
                                                     unsigned length) {
  Scanner *scanner = (Scanner *)payload;

  array_delete(&scanner->indents);
  array_push(&scanner->indents, 0);

  if (length > 0) {
    size_t size = 0;

    for (; size < length; size++) {
      array_push(&scanner->indents, (unsigned char)buffer[size]);
    }
  }
}

void *tree_sitter_fsharp_external_scanner_create() {
  Scanner *scanner = ts_calloc(1, sizeof(Scanner));
  array_init(&scanner->indents);
  tree_sitter_fsharp_external_scanner_deserialize(scanner, NULL, 0);
  return scanner;
}

void tree_sitter_fsharp_external_scanner_destroy(void *payload) {
  Scanner *scanner = (Scanner *)payload;
  array_delete(&scanner->indents);
  ts_free(scanner);
}
