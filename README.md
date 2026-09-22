# Better SQL Formatter

## Description
This is a CLI program that takes a .sql or .txt file and outputs a new file with the formatted version of the SQL code. It is my first C project that I undertook with the purpose of getting a better understanding of C while making something semi-useful. I quickly realized that making a SQL formatter is quite complex, and so it became something that I just "wanted to finish".

I decided to open-source it anyway in case anyone finds my specific style of SQL code formatting agreeable; and I have not found a single formatter that emulates my style perfectly.

It does not have any customization features (i.e. enabling the control of how many spaces you use and such, though you can just change the code). It is simply unformatted file goes, formatted file comes out. It also has the -v option to print out the tokens it reads in.

In short, the style is:
- Every clause keyword (`SELECT`, `FROM`, `WHERE`, `GROUP BY`, `ORDER BY`, `HAVING`) on its own line, with its contents indented 4 spaces below it and a blank line between clauses.
- Leading commas: one item per line, with the comma at the start of the line.
- `AND` / `OR` on their own line in `WHERE`, `HAVING` and join conditions. The `AND` of a `BETWEEN` stays on the same line.
- Joins on their own line with a blank line before them, and `ON` indented one level deeper.
- `CASE` blocks with `WHEN` / `ELSE` / `END` on their own lines.
- Subqueries used as a table (in `FROM`, `JOIN` or a CTE) are formatted as a full indented block. Subqueries in `WHERE` / `HAVING` get their own line. Everything else (function calls, subqueries in the select list) stays inline.
- Keywords and operators are uppercased.

What it will not do is change your code: every word, string and comment from the input ends up in the output in the same order. String literals, quoted identifiers and comments are copied exactly, including the spacing inside them. The only things that change are the whitespace between tokens and the case of keywords. Comments that sat directly after a comma are placed before it (`a, -- note` becomes `a -- note` followed by `,b` on the next line), and a `--` comment is always followed by a line break so it can never comment out code. The exceptions I know of are listed under Known Issues.

## Installation
```bash
# Clone the repository
git clone https://github.com/ML1883/better-sql-formatter.git

# Navigate to the directory
cd better-sql-formatter

# Compile using the provided Makefile
make
```
It builds with `-Wall -Wextra` without any warnings (tested with GCC 16.2.1 on Linux). The result is an executable called `main`.


## Usage
```bash
./main [-v] file.sql
```
or
```bash
./main [-v] file.txt
```
The output is written to `formatted_<file name>` in the current directory, e.g. `formatted_file.sql`. An existing file with that name is overwritten.

Note: pass the file name without a folder, i.e. run the program from the directory the file is in (`../better-sql-formatter/main file.sql` works, `./main queries/file.sql` does not). Otherwise it tries to write to `formatted_queries/file.sql` and stops with `Error writing output: Failed to open file`.

The -v flag prints the tokens it read in, for example:
```
Token 0: value='WITH', type=0, line=0
Token 1: value='recursive_dates', type=1, line=0
Token 2: value='AS', type=0, line=0
Token 3: value='(', type=4, line=0
Token 4: value='SELECT', type=0, line=1
```
The types are listed in `include/tokenizer.h`.

The program returns 0 on success and 1 on an error (wrong arguments, a file that isn't .sql or .txt, a file that can't be read or written, or running out of memory). On an error no output file is written.

## Example
This input:
```sql
WITH recursive_dates AS (
    SELECT DATE '2024-01-01' AS dt
    UNION ALL
    SELECT dt + INTERVAL '1 day' FROM recursive_dates WHERE dt < DATE '2024-01-10'
),
aggregated_sales AS (
    SELECT 
        c.customer_id,
        c.name AS customer_name,
        SUM(o.total_amount, 0) AS total_spent,
        COUNT(o.order_id) AS order_count
    FROM customers c
    LEFT JOIN orders o ON c.customer_id = o.customer_id
    WHERE o.order_date BETWEEN '2024-01-01' AND '2024-12-31'
    GROUP BY c.customer_id, c.name
    HAVING SUM(o.total_amount) > 500
)
SELECT 
    c.customer_id,
    c.name AS customer_name,
    COALESCE(a.total_spent, 0) AS total_spent,
    COALESCE(a.order_count, 0) AS order_count,
    CASE 
        WHEN a.total_spent > 1000 THEN 'VIP'
        ELSE 'Regular'
    END AS customer_status,
    (SELECT wat FROM orders WHERE customer_id = c.customer_id) AS total_orders

FROM (SELECT wat FROM orders WHERE customer_id = c.customer_id) c
LEFT JOIN aggregated_sales a ON c.customer_id = a.customer_id
WHERE EXISTS (
    SELECT 1 FROM orders o WHERE o.customer_id = c.customer_id AND o.total_amount > 100
)
ORDER BY total_spent DESC, customer_name ASC
LIMIT 50
OFFSET 10;
```

