#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <tokenizer.h>
#include <ctype.h>


char* clean_commas(const char* str) {
    // Deze functie gebruiken we om de commas eruit te halen, zodat die geen rommel veroorzaken.
    if (!str) return NULL;
    
    char* cleaned = strdup(str);
    char* src = cleaned;
    char* dst = cleaned;
    
    while (*src) {
        if (*src != ',' && *src != '\n' && *src != '\r') {
            *dst = *src;
            dst++;
        }
        src++;
    }
    *dst = '\0';
    
    return cleaned;
}

int add_token(Token **tokens, int *token_count, char *temp_string, unsigned int *char_counter,
               int is_comment, unsigned int line_number, int *space_before) {
    /* Voeg de token die in temp_string staat toe aan onze array. Return 0 als het geheugen op is. */
    if (*char_counter == 0) {
        return 1; //Niks om toe te voegen.
    }
    temp_string[*char_counter] = '\0';

    *tokens = realloc(*tokens, ((*token_count) + 1) * sizeof(Token)); //Moet beter, maarja.
    if (!*tokens) {
        return 0;
    }
    (*tokens)[*token_count].value = strdup(temp_string);
    (*tokens)[*token_count].raw_value = strdup(temp_string);
    (*tokens)[*token_count].type = tokenize(temp_string, is_comment);
    (*tokens)[*token_count].line_number = line_number;
    (*tokens)[*token_count].length = strlen(temp_string);
    (*tokens)[*token_count].raw_length = strlen(temp_string);
    (*tokens)[*token_count].space_before = *space_before;

    *token_count = (*token_count) + 1;
    *char_counter = 0;
    *space_before = 0;
    return 1;
}

void find_possible_tokens(const char *input, Token **tokens, int *token_count) {
    // printf("tokenizing tokens\n");

    //Het kan lang worden
    size_t length_input = strlen(input);
    unsigned int char_counter = 0;

    //Een token kan nooit langer zijn dan de input zelf, dus zo kunnen we nooit buiten de buffer schrijven.
    char *temp_string = malloc((length_input + 1) * sizeof(char));
    if (!temp_string) {
        return;
    }

    unsigned int line_number_temp = 0;
    unsigned int token_line_number = 0;
    int space_before = 0;
    //Elke spatie en nieuweline is een nieuwe token. Strings en comments blijven wel in hun geheel een token,
    //en haakjes, commas en puntkomma's zijn altijd een eigen token.
    for (size_t i = 0; i < length_input; i++) {
        char c = input[i];

        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            if (!add_token(tokens, token_count, temp_string, &char_counter, 0, token_line_number, &space_before)) break;
            space_before = 1;
            if (c == '\n') {
                line_number_temp++;
            }
            continue;
        }

        // Een -- comment loopt tot het einde van de regel.
        if (c == '-' && input[i + 1] == '-') {
            if (!add_token(tokens, token_count, temp_string, &char_counter, 0, token_line_number, &space_before)) break;
            token_line_number = line_number_temp;
            while (i < length_input && input[i] != '\n' && input[i] != '\r') {
                temp_string[char_counter++] = input[i++];
            }
            while (char_counter > 0 && (temp_string[char_counter - 1] == ' ' || temp_string[char_counter - 1] == '\t')) {
                char_counter--; //Spaties aan het einde van de comment doen er niet toe.
            }
            if (!add_token(tokens, token_count, temp_string, &char_counter, 1, token_line_number, &space_before)) break;
            i--; //Zodat de enter zelf ook weer door de loop verwerkt wordt.
            continue;
        }

        // Een /* comment loopt tot de */, ook over meerdere regels heen.
        if (c == '/' && input[i + 1] == '*') {
            if (!add_token(tokens, token_count, temp_string, &char_counter, 0, token_line_number, &space_before)) break;
            token_line_number = line_number_temp;
            temp_string[char_counter++] = input[i++];
            temp_string[char_counter++] = input[i++];
            while (i < length_input && !(input[i] == '*' && input[i + 1] == '/')) {
                if (input[i] == '\n') {
                    line_number_temp++;
                }
                temp_string[char_counter++] = input[i++];
            }
            if (i < length_input) {
                temp_string[char_counter++] = input[i++];
                temp_string[char_counter++] = input[i];
            }
            if (!add_token(tokens, token_count, temp_string, &char_counter, 1, token_line_number, &space_before)) break;
            continue;
        }

        // Haakjes, commas en puntkomma's zijn altijd een losse token.
        if (c == '(' || c == ')' || c == ',' || c == ';') {
            if (!add_token(tokens, token_count, temp_string, &char_counter, 0, token_line_number, &space_before)) break;
            token_line_number = line_number_temp;
            temp_string[char_counter++] = c;
            if (!add_token(tokens, token_count, temp_string, &char_counter, 0, token_line_number, &space_before)) break;
            continue;
        }

        if (char_counter == 0) {
            token_line_number = line_number_temp;
        }
        temp_string[char_counter++] = c;

        // Alles tussen quotes (of blokhaken) hoort bij dezelfde token, inclusief spaties en enters.
        if (c == '\'' || c == '"' || c == '`' || c == '[') {
            char closing = (c == '[') ? ']' : c;
            i++;
            while (i < length_input) {
                temp_string[char_counter++] = input[i];
                if (input[i] == '\n') {
                    line_number_temp++;
                }
                if (input[i] == closing) {
                    if (closing != ']' && input[i + 1] == closing) { //Twee quotes achter elkaar is een escape, geen einde.
                        i++;
                        temp_string[char_counter++] = input[i];
                    } else {
                        break;
                    }
                }
                i++;
            }
        }
    }
    //De laatste token toevoegen, ook als het bestand niet met witruimte eindigt.
    add_token(tokens, token_count, temp_string, &char_counter, 0, token_line_number, &space_before);

    free(temp_string);

}


