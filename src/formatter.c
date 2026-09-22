#include <tokenizer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <formatter.h>
#include <ctype.h>

typedef struct {
    const char* keyword;
    const char* second_word;
    int start_pos;
    int end_pos;
    const char* format_keyword;
    bool split_conditions; //Komen AND/OR/ON op een nieuwe regel?
    bool has_joins;        //Kunnen er joins en subqueries als tabel in staan?
} SQLClause;

// Keywords die een select statement beeindigen.
static const char* stop_words[] = {"DROP", "CREATE", "ALTER", "SELECT", "UPDATE", "INSERT", "WITH",
                                   "USE", "DELETE", "UNION", "LIMIT", "OFFSET", NULL};
static const char* join_words[] = {"LEFT", "RIGHT", "INNER", "OUTER", "FULL", "CROSS", "NATURAL", "JOIN", NULL};
// Na deze woorden is een subquery een tabel.
static const char* table_words[] = {"JOIN", "LATERAL", "APPLY", ",", NULL};
static const char* condition_words[] = {"AND", "OR", "ON", NULL};
static const char* case_words[] = {"WHEN", "ELSE", "END", NULL};

// Hoeveel niveaus we extra inspringen omdat we in een subquery zitten.
static int base_indentation = 0;

static void format_statements(Token* tokens, int start_idx, int end_idx, char* result, size_t* pos);

static bool is_one_of(const char* value, const char** words) {
    for (; *words != NULL; words++) {
        if (strcasecmp(value, *words) == 0) {
            return true;
        }
    }
    return false;
}

static int parenthesis_change(Token* tokens, int i) {
    // +1 voor een haakje openen, -1 voor een haakje sluiten, anders 0.
    if (tokens[i].type != TOKEN_PARENTHESIS) {
        return 0;
    }
    return tokens[i].value[0] == '(' ? 1 : -1;
}

//Om tabjes te inserten van 4 maal spatie
static void add_indentation(char* result, size_t* pos, int level) {
    for (int i = 0; i < (base_indentation + level) * 4; i++) {
        result[(*pos)++] = ' ';
    }
}

static bool is_at_line_start(const char* result, size_t pos) {
    // Staan er op de huidige regel alleen nog maar spaties?
    while (pos > 0 && result[pos - 1] == ' ') {
        pos--;
    }
    return pos == 0 || result[pos - 1] == '\n';
}

static void ensure_newlines(char* result, size_t* pos, int amount) {
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
        result[(*pos)++] = '\n';
    }
}

static void new_line(char* result, size_t* pos, int amount, int level) {
    // Begin een nieuwe regel op het gegeven niveau. Met amount = 2 komt er een witregel voor.
    ensure_newlines(result, pos, amount);
    add_indentation(result, pos, level);
}

static void continue_on_new_line(char* result, size_t* pos) {
    // Nieuwe regel met dezelfde inspringing als de huidige regel, tenzij we al aan het begin van een regel staan.
    if (is_at_line_start(result, *pos)) {
        return;
    }
    size_t line_start = *pos;
    while (line_start > 0 && result[line_start - 1] != '\n') {
        line_start--;
    }
    result[(*pos)++] = '\n';
    for (size_t i = line_start; result[i] == ' '; i++) {
        result[(*pos)++] = ' ';
    }
}

static void write_token(Token* tokens, int i, char* result, size_t* pos) {
    /* Schrijft een enkele token weg. ALLE tokens gaan via deze functie de output in, zo raken we nooit tekst kwijt.
    Een spatie ervoor zetten we alleen als die er in het origineel ook stond.*/
    bool is_comment = tokens[i].type == TOKEN_COMMENT;
    if (is_comment && i > 0 && tokens[i].line_number > tokens[i - 1].line_number) {
        continue_on_new_line(result, pos); //De comment stond op een eigen regel, dat houden we zo.
    }

    char previous = *pos > 0 ? result[*pos - 1] : '\n';
    if (tokens[i].space_before && previous != ' ' && previous != '\n'
        && !(previous == ',' && is_at_line_start(result, *pos - 1))) { //Geen spatie na een comma aan het begin van de regel
        result[(*pos)++] = ' ';
    }

    size_t start = *pos;
    *pos += sprintf(result + *pos, "%s", tokens[i].value);
    if (tokens[i].type == TOKEN_KEYWORD || tokens[i].type == TOKEN_OPERATOR) {
        for (size_t c = start; c < *pos; c++) {
            result[c] = toupper((unsigned char)result[c]);
        }
    }

    if (is_comment && tokens[i].value[0] == '-') {
        continue_on_new_line(result, pos); //Een -- comment loopt tot het einde van de regel, anders commenten we code weg.
    }
}

