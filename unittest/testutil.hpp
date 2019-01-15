#pragma once

#include "lex/Lexer.hpp"
#include "util/Diagnostic.hpp"
#include "catalog/Schema.hpp"
#include "util/StringPool.hpp"
#include <sstream>
#include <string>

#define LEXER(STR) \
    Catalog &C = Catalog::Get(); \
    std::ostringstream out, err; \
    Diagnostic diag(false, out, err); \
    std::istringstream in((STR)); \
    Lexer lexer(diag, C.get_pool(), "-", in)