TokenType tokenize(const char *input, int is_comment) {
    if (input == NULL || *input == '\0') {
        return TOKEN_UNKNOWN;
    }

    if (is_comment == 1) {
        return TOKEN_COMMENT;
    }

    if (input[0] == '-' && input[1] == '-') {
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
    // Hier zit een erg grote aanname in dat de string een enkel woord is
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
    // TODO: Opschonen van de deze vergelijkingen, misschien een switch statement?
    if (strchr(",", input[0]) != NULL && length_str == 1) {
        return TOKEN_COMMA;
    }

    if (strchr("()", input[0]) != NULL && length_str == 1) {
        return TOKEN_PARENTHESIS;
    }
    
    if (strchr(";", input[0]) != NULL && length_str == 1) {
        return TOKEN_SEMICOLON;
    }

    // Check of we een select item hebben
    // Dit is een beetje raar omdat we al een clean_commas hebben
    if (isalnum(input[0]) || input[0] == '_' || input[0] == ',' || input[0] == '[' || input[0] == ']' ) { //Begint het met een letter of lager streepje?
        int comma_present = 0;
        for (size_t i = 0; i < length_str; i++)  { //Dan lopen we door de string heen
            if (input[i] == ',') { //Hebben we een comma?
                comma_present = 1;
            } else if (!isalnum(input[i]) && input[i] != '_' && input[i] != '[' && input[i] != ']'
                && input[i] != ',' && input[i] != '.'
                && input[i] != '*' && input[i] != '(' && input[i] != ')') { //Als we een non-alfanumeriek character tegekomen die geen laag streepje is 
                return TOKEN_UNKNOWN; //Geef dan terug dat we niet weten wat voor token we hebben.    
            }
        }
        if (comma_present == 1) {
            return TOKEN_SELECTITEM;
        } else {
            return TOKEN_IDENTIFIER; //Als alles alfanumeriek is of een laag streepje, weten we dat we inderdaad met een identifier te maken hebben
        }
        
    }

    return TOKEN_UNKNOWN; //Als we op 1 of andere manier al deze stappen ontstappen, dan eindigen we hier. Dan is het onbekend.
}