static int find_closing_parenthesis(Token* tokens, int start_idx, int end_idx) {
    // Geeft de index van het haakje dat het haakje op start_idx sluit, of -1 als die er niet is.
    int depth = 0;
    for (int i = start_idx; i <= end_idx; i++) {
        depth += parenthesis_change(tokens, i);
        if (depth == 0) {
            return i;
        }
    }
    return -1;
}

static bool is_subquery_start(Token* tokens, int i, int end_idx) {
    // Is dit een haakje openen met direct (eventueel na comments) een SELECT of WITH erachter?
    if (parenthesis_change(tokens, i) != 1) {
        return false;
    }
    do {
        i++;
    } while (i <= end_idx && tokens[i].type == TOKEN_COMMENT);
    return i <= end_idx && (strcasecmp(tokens[i].value, "SELECT") == 0 || strcasecmp(tokens[i].value, "WITH") == 0);
}

static int format_subquery(Token* tokens, int start_idx, int end_idx, char* result, size_t* pos, int level, bool expand) {
    /* Schrijft een subquery tussen haakjes. Met expand wordt het een volledig geformatteerd blok,
    anders komt de inhoud zoals die er staat op een eigen regel. level is de inspringing van de regel met het haakje.
    Return de index van de laatst geschreven token.*/
    int close_idx = find_closing_parenthesis(tokens, start_idx, end_idx);
    int inner_end = (close_idx == -1) ? end_idx : close_idx - 1;

    write_token(tokens, start_idx, result, pos);
    if (expand) {
        base_indentation += level + 1;
        format_statements(tokens, start_idx + 1, inner_end, result, pos);
        base_indentation -= level + 1;
    } else {
        new_line(result, pos, 1, level + 1);
        for (int i = start_idx + 1; i <= inner_end; i++) {
            write_token(tokens, i, result, pos);
        }
    }

    if (close_idx == -1) {
        return end_idx;
    }
    new_line(result, pos, expand ? 2 : 1, level);
    write_token(tokens, close_idx, result, pos);
    return close_idx;
}

static int format_case_block(Token* tokens, int start_idx, int end_idx, char* result, size_t* pos, int indentation) {
    /* Schrijft een CASE blok, met WHEN, ELSE en END elk op een eigen regel.
    Return de index van de laatst geschreven token (de END). */
    int depth = 0;
    write_token(tokens, start_idx, result, pos); //De CASE zelf

    for (int i = start_idx + 1; i <= end_idx; i++) {
        if (depth == 0 && strcasecmp(tokens[i].value, "CASE") == 0) {
            i = format_case_block(tokens, i, end_idx, result, pos, indentation + 1); //Case in een case springt verder in.
            continue;
        }
        if (depth == 0 && is_one_of(tokens[i].value, case_words)) {
            new_line(result, pos, 1, indentation + 1);
        }
        depth += parenthesis_change(tokens, i);
        write_token(tokens, i, result, pos);
        if (depth == 0 && strcasecmp(tokens[i].value, "END") == 0) {
            return i;
        }
    }
    return end_idx;
}

