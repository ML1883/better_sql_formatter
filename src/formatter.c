#include <tokenizer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <formatter.h>
#include <ctype.h>

// Hoeveel niveaus we extra inspringen omdat we in een subquery zitten.
static int base_indentation = 0;
// Hebben we net een -- comment geschreven? Dan moet de volgende token op een nieuwe regel, anders commenten we code weg.
static int line_comment_open = 0;
static size_t line_comment_end = 0;

//Om tabjes te inserten van 4 maal spatie
void add_indentation(char* result, size_t* pos, int level) {
    for (int i = 0; i < (base_indentation + level) * 4; i++) {
        result[(*pos)++] = ' ';
    }
}

char* to_uppercase(const char* str) {
    if (!str) return NULL;  // NULL input geeft null terug
    
    size_t len = strlen(str);
    char* upperStr = malloc(len + 1);  // +1 voor eventueele null terminator
    if (!upperStr) return NULL; //Nog een keer checken of dit goed gaat.
    
    for (size_t i = 0; i < len; i++) {
        upperStr[i] = toupper((unsigned char)str[i]);
    }
    upperStr[len] = '\0';
    
    return upperStr;
}

int is_at_line_start(const char* result, size_t pos) {
    // Staan er op de huidige regel alleen nog maar spaties?
    while (pos > 0 && result[pos - 1] == ' ') {
        pos--;
    }
    return pos == 0 || result[pos - 1] == '\n';
}

void ensure_newlines(char* result, size_t* pos, int amount) {
    // Zorg dat de output eindigt op minimaal amount enters. Aan het begin van de output doen we niks.
    while (*pos > 0 && result[*pos - 1] == ' ') {
        (*pos)--;
    }
    if (*pos == 0) {
        return;
    }
    int present = 0;
    while ((size_t)present < *pos && result[*pos - 1 - present] == '\n') {
        present++;
    }
    for (; present < amount; present++) {
        *pos += sprintf(result + *pos, "\n");
    }
}

void write_token(Token* tokens, int i, char* result, size_t* pos) {
    /* Schrijft een enkele token weg. ALLE tokens moeten via deze functie de output in, zo raken we nooit tekst kwijt.
    Een spatie ervoor zetten we alleen als die er in het origineel ook stond.*/
    int own_line_comment = tokens[i].type == TOKEN_COMMENT && i > 0
                           && tokens[i].line_number > tokens[i - 1].line_number;
    int comment_still_open = line_comment_open
                             && memchr(result + line_comment_end, '\n', *pos - line_comment_end) == NULL;

    if ((own_line_comment || comment_still_open) && !is_at_line_start(result, *pos)) {
        // Nieuwe regel met dezelfde inspringing als de huidige regel.
        size_t line_start = *pos;
        while (line_start > 0 && result[line_start - 1] != '\n') {
            line_start--;
        }
        int spaces = 0;
        while (line_start + spaces < *pos && result[line_start + spaces] == ' ') {
            spaces++;
        }
        *pos += sprintf(result + *pos, "\n");
        for (int s = 0; s < spaces; s++) {
            result[(*pos)++] = ' ';
        }
    }
    line_comment_open = 0;

    if (tokens[i].space_before && *pos > 0 && result[*pos - 1] != ' ' && result[*pos - 1] != '\n'
        && !(result[*pos - 1] == ',' && is_at_line_start(result, *pos - 1))) { //Geen spatie na een comma aan het begin van de regel
        *pos += sprintf(result + *pos, " ");
    }

    if (tokens[i].type == TOKEN_KEYWORD || tokens[i].type == TOKEN_OPERATOR) {
        char* uppercase_value = to_uppercase(tokens[i].raw_value);
        *pos += sprintf(result + *pos, "%s", uppercase_value);
        free(uppercase_value);
    } else {
        *pos += sprintf(result + *pos, "%s", tokens[i].raw_value);
    }

    if (tokens[i].type == TOKEN_COMMENT && tokens[i].raw_value[0] == '-') {
        line_comment_open = 1;
        line_comment_end = *pos;
    }
}

int is_open_parenthesis(Token* tokens, int i) {
    return tokens[i].type == TOKEN_PARENTHESIS && tokens[i].raw_value[0] == '(';
}

