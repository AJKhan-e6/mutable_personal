#pragma once


#include "util/macro.hpp"
#include <iostream>
#include <sstream>


namespace db {

struct Position
{
    const char *name;
    unsigned line;
    unsigned column;

    explicit Position(const char *name)
        : name(name)
        , line(0)
        , column(0)
    { }

    explicit Position(const char *name, const size_t line, const size_t column)
        : name(name)
        , line(line)
        , column(column)
    { }

    friend std::string to_string(const Position &pos) {
        std::ostringstream os;
        os << pos;
        return os.str();
    }

    friend std::ostream & operator<<(std::ostream &os, const Position &pos) {
        return os << pos.name << ":" << pos.line << ":" << pos.column;
    }

    DECLARE_DUMP
};

}