static void format_section(Token* tokens, const SQLClause* clause, char* result, size_t* pos) {
    /* Formatteert 1 clause: het keyword op een eigen regel en de inhoud een niveau dieper.
    Een comma buiten haakjes begint een nieuw item. Afhankelijk van de clause krijgen ook AND/OR/ON en joins een eigen regel.*/
    int condition_level = clause->has_joins ? 2 : 1;
    int depth = 0;
    int prev_idx = -1; //Vorige token die geen comment is
    bool between_found = false;

    add_indentation(result, pos, 0);
    *pos += sprintf(result + *pos, "%s", clause->format_keyword);
    new_line(result, pos, 1, 1);

    for (int i = clause->start_pos; i <= clause->end_pos; i++) {
        const char* value = tokens[i].value;

        if (depth == 0 && tokens[i].type != TOKEN_COMMENT) {
            bool is_table = clause->has_joins && (prev_idx == -1 || is_one_of(tokens[prev_idx].value, table_words));

            if (is_subquery_start(tokens, i, clause->end_pos) && (is_table || clause->split_conditions)) {
                // Een subquery als tabel schrijven we volledig uit, een subquery in een conditie op een eigen regel.
                i = format_subquery(tokens, i, clause->end_pos, result, pos, is_table ? 1 : condition_level, is_table);
                prev_idx = i;
                continue;
            } else if (tokens[i].type == TOKEN_COMMA) {
                // Comments die achter de comma stonden, horen nog bij het vorige item.
                int comma_idx = i;
                while (i + 1 <= clause->end_pos && tokens[i + 1].type == TOKEN_COMMENT) {
                    write_token(tokens, ++i, result, pos);
                }
                new_line(result, pos, 1, 1);
                write_token(tokens, comma_idx, result, pos);
                prev_idx = comma_idx;
                continue;
            } else if (strcasecmp(value, "CASE") == 0) {
                i = format_case_block(tokens, i, clause->end_pos, result, pos, condition_level);
                prev_idx = i;
                continue;
            } else if (clause->has_joins && is_one_of(value, join_words)) {
                // LEFT OUTER JOIN en dergelijke blijven samen op 1 regel, met een witregel ervoor.
                if (prev_idx == -1 || !is_one_of(tokens[prev_idx].value, join_words)) {
                    new_line(result, pos, 2, 1);
                }
            } else if (clause->split_conditions && strcasecmp(value, "BETWEEN") == 0) {
                between_found = true;
            } else if (clause->split_conditions && is_one_of(value, condition_words)) {
                if (between_found && strcasecmp(value, "AND") == 0) {
                    between_found = false; //De AND van een BETWEEN blijft op dezelfde regel.
                } else {
                    new_line(result, pos, 1, condition_level);
                }
            }
        }

        depth += parenthesis_change(tokens, i);
        write_token(tokens, i, result, pos);
        if (tokens[i].type != TOKEN_COMMENT) {
            prev_idx = i;
        }
    }
    ensure_newlines(result, pos, 2);
}

static void format_sql(Token* tokens, int start_idx, int end_idx, char* result, size_t* pos) {
    /*Functie die een blok select statement formateert. Het blok begint altijd met de SELECT.*/
    SQLClause clauses[] = {
        // keyword, tweede woord, start, einde, zo schrijven we het, AND/OR/ON op nieuwe regel, joins
        {"SELECT", NULL, -1, -1, "SELECT", false, false},
        {"INTO", NULL, -1, -1, "INTO", false, false},
        {"FROM", NULL, -1, -1, "FROM", true, true},
        {"WHERE", NULL, -1, -1, "WHERE", true, false},
        {"GROUP", "BY", -1, -1, "GROUP BY", false, false},
        {"ORDER", "BY", -1, -1, "ORDER BY", false, false},
        {"HAVING", NULL, -1, -1, "HAVING", true, false}
    };
    const int num_clauses = sizeof(clauses) / sizeof(clauses[0]);
    SQLClause* found[sizeof(clauses) / sizeof(clauses[0])];
    int found_count = 0;

    // Zoek de clauses in de volgorde waarin ze in de tekst staan. Elke clause loopt tot het keyword van de volgende.
    // Keywords tussen haakjes (subqueries, OVER (ORDER BY ...)) tellen niet mee, en elke clause telt maar 1 keer.
    int depth = 0;
    for (int i = start_idx; i <= end_idx; i++) {
        depth += parenthesis_change(tokens, i);
        if (depth != 0) {
            continue;
        }
        for (int j = 0; j < num_clauses; j++) {
            SQLClause* clause = &clauses[j];
            if (clause->start_pos != -1 || strcasecmp(tokens[i].value, clause->keyword) != 0) {
                continue;
            }
            int words = 1;
            if (clause->second_word != NULL) {
                if (i + 1 > end_idx || strcasecmp(tokens[i + 1].value, clause->second_word) != 0) {
                    break;
                }
                words = 2;
            }
            if (found_count > 0) {
                found[found_count - 1]->end_pos = i - 1;
            }
            clause->start_pos = i + words;
            found[found_count++] = clause;
            i += words - 1;
            break;
        }
    }
    if (found_count > 0) {
        found[found_count - 1]->end_pos = end_idx;
    }

    for (int i = 0; i < found_count; i++) {
        format_section(tokens, found[i], result, pos);
    }
}