int is_close_parenthesis(Token* tokens, int i) {
    return tokens[i].type == TOKEN_PARENTHESIS && tokens[i].raw_value[0] == ')';
}

int find_closing_parenthesis(Token* tokens, int start_idx, int end_idx) {
    // Geeft de index van het bijbehorende sluitende haakje, of -1 als die er niet is.
    int depth = 0;
    for (int i = start_idx; i <= end_idx; i++) {
        if (is_open_parenthesis(tokens, i)) {
            depth++;
        } else if (is_close_parenthesis(tokens, i)) {
            depth--;
            if (depth == 0) {
                return i;
            }
        }
    }
    return -1;
}

int is_subquery_start(Token* tokens, int idx, int end_idx) {
    // Is dit een haakje openen met direct (eventueel na comments) een SELECT of WITH erachter?
    if (!is_open_parenthesis(tokens, idx)) {
        return 0;
    }
    int next = idx + 1;
    while (next <= end_idx && tokens[next].type == TOKEN_COMMENT) {
        next++;
    }
    if (next > end_idx) {
        return 0;
    }
    return strcasecmp(tokens[next].value, "SELECT") == 0 || strcasecmp(tokens[next].value, "WITH") == 0;
}

int format_subquery(Token* tokens, int start_idx, int end_idx, char* result, size_t* pos, int level) {
    /* Schrijft een subquery tussen haakjes uit als volledig geformatteerd blok.
    level is de inspringing van de regel waar het haakje op staat. Return de index van de laatst geschreven token.*/
    int close_idx = find_closing_parenthesis(tokens, start_idx, end_idx);
    int inner_end = (close_idx == -1) ? end_idx : close_idx - 1;

    write_token(tokens, start_idx, result, pos);
    base_indentation += level + 1;
    format_statements(tokens, start_idx + 1, inner_end, result, pos);
    base_indentation -= level + 1;

    if (close_idx == -1) {
        return end_idx;
    }
    ensure_newlines(result, pos, 2);
    add_indentation(result, pos, level);
    write_token(tokens, close_idx, result, pos);
    return close_idx;
}

int format_inline_subquery(Token* tokens, int start_idx, int end_idx, char* result, size_t* pos, int level) {
    /* Schrijft een subquery tussen haakjes uit op een eigen regel, maar verder zoals die er staat.
    Return de index van de laatst geschreven token.*/
    int close_idx = find_closing_parenthesis(tokens, start_idx, end_idx);
    int inner_end = (close_idx == -1) ? end_idx : close_idx - 1;

    write_token(tokens, start_idx, result, pos);
    *pos += sprintf(result + *pos, "\n");
    add_indentation(result, pos, level + 1);
    for (int i = start_idx + 1; i <= inner_end; i++) {
        write_token(tokens, i, result, pos);
    }

    if (close_idx == -1) {
        return end_idx;
    }
    *pos += sprintf(result + *pos, "\n");
    add_indentation(result, pos, level);
    write_token(tokens, close_idx, result, pos);
    return close_idx;
}


void format_select_section(Token* tokens, int start_idx, int end_idx, char* result, size_t* pos, const char* keyword) {
    /*Deze functie is niet alleen voor select onderdelen, maar alles wat erop lijkt. 
    Daarom heeft het ook het keyword nodig.*/
    add_indentation(result, pos, 0);
    *pos += sprintf(result + *pos, "%s", keyword);
    // Ga vervolgens door de items van de select heen. Een nieuw item begint alleen bij een comma buiten haakjes.
    int depth = 0;

    for (int i = start_idx; i <= end_idx; i++) {
        if (i == start_idx) {
            *pos += sprintf(result + *pos, "\n");
            add_indentation(result, pos, 1);
        } else if (depth == 0 && tokens[i].type == TOKEN_COMMA) {
            // Comments die achter de comma stonden, horen nog bij het vorige item.
            int comma_idx = i;
            while (i + 1 <= end_idx && tokens[i + 1].type == TOKEN_COMMENT) {
                i++;
                write_token(tokens, i, result, pos);
            }
            *pos += sprintf(result + *pos, "\n");
            add_indentation(result, pos, 1);
            write_token(tokens, comma_idx, result, pos);
            continue;
        }

        // Als we een case hebben moeten we daar speciaal mee omgaan.
        if (depth == 0 && strcasecmp(tokens[i].value, "CASE") == 0) {
            i += format_case_block(tokens, i, end_idx, result, pos, 1) - 1;
            continue;
        }

        if (is_open_parenthesis(tokens, i)) {
            depth++;
        } else if (is_close_parenthesis(tokens, i)) {
            depth--;
        }
        write_token(tokens, i, result, pos);
    }
    *pos += sprintf(result + *pos, "\n");
    *pos += sprintf(result + *pos, "\n");

}

