CREATE TABLE ambiguous_a (
    n INT(4)
);

CREATE TABLE ambiguous_b (
    n INT(4)
);

SELECT
    a.n, b.n
FROM
    ambiguous_a AS a,
    ambiguous_b AS b;
