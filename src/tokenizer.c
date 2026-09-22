#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <tokenizer.h>
#include <ctype.h>


int add_token(Token **tokens, int *token_count, const char *start, size_t length,
              int is_comment, unsigned int line_number, int space_before) {
    /* Voeg het stuk input van start tot start + length toe als token. Return 0 als het geheugen op is. */
    Token *grown = realloc(*tokens, ((*token_count) + 1) * sizeof(Token)); //Moet beter, maarja.
    char *value = strndup(start, length);
    if (!grown || !value) {
        free(value);
        return 0;
    }
    *tokens = grown;
    (*tokens)[*token_count].value = value;
    (*tokens)[*token_count].type = tokenize(value, is_comment);
    (*tokens)[*token_count].line_number = line_number;
    (*tokens)[*token_count].length = length;
    (*tokens)[*token_count].space_before = space_before;
    *token_count = (*token_count) + 1;
    return 1;
}

int is_token_boundary(const char *input, size_t i) {
    // Witruimte, haakjes, commas, puntkomma's en het begin van een comment breken een woord af.
    return isspace((unsigned char)input[i]) || strchr("(),;", input[i]) != NULL
           || (input[i] == '-' && input[i + 1] == '-') || (input[i] == '/' && input[i + 1] == '*');
}

int find_possible_tokens(const char *input, Token **tokens, int *token_count) {
    /* Knipt de input op in tokens. Elke token is precies een stuk van de input, zodat we nooit tekst kwijtraken.
    Strings en comments blijven in hun geheel een token, en haakjes, commas en puntkomma's zijn altijd een eigen token.
    Return 0 als het geheugen op is. */
    size_t length_input = strlen(input);
    unsigned int line_number = 0;
    int space_before = 0;
    size_t i = 0;

    while (i < length_input) {
        if (isspace((unsigned char)input[i])) {
            if (input[i] == '\n') {
                line_number++;
            }
            space_before = 1;
            i++;
            continue;
        }

        size_t start = i;
        int is_comment = 0;
        if (input[i] == '-' && input[i + 1] == '-') {
            // Een -- comment loopt tot het einde van de regel.
            is_comment = 1;
            while (i < length_input && input[i] != '\n' && input[i] != '\r') {
                i++;
            }
        } else if (input[i] == '/' && input[i + 1] == '*') {
            // Een /* comment loopt tot de */, ook over meerdere regels heen.
            is_comment = 1;
            const char *comment_end = strstr(input + i + 2, "*/");
            i = comment_end ? (size_t)(comment_end - input) + 2 : length_input;
        } else if (strchr("(),;", input[i]) != NULL) {
            i++;
        } else {
            // Een gewoon woord. Alles tussen quotes (of blokhaken) hoort erbij, inclusief spaties en enters.
            while (i < length_input && !is_token_boundary(input, i)) {
                if (strchr("'\"`[", input[i]) != NULL) {
                    char closing = input[i] == '[' ? ']' : input[i];
                    i++;
                    while (i < length_input && !(input[i] == closing && input[i + 1] != closing)) {
                        i += (input[i] == closing) ? 2 : 1; //Twee quotes achter elkaar is een escape, geen einde.
                    }
                }
                if (i < length_input) {
                    i++;
                }
            }
        }

        size_t end = i;
        while (is_comment && end > start && (input[end - 1] == ' ' || input[end - 1] == '\t')) {
            end--; //Spaties aan het einde van een comment doen er niet toe.
        }
        if (!add_token(tokens, token_count, input + start, end - start, is_comment, line_number, space_before)) {
            return 0;
        }
        for (size_t k = start; k < i; k++) {
            if (input[k] == '\n') {
                line_number++; //Enters binnen strings en comments tellen ook mee.
            }
        }
        space_before = 0;
    }
    return 1;
}


TokenType tokenize(const char *input, int is_comment) {
    if (input == NULL || *input == '\0') {
        return TOKEN_UNKNOWN;
    }

    if (is_comment == 1) {
        return TOKEN_COMMENT;
    }

    // Check for keywords
    const char *keywords[] = {
        "SELECT", "FROM", "WHERE", "INSERT", "UPDATE", "DELETE",
        "CREATE", "DROP", "ALTER", "JOIN", "ON", "GROUP", "ORDER",
        "LEFT", "RIGHT", "INNER", "BY", "AS", "INTO", "WITH", "UNION",
        "LIMIT", "OFFSET", "HAVING", "FULL", "OUTER", "CROSS", "USE",
        "CASE", "WHEN", "THEN", "ELSE", "END", "BETWEEN", "DISTINCT", "ASC", "DESC", NULL
    };


    for (const char **keyword = keywords; *keyword != NULL; keyword++) {
        if (strcasecmp(input, *keyword) == 0) {
            return TOKEN_KEYWORD;
        }
    }

    // Kijk of het een nummer is.
    if (isdigit(input[0]) || (input[0] == '-' && isdigit(input[1]))) {
        size_t length_str = strlen(input);
        size_t i; //checken of er een minus sign is
        if (input[0] == '-') {
            i = 1;
        } else {
            i = 0;
        }
        for (; i < length_str; i++) { //loop over de string na eventueel minus sign
            if (!isdigit(input[i]) && input[i] != '.') { //als we dan geen number tegenkomen dat ook geen punt is (decimaal seperator), dan breaken we.
                break;
            }
        }
        if (i == length_str) { //Als we de volledige string hebben gezien en we geen characters zijn tegengekomen die non-numeriek zijn, hebben we een cijfer te pakken
            return TOKEN_NUMBER;
        }
    }

    // Kijken of er we een string hebben gevonden (gemarkeerd door single quotes aan het begin en einde.
    size_t length_str = strlen(input);
    if (length_str > 1 && input[0] == '\'' && input[length_str - 1] == '\'') {
        return TOKEN_STRING;
    }

    // Check voor operators
    const char *operators[] = {
        "+", "-", "*", "/", "=", "<", ">", "<=", ">=", "<>", "!=", "AND", "OR", "NOT", NULL
    };
    for (const char **operator = operators; *operator != NULL; operator++) {
        if (strcasecmp(input, *operator) == 0) { //Als het gelijk is aan elkaar dan hebben we een operator gevonden.
            return TOKEN_OPERATOR;
        }
    }

    // Check voor symbolen
    if (length_str == 1) {
        switch (input[0]) {
            case ',': return TOKEN_COMMA;
            case '(': case ')': return TOKEN_PARENTHESIS;
            case ';': return TOKEN_SEMICOLON;
        }
    }

    // Check of we een identifier hebben: begint met een letter, laag streepje of blokhaak en bevat verder niks geks.
    if (isalnum(input[0]) || input[0] == '_' || input[0] == '[') {
        for (size_t i = 0; i < length_str; i++) {
            if (!isalnum(input[i]) && strchr("_[].*", input[i]) == NULL) {
                return TOKEN_UNKNOWN; //Geef dan terug dat we niet weten wat voor token we hebben.
            }
        }
        return TOKEN_IDENTIFIER;
    }

    return TOKEN_UNKNOWN; //Als we op 1 of andere manier al deze stappen ontstappen, dan eindigen we hier. Dan is het onbekend.
}