void format_from_section(Token* tokens, int start_idx, int end_idx, char* result, size_t* pos) {
    //Hieronder de code om de from section te handelen
    add_indentation(result, pos, 0);
    *pos += sprintf(result + *pos, "FROM\n");
    add_indentation(result, pos, 1);

    bool join_type_found = false;
    bool between_found = false;
    int join_level = 1;
    int depth = 0;
    int prev_idx = -1; //Vorige token die geen comment is

    for (int i = start_idx; i <= end_idx; i++) {
        if (tokens[i].type == TOKEN_COMMENT) {
            write_token(tokens, i, result, pos);
            continue;
        }
        const char* value = tokens[i].value;

        if (depth == 0) {
            // Een subquery als tabel schrijven we volledig uit.
            if (is_subquery_start(tokens, i, end_idx)
                && (prev_idx == -1 || strcasecmp(tokens[prev_idx].value, "JOIN") == 0
                    || strcasecmp(tokens[prev_idx].value, "LATERAL") == 0
                    || strcasecmp(tokens[prev_idx].value, "APPLY") == 0
                    || tokens[prev_idx].type == TOKEN_COMMA)) {
                i = format_subquery(tokens, i, end_idx, result, pos, join_level);
                prev_idx = i;
                continue;
            }
            prev_idx = i;

            if (strcasecmp(value, "CASE") == 0) {
                i += format_case_block(tokens, i, end_idx, result, pos, join_level + 1) - 1;
                prev_idx = i;
                continue;
            }
            // Handelen van left right inner and outer
            if (strcasecmp(value, "LEFT") == 0 ||
                strcasecmp(value, "RIGHT") == 0 ||
                strcasecmp(value, "INNER") == 0 ||
                strcasecmp(value, "OUTER") == 0 ||
                strcasecmp(value, "FULL") == 0 ||
                strcasecmp(value, "CROSS") == 0) {
                if (!join_type_found) { //Als we een FULL OUTER doen of dergelijke, willen we niet dat dit zorgt voor meer enters.
                    *pos += sprintf(result + *pos, "\n\n");
                    add_indentation(result, pos, join_level);
                }
                write_token(tokens, i, result, pos);
                join_type_found = true;
                continue;
            }
            // Handelen van join keyword
            if (strcasecmp(value, "JOIN") == 0) {
                if (!join_type_found) {
                    *pos += sprintf(result + *pos, "\n\n");
                    add_indentation(result, pos, join_level);
                }
                write_token(tokens, i, result, pos);
                join_type_found = false;
                continue;
            }
            // On keyword en AND/OR in de join conditie
            if (strcasecmp(value, "BETWEEN") == 0) {
                between_found = true;
            } else if (strcasecmp(value, "ON") == 0 ||
                       ((strcasecmp(value, "AND") == 0 && !between_found) || strcasecmp(value, "OR") == 0)) {
                *pos += sprintf(result + *pos, "\n");
                add_indentation(result, pos, join_level + 1);
                write_token(tokens, i, result, pos);
                continue;
            } else if (strcasecmp(value, "AND") == 0) {
                between_found = false; //Dit is de AND van de BETWEEN, die blijft op dezelfde regel.
            } else if (tokens[i].type == TOKEN_COMMA) {
                *pos += sprintf(result + *pos, "\n");
                add_indentation(result, pos, join_level);
                write_token(tokens, i, result, pos);
                continue;
            }
        }
        prev_idx = i;

        if (is_open_parenthesis(tokens, i)) {
            depth++;
        } else if (is_close_parenthesis(tokens, i)) {
            depth--;
        }
        write_token(tokens, i, result, pos);
    }
    *pos += sprintf(result + *pos, "\n\n");
}