Is turned into this (actual output of the current version):
```sql
WITH recursive_dates AS (

    SELECT
        DATE '2024-01-01' AS dt

    UNION ALL

    SELECT
        dt + INTERVAL '1 day'

    FROM
        recursive_dates

    WHERE
        dt < DATE '2024-01-10'

), aggregated_sales AS (

    SELECT
        c.customer_id
        ,c.name AS customer_name
        ,SUM(o.total_amount, 0) AS total_spent
        ,COUNT(o.order_id) AS order_count

    FROM
        customers c

        LEFT JOIN orders o
            ON c.customer_id = o.customer_id

    WHERE
        o.order_date BETWEEN '2024-01-01' AND '2024-12-31'

    GROUP BY
        c.customer_id
        ,c.name

    HAVING
        SUM(o.total_amount) > 500

)

SELECT
    c.customer_id
    ,c.name AS customer_name
    ,COALESCE(a.total_spent, 0) AS total_spent
    ,COALESCE(a.order_count, 0) AS order_count
    ,CASE
        WHEN a.total_spent > 1000 THEN 'VIP'
        ELSE 'Regular'
        END AS customer_status
    ,(SELECT wat FROM orders WHERE customer_id = c.customer_id) AS total_orders

FROM
    (

        SELECT
            wat

        FROM
            orders

        WHERE
            customer_id = c.customer_id

    ) c

    LEFT JOIN aggregated_sales a
        ON c.customer_id = a.customer_id

WHERE
    EXISTS (
        SELECT 1 FROM orders o WHERE o.customer_id = c.customer_id AND o.total_amount > 100
    )

ORDER BY
    total_spent DESC
    ,customer_name ASC

LIMIT 50
OFFSET 10;
```

A smaller one with comments and strings:
```sql
select id, -- the key
  name, 'a,  b  (c)' as label
from users u left outer join orders o on u.id = o.user_id
where u.active = 1 and o.total between 10 and 20
```
becomes
```sql
SELECT
    id -- the key
    ,name
    ,'a,  b  (c)' AS label

FROM
    users u

    LEFT OUTER JOIN orders o
        ON u.id = o.user_id

WHERE
    u.active = 1
    AND o.total BETWEEN 10 AND 20
```

## Known Issues
- MySQL-style backslash escapes in strings are not supported, only the standard `''`. In `'it\'s   here'` the string is thought to end at `\'`, so the spaces inside the rest of it get collapsed: the output is `'it\'s here'`. This is the only way I know of in which the contents of your code can still change.
- PostgreSQL dollar-quoted strings (`$$ ... $$`, e.g. function bodies) are not recognised as strings and get formatted as if they were SQL.
- MySQL `#` comments and nested `/* /* */ */` comments are not recognised.
- Not every keyword is known, so the casing can end up mixed. For example, this input:
  ```sql
  select distinct a from t where a is not null and b like '%x%' union all select b from u;
  ```
  becomes:
  ```sql
  SELECT
      DISTINCT a

  FROM
      t

  WHERE
      a is NOT null
      AND b like '%x%'

  UNION all

  SELECT
      b

  FROM
      u;
  ```
  It also shows that `DISTINCT` ends up on the first item instead of next to `SELECT`.
- Clauses it doesn't know, like `QUALIFY`, `WINDOW` and `FETCH`, are not put on their own line.
- `INSERT`, `UPDATE`, `DELETE` and `CREATE` statements are mostly kept on one line. Only subqueries inside them get formatted.
- Windows line endings (CRLF) come out as LF, except inside strings and block comments.
- It is only tested on Linux. On Windows the input file is opened in text mode, which will probably make it fail on files with CRLF line endings.
- `make test` and `make run` in the Makefile don't work.

## Feedback and such
Feel free to comment and suggest improvements. Do know that maintenance is at my leisure.

## Disclaimer
No garantuees or warranties of any kind are provided.

## License
This project is licensed under AGPL-3.0. The copyright holder reserves the right to make exceptions to the AGPL-3.0 license at its discretion.