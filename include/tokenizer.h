#ifndef TOKENIZER_H
#define TOKENIZER_H
#include <stdlib.h>

typedef enum {
    TOKEN_KEYWORD, //0
    TOKEN_IDENTIFIER, //1
    TOKEN_OPERATOR, //2
    TOKEN_COMMA, //3
    TOKEN_PARENTHESIS, //4
    TOKEN_SEMICOLON, //5
    TOKEN_NUMBER, //6
    TOKEN_STRING, //7
    TOKEN_COMMENT, //8
    TOKEN_UNKNOWN //9
} TokenType;

typedef struct {
    TokenType type;
    char *value; //Precies de tekst zoals die in het origineel staat
    unsigned int line_number;
    size_t length;
    int space_before; //Stond er in het origineel witruimte voor deze token?
} Token;

int find_possible_tokens(const char *input, Token **tokens, int *token_count);
TokenType tokenize(const char *input, int is_comment);

#endif //TOKENIZER_H