void format_where_section(Token* tokens, int start_idx, int end_idx, char* result, size_t* pos) {
    add_indentation(result, pos, 0);
    *pos += sprintf(result + *pos, "WHERE\n");
    add_indentation(result, pos, 1);

    bool between_found = false;
    int depth = 0;

    for (int i = start_idx; i <= end_idx; i++) {
        const char* value = tokens[i].value;

        if (depth == 0) {
            // Subqueries in de where (EXISTS, IN) komen op hun eigen regel.
            if (is_subquery_start(tokens, i, end_idx)) {
                i = format_inline_subquery(tokens, i, end_idx, result, pos, 1);
                continue;
            }
            if (strcasecmp(value, "CASE") == 0) {
                i += format_case_block(tokens, i, end_idx, result, pos, 1) - 1;
                continue;
            }
            if (strcasecmp(value, "BETWEEN") == 0) {
                between_found = true;
            } else if (strcasecmp(value, "AND") == 0 && between_found) {
                between_found = false; //Dit is de AND van de BETWEEN, die blijft op dezelfde regel.
            } else if (strcasecmp(value, "AND") == 0 || strcasecmp(value, "OR") == 0) {
                *pos += sprintf(result + *pos, "\n");
                add_indentation(result, pos, 1);
                write_token(tokens, i, result, pos);
                continue;
            }
        }

        if (is_open_parenthesis(tokens, i)) {
            depth++;
        } else if (is_close_parenthesis(tokens, i)) {
            depth--;
        }
        write_token(tokens, i, result, pos);
    }
    *pos += sprintf(result + *pos, "\n\n");
}


void format_unknown_section(Token* tokens, int start_idx, int end_idx, char* result, size_t* pos) {
    /* Simpele catch all functie die alles wat we nog niet hebben behandeld doet.
    Die behandelen door het maar gewoon weg te schrijven.
    Wordt momenteel niet gebruikt.*/
    for (int i = start_idx; i <= end_idx; i++) {
        *pos += sprintf(result + *pos, "%s ", tokens[i].raw_value);
    }
    *pos += sprintf(result + *pos, "\n\n");
}

unsigned int format_comments(Token* tokens, int start_idx, int max_i, char* result, size_t* pos) {
    /* Speciale functie die comments schrijft. Return het aantal geskipte tokens*/
    int i = start_idx;
    unsigned int skipped_tokens = 0;
    while (tokens[i].type == TOKEN_COMMENT && i <= max_i) {
        if (tokens[i].line_number > tokens[i-1].line_number) {
            *pos += sprintf(result + *pos, "\n" );
        }
        *pos += sprintf(result + *pos, " %s", tokens[i].raw_value);
        i++;
        skipped_tokens++;
    }

    // *pos += sprintf(result + *pos, "\n");
    return skipped_tokens;
}

unsigned int format_case_block(Token* tokens, int start_idx, int max_i, char* result, size_t* pos, int indentation) {
    /* Speciale functie die case-when blocks schrijft. Begint op de CASE token.
    Return het aantal geschreven tokens (tot en met de END). */
    int i = start_idx;
    int depth = 0;

    write_token(tokens, i, result, pos); //De CASE zelf
    i++;

    while (i <= max_i) {
        const char* value = tokens[i].value;

        if (depth == 0 && strcasecmp(value, "CASE") == 0) {
            // Case in een case, die springt een niveau verder in.
            i += format_case_block(tokens, i, max_i, result, pos, indentation + 1);
            continue;
        }

        if (depth == 0 && (strcasecmp(value, "WHEN") == 0 || strcasecmp(value, "ELSE") == 0)) {
            // Voor when en else, een nieuwe lijn met tabjes
            *pos += sprintf(result + *pos, "\n");
            add_indentation(result, pos, indentation + 1);
            write_token(tokens, i, result, pos);
        } else if (depth == 0 && strcasecmp(value, "END") == 0) {
            // Klaar met case block, END op dezelfde hoogte als de WHEN.
            *pos += sprintf(result + *pos, "\n");
            add_indentation(result, pos, indentation + 1);
            write_token(tokens, i, result, pos);
            i++;
            break;
        } else {
            // De rest op 1 lijn.
            if (is_open_parenthesis(tokens, i)) {
                depth++;
            } else if (is_close_parenthesis(tokens, i)) {
                depth--;
            }
            write_token(tokens, i, result, pos);
        }

        i++;
    }

    return i - start_idx;
}