static void format_statements(Token* tokens, int start_idx, int end_idx, char* result, size_t* pos) {
    /* Formatteert een reeks statements. Select blokken gaan naar format_sql, de rest schrijven we zoals het er staat. */
    int i = start_idx;
    while (i <= end_idx) {
        if (strcasecmp(tokens[i].value, "SELECT") == 0) {
            // Het select blok loopt tot een stopwoord of puntkomma buiten haakjes, of tot een haakje dat niet van ons is.
            int select_start = i;
            int depth = 0;
            for (i++; i <= end_idx; i++) {
                depth += parenthesis_change(tokens, i);
                if (depth < 0 || (depth == 0 && is_one_of(tokens[i].value, stop_words))) {
                    break;
                }
                if (depth == 0 && tokens[i].type == TOKEN_SEMICOLON) {
                    i++;
                    break;
                }
            }
            ensure_newlines(result, pos, 2);
            format_sql(tokens, select_start, i - 1, result, pos);
        } else if (is_subquery_start(tokens, i, end_idx)) {
            // Bijvoorbeeld de body van een CTE: WITH x AS ( SELECT ... )
            i = format_subquery(tokens, i, end_idx, result, pos, 0, true) + 1;
        } else {
            // De rest schrijven we gewoon weg, alleen stopwoorden beginnen op een nieuwe regel.
            if (is_one_of(tokens[i].value, stop_words)) {
                new_line(result, pos, 1, 0);
            } else if (*pos > 0 && result[*pos - 1] == '\n') {
                add_indentation(result, pos, 0);
            }
            write_token(tokens, i, result, pos);
            i++;
        }
    }
}


char* preprocess_format_postprocess(Token* tokens, int token_count) {
    /* Formatteert alle tokens. Return NULL als het geheugen op is. */

    // Bereken hoeveel ruimte we maximaal nodig hebben. Per token komen er hooguit een paar enters,
    // een keyword en inspringing bij, en die inspringing hangt af van hoe diep de haakjes en cases genest zijn.
    size_t buffer_size = 2;
    int depth = 0;
    int max_depth = 0;
    for (int i = 0; i < token_count; i++) {
        buffer_size += tokens[i].length;
        if (strcasecmp(tokens[i].value, "CASE") == 0) {
            depth++;
        } else if (strcasecmp(tokens[i].value, "END") == 0) {
            depth--;
        } else {
            depth += parenthesis_change(tokens, i);
        }
        if (depth < 0) {
            depth = 0;
        }
        if (depth > max_depth) {
            max_depth = depth;
        }
    }
    buffer_size += (size_t)token_count * (64 + 16 * max_depth);

    char* final_result = malloc(buffer_size * sizeof(char));
    if (!final_result) {
        return NULL;
    }

    size_t final_pos = 0;
    base_indentation = 0;
    format_statements(tokens, 0, token_count - 1, final_result, &final_pos);

    // Netjes afsluiten met precies 1 enter.
    while (final_pos > 0 && (final_result[final_pos - 1] == '\n' || final_result[final_pos - 1] == ' ')) {
        final_pos--;
    }
    if (final_pos > 0) {
        final_result[final_pos++] = '\n';
    }
    final_result[final_pos] = '\0';
    return final_result;
}
