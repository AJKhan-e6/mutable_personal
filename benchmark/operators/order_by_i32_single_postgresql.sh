#!/bin/bash

# Define path to PostgreSQL CLI
POSTGRESQL=psql

{ ${POSTGRESQL} -U postgres | ack 'Time' | cut -d ' ' -f 2; } << EOF
DROP DATABASE IF EXISTS benchmark_tmp;
CREATE DATABASE benchmark_tmp;
\c benchmark_tmp
CREATE TABLE Attributes_i32 ( a0 INT, a1 INT, a2 INT, a3 INT, a4 INT, a5 INT, a6 INT, a7 INT, a8 INT, a9 INT);
\copy Attributes_i32 FROM 'benchmark/operators/data/Attributes_i32.csv' WITH DELIMITER ',' CSV HEADER;
\timing on
SELECT a0 FROM Attributes_i32 WHERE a0 < -2147483647 ORDER BY a0;
SELECT a0 FROM Attributes_i32 WHERE a0 < -1717986918 ORDER BY a0;
SELECT a0 FROM Attributes_i32 WHERE a0 < -1288490188 ORDER BY a0;
SELECT a0 FROM Attributes_i32 WHERE a0 < -858993459 ORDER BY a0;
SELECT a0 FROM Attributes_i32 WHERE a0 < -429496729 ORDER BY a0;
SELECT a0 FROM Attributes_i32 WHERE a0 < 0 ORDER BY a0;
SELECT a0 FROM Attributes_i32 WHERE a0 < 429496729 ORDER BY a0;
SELECT a0 FROM Attributes_i32 WHERE a0 < 858993459 ORDER BY a0;
SELECT a0 FROM Attributes_i32 WHERE a0 < 1288490188 ORDER BY a0;
SELECT a0 FROM Attributes_i32 WHERE a0 < 1717986918 ORDER BY a0;
SELECT a0 FROM Attributes_i32 WHERE a0 < 2147483647 ORDER BY a0;
EOF