void format_sql(Token* token_array, int start_idx, int end_idx, char* result, size_t* pos) {
    /*Functie die een blok select statement formateert. Het blok begint altijd met de SELECT.*/
    
    // Maak onze clause array met specifieke functie voor elke clause. 
    SQLClause clauses[] = {
        {"SELECT", NULL, -1, -1, (FormatFunction)format_select_section, "SELECT"},
        {"INTO", NULL, -1, -1, (FormatFunction)format_select_section, "INTO"},
        {"FROM", NULL, -1, -1, format_from_section, NULL},
        {"WHERE", NULL, -1, -1, format_where_section, NULL},
        {"GROUP", "BY", -1, -1, (FormatFunction)format_select_section, "GROUP BY"},
        {"ORDER", "BY", -1, -1, (FormatFunction)format_select_section, "ORDER BY"},
        {"HAVING", NULL, -1, -1, (FormatFunction)format_select_section, "HAVING"}
    };

    const int num_clauses = sizeof(clauses) / sizeof(clauses[0]);

    // Nu gaan we kijken waar de clauses zijn.
    // In de eerste pass kijken we louter waar elk keyword begint.
    // Keywords tussen haakjes (subqueries, OVER (ORDER BY ...)) tellen niet mee.
    int depth = 0;
    for (int i = start_idx; i <= end_idx; i++) {
        if (is_open_parenthesis(token_array, i)) {
            depth++;
        } else if (is_close_parenthesis(token_array, i)) {
            depth--;
        }
        
        if (token_array[i].type == TOKEN_KEYWORD && depth == 0) { 
            for (int j = 0; j < num_clauses; j++) {
                if (strcasecmp(token_array[i].value, clauses[j].keyword) == 0) {
                    if (clauses[j].start_pos != -1) {
                        break; //Alleen de eerste keer telt, anders raken we het stuk ervoor kwijt.
                    }
                    if (clauses[j].second_word != NULL) {
                        if (i + 1 <= end_idx && 
                            strcasecmp(token_array[i + 1].value, clauses[j].second_word) == 0) {
                            clauses[j].start_pos = i + 2;
                            i++;
                        }
                    } else {
                        clauses[j].start_pos = i + 1;
                    }
                    break;
                }
            }
        }
    }

    // Tweede pas kijken waar alle clauses eindigen: net voor het keyword van de clause die er in de tekst op volgt.
    for (int i = 0; i < num_clauses; i++) {
        if (clauses[i].start_pos != -1) {
            int end_pos = end_idx;  // De default waarde is het einde van ons volledige statement.
            for (int j = 0; j < num_clauses; j++) {
                if (clauses[j].start_pos > clauses[i].start_pos) {
                    int keyword_pos = clauses[j].start_pos - (clauses[j].second_word != NULL ? 2 : 1);
                    if (keyword_pos - 1 < end_pos) {
                        end_pos = keyword_pos - 1;
                    }
                }
            }
            clauses[i].end_pos = end_pos;
            // printf("Clause %s: start=%d, end=%d\n", clauses[i].keyword, clauses[i].start_pos, clauses[i].end_pos);
        }
    }

    // Vervolgens formatteren we elke clause die we gevonden hebben, in de volgorde waarin ze in de tekst staan.
    int last_start = -1;
    for (int n = 0; n < num_clauses; n++) {
        int i = -1;
        for (int j = 0; j < num_clauses; j++) {
            if (clauses[j].start_pos > last_start && (i == -1 || clauses[j].start_pos < clauses[i].start_pos)) {
                i = j;
            }
        }
        if (i == -1) {
            break;
        }
        last_start = clauses[i].start_pos;

        if (clauses[i].format_keyword) {
            format_select_section(token_array, clauses[i].start_pos, 
                                clauses[i].end_pos, result, pos, 
                                clauses[i].format_keyword);
        } else {
            clauses[i].format_func(token_array, clauses[i].start_pos, 
                                 clauses[i].end_pos, result, pos);
        }
    }
}



char* apply_parenthesis_indentation(const char* input) {
    //Probleem met deze code: haakjes in de comments zorgen ook voor enter wat bij een SL comment een probleem is.
    size_t length = strlen(input);
    char* output = malloc(length * 8);  // Alloceer genoeg ruimte zodat onze spaces geen buffer overflow veroorzaken (worst-kaas scenario)
    if (!output) {
        return "Error: Memory allocation failed\n";
    }

    int indent_level = 0;
    const int INDENT_SIZE = 4;
    int line_indent = 0;
    int new_line = 1;
    size_t in_pos = 0, out_pos = 0;
    size_t last_pos = strlen(input) - 1;

    while (input[in_pos] != '\0') { 
         
        //Detecteer een nieuwe regel en meet de indent die we hebben
        if (new_line) {
            line_indent = 0;
            while (input[in_pos] == ' ') {  // Tel hoeveel bestaande spaties we hebben
                // printf("Current letter space: %c\n", input[in_pos]);
                output[out_pos++] = input[in_pos++];
                line_indent++;
            }
            
            new_line = 0;
        }
        

        char c = input[in_pos];

        // Als we een haakje openen zien dan verhogen we de indent met 1
        if (c == '(' && in_pos < last_pos && input[in_pos + 1] != ')') {
            output[out_pos++] = c;
            output[out_pos++] = '\n';
            indent_level++;  // Increase indent level after opening parenthesis
            for (int i = 0; i < line_indent + (indent_level * INDENT_SIZE); i++) {
                output[out_pos++] = ' ';
            }
            
            // Begin met tabben van alle regels tussen haakjes, en vergeet geen nested haakjes mee te nemen
            int nested_parens = 1; 
            while (nested_parens > 0 && input[in_pos] != '\0') {
                c = input[++in_pos]; 

                if (c == '(') {
                    nested_parens++;
                } else if (c == ')') {
                    nested_parens--; 
                    if (nested_parens == 0) {
                        break; 
                    }
                }

                // Handelen van nieuwe lines en de indentatie die er al is binnen de haakjes. 
                if (c == '\n') {
                    output[out_pos++] = c;  // Hou de nieuwlijn
                    // En voeg de identation dei we willen toe 
                    for (int i = 0; i < line_indent + (indent_level * INDENT_SIZE); i++) {
                        output[out_pos++] = ' ';
                    }
                } else {
                    output[out_pos++] = c;  // Of voeg gewoon het huidige karakter toe
                }
            }
            continue;  // Na het sluiten van de paranthesis kunnen we de rest skippen en de volgende karakter uitlezen
        }
        // ... en een als we een hakje sluiten zien dan verlagen we de indent met 1
        else if (c == ')' && in_pos > 1 && input[in_pos - 1] != '(') {
            indent_level--;
            output[out_pos++] = '\n';
            for (int i = 0; i < line_indent + (indent_level * INDENT_SIZE); i++) {
                output[out_pos++] = ' ';
            }
            output[out_pos++] = c;
        }
        // En hoe ga je om met de rest van de karakters?
        // Je print ze gewoon
        else {
            // printf("Current letter else: %c\n", c);
            output[out_pos++] = c;
            // printf("Current output: %s\n", output);
        }

        // Detecteer dat we een nieuwe regel hebben
        if (c == '\n') {
            new_line = 1;
        }

        in_pos++;
    }

    output[out_pos] = '\0';
    return output;
}


void format_statements(Token* token_array, int start_idx, int end_idx, char* result, size_t* pos) {
    /* Formatteert een reeks statements. Select blokken gaan naar format_sql, de rest schrijven we zoals het er staat. */

    // Keywords die ons moeten stoppen om een select statement in te lezen. 
    const char* stop_words[] = {"DROP", "CREATE", "ALTER", "SELECT", "UPDATE", 
                                "INSERT", "WITH", "USE", "DELETE", "UNION", "LIMIT",
                                "OFFSET", NULL};

    int current_pos = start_idx;
    int depth = 0;

    while (current_pos <= end_idx) {
        // Kijk of de huidige token een select is.
        if (strcasecmp(token_array[current_pos].value, "SELECT") == 0 && token_array[current_pos].type == TOKEN_KEYWORD) {
            int select_start = current_pos;
            int select_depth = 0;
            
            // Vind het einde van de huidige select statement.
            while (current_pos <= end_idx) {
                if (is_open_parenthesis(token_array, current_pos)) {
                    select_depth++;
                } else if (is_close_parenthesis(token_array, current_pos)) {
                    select_depth--;
                    if (select_depth < 0) {
                        break; //Een haakje dat we niet zelf geopend hebben, dat is niet van ons.
                    }
                }
                
                // Ook een check of we toevallig een puntkomma vinden. Lang leve afwijkinge van de SQL standard. 
                if (select_depth == 0 && token_array[current_pos].type == TOKEN_SEMICOLON) {
                    current_pos++;
                    break;
                }
                
                // Kijk of we een stop woord hebben gevonden, alleen buiten haakjes en nadat je de eerste select passeert
                int found_stop = 0;
                if (current_pos > select_start && select_depth == 0 && token_array[current_pos].type == TOKEN_KEYWORD) {
                    for (const char** stop = stop_words; *stop != NULL; stop++) {
                        if (strcasecmp(token_array[current_pos].value, *stop) == 0) {
                            found_stop = 1;
                            break;
                        }
                    }
                }
                if (found_stop) {
                    break;  // Niet de huidige positie incrementeren; we willen deze namelijk weer kunnen verwerken
                }
                
                current_pos++;
            }

            // Formateer ons gevonden select block, met een witregel ervoor.
            ensure_newlines(result, pos, 2);
            format_sql(token_array, select_start, current_pos - 1, result, pos);

        } else if (is_subquery_start(token_array, current_pos, end_idx)) {
            // Bijvoorbeeld de body van een CTE: WITH x AS ( SELECT ... )
            current_pos = format_subquery(token_array, current_pos, end_idx, result, pos, 0) + 1;

        } else {
            // Als we een non select keyword hebben schrijven we het gewoon weg. Stopwoorden beginnen op een nieuwe regel.
            int found_stop = 0;
            if (depth == 0 && token_array[current_pos].type == TOKEN_KEYWORD) {
                for (const char** stop = stop_words; *stop != NULL; stop++) {
                    if (strcasecmp(token_array[current_pos].value, *stop) == 0) {
                        found_stop = 1;
                        break;
                    }
                }
            }
            if (found_stop) {
                ensure_newlines(result, pos, 1);
            }
            if (*pos > 0 && result[*pos - 1] == '\n') {
                add_indentation(result, pos, 0);
            }

            if (is_open_parenthesis(token_array, current_pos)) {
                depth++;
            } else if (is_close_parenthesis(token_array, current_pos)) {
                depth--;
            }
            write_token(token_array, current_pos, result, pos);
            current_pos++;
        }
    }
}


const char* preprocess_format_postprocess(Token** tokens, unsigned int token_count) {
    if (!tokens || !*tokens) {
        return "Error: Invalid tokens array\n";
    }
    if (token_count == 0) {
        return ""; //Leeg bestand in, leeg bestand uit.
    }

    Token* token_array = *tokens;

    // Bereken hoeveel ruimte we maximaal nodig hebben. Per token komen er hooguit een paar enters, 
    // een keyword en inspringing bij, en die inspringing hangt af van hoe diep de haakjes en cases genest zijn.
    size_t buffer_size = 1;
    int depth = 0;
    int max_depth = 0;
    for (unsigned int i = 0; i < token_count; i++) {
        buffer_size += token_array[i].raw_length;
        if (is_open_parenthesis(token_array, i) || strcasecmp(token_array[i].value, "CASE") == 0) {
            depth++;
        } else if ((is_close_parenthesis(token_array, i) || strcasecmp(token_array[i].value, "END") == 0) && depth > 0) {
            depth--;
        }
        if (depth > max_depth) {
            max_depth = depth;
        }
    }
    buffer_size += (size_t)token_count * (16 + 8 * (max_depth + 4));

    char* final_result = malloc(buffer_size * sizeof(char));
    if (!final_result) {
        return "Error: Memory allocation failed\n";
    }

    size_t final_pos = 0;
    base_indentation = 0;
    line_comment_open = 0;
    format_statements(token_array, 0, token_count - 1, final_result, &final_pos);

    // Netjes afsluiten met precies 1 enter.
    while (final_pos > 0 && (final_result[final_pos - 1] == '\n' || final_result[final_pos - 1] == ' ')) {
        final_pos--;
    }
    final_result[final_pos++] = '\n';
    final_result[final_pos] = '\0';
    return final_result;
}


