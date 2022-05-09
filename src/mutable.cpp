#include <mutable/mutable.hpp>

#include "backend/Interpreter.hpp"
#include "backend/StackMachine.hpp"
#include "backend/WebAssembly.hpp"
#include "io/Reader.hpp"
#include "lex/Lexer.hpp"
#include <mutable/IR/Tuple.hpp>
#include <mutable/util/Diagnostic.hpp>
#include "parse/Parser.hpp"
#include "parse/Sema.hpp"
#include "parse/Sema.hpp"
#include <cerrno>
#include <fstream>


using namespace m;


bool init() { return streq(m::version::GIT_REV, m::version::get().GIT_REV); }

std::unique_ptr<Stmt> m::statement_from_string(Diagnostic &diag, const std::string &str)
{
    Catalog &C = Catalog::Get();

    std::istringstream in(str);
    Lexer lexer(diag, C.get_pool(), "-", in);
    Parser parser(lexer);
    auto stmt = M_TIME_EXPR(std::unique_ptr<Stmt>(parser.parse()), "Parse the statement", C.timer());
    if (diag.num_errors() != 0)
        throw frontend_exception("syntactic error in statement");
    M_insist(diag.num_errors() == 0);

    Sema sema(diag);
    M_TIME_EXPR(sema(*stmt), "Semantic analysis", C.timer());
    if (diag.num_errors() != 0)
        throw frontend_exception("semantic error in statement");
    M_insist(diag.num_errors() == 0);

    return stmt;
}

void m::execute_statement(Diagnostic &diag, const Stmt &stmt)
{
    diag.clear();
    Catalog &C = Catalog::Get();

    if (is<const SelectStmt>(stmt)) {
        auto query_graph = M_TIME_EXPR(QueryGraph::Build(stmt), "Construct the query graph", C.timer());

        Optimizer Opt(C.plan_enumerator(), C.cost_function());
        auto optree = M_TIME_EXPR(Opt(*query_graph), "Compute the query plan", C.timer());

        PrintOperator print(std::cout);
        print.add_child(optree.release());

        auto &backend = C.default_backend();
        M_TIME_EXPR(backend.execute(print), "Execute the query", C.timer());
    } else if (auto I = cast<const InsertStmt>(&stmt)) {
        auto &DB = C.get_database_in_use();
        auto &T = DB.get_table(I->table_name.text);
        auto &store = T.store();
        StoreWriter W(store);
        auto &S = W.schema();
        Tuple tup(S);

        /* Write all tuples to the store. */
        M_TIME_THIS("Execute the query", C.timer());
        for (auto &t : I->tuples) {
            StackMachine get_tuple(Schema{});
            for (std::size_t i = 0; i != t.size(); ++i) {
                auto &v = t[i];
                switch (v.first) {
                    case InsertStmt::I_Null:
                        get_tuple.emit_St_Tup_Null(0, i);
                        break;

                    case InsertStmt::I_Default:
                        /* nothing to be done, Tuples are initialized to default values */
                        break;

                    case InsertStmt::I_Expr:
                        get_tuple.emit(*v.second);
                        get_tuple.emit_Cast(S[i].type, v.second->type());
                        get_tuple.emit_St_Tup(0, i, S[i].type);
                        break;
                }
            }
            Tuple *args[] = { &tup };
            get_tuple(args);
            W.append(tup);
        }
    } else if (auto S = cast<const CreateTableStmt>(&stmt)) {
        auto &DB = C.get_database_in_use();
        auto &T = DB.get_table(S->table_name.text);
        T.store(C.create_store(T));
    } else if (auto S = cast<const DSVImportStmt>(&stmt)) {
        auto &DB = C.get_database_in_use();
        auto &T = DB.get_table(S->table_name.text);

        struct {
            char delimiter = ',';
            char escape = '\\';
            char quote = '\"';
            bool has_header = false;
            bool skip_header = false;
            std::size_t num_rows = std::numeric_limits<decltype(num_rows)>::max();
        } reader_config;
        if (S->rows) reader_config.num_rows = strtol(S->rows.text, nullptr, 10);
        if (S->delimiter) reader_config.delimiter = unescape(S->delimiter.text)[1];
        if (S->escape) reader_config.escape = unescape(S->escape.text)[1];
        if (S->quote) reader_config.quote = unescape(S->quote.text)[1];
        reader_config.has_header = S->has_header;
        reader_config.skip_header = S->skip_header;

        try {
            DSVReader R(T, diag, reader_config.delimiter, reader_config.escape, reader_config.quote,
                        reader_config.has_header, reader_config.skip_header, reader_config.num_rows);

            const auto filename = unquote(S->path.text);
            errno = 0;
            std::ifstream file(filename);
            if (not file) {
                const auto errsv = errno;
                diag.e(S->path.pos) << "Could not open file '" << filename << '\'';
                if (errsv)
                    diag.err() << ": " << strerror(errsv);
                diag.err() << std::endl;
            } else {
                M_TIME_THIS("Read DSV file", C.timer());
                R(file, filename.c_str());
            }

            if (diag.num_errors() != 0)
                throw runtime_error("error while reading DSV file");
        } catch (m::invalid_argument e) {
            diag.e(Position("DSVReader")) << "Error reading DSV file.\n"
                                          << e.what() << "\n";
        }
    }

    std::cout.flush();
    std::cerr.flush();
}

void m::execute_query(Diagnostic&, const SelectStmt &stmt, std::unique_ptr<Consumer> consumer)
{
    Catalog &C = Catalog::Get();
    auto query_graph = QueryGraph::Build(stmt);

    Optimizer Opt(C.plan_enumerator(), C.cost_function());
    auto optree = Opt(*query_graph);

    consumer->add_child(optree.release());

    auto &backend = C.default_backend();
    M_TIME_EXPR(backend.execute(*consumer), "Execute the query", C.timer());
}

void m::load_from_CSV(Diagnostic &diag, Table &table, const std::filesystem::path &path, std::size_t num_rows,
                      bool has_header, bool skip_header)
{
    diag.clear();
    DSVReader R(table, diag, num_rows, ',', '\\', '\"', has_header, skip_header);

    errno = 0;
    std::ifstream file(path);
    if (not file) {
        diag.e(Position(path.c_str())) << "Could not open file '" << path << '\'';
        if (errno)
            diag.err() << ": " << strerror(errno);
        diag.err() << std::endl;
    } else {
        R(file, path.c_str());
    }

    if (diag.num_errors() != 0)
        throw runtime_error("error while reading CSV file");
}

void m::execute_file(Diagnostic &diag, const std::filesystem::path &path)
{
    diag.clear();
    auto &C = Catalog::Get();

    errno = 0;
    std::ifstream in(path);
    if (not in) {
        auto errsv = errno;
        std::cerr << "Could not open '" << path << "'";
        if (errno)
            std::cerr << ": " << std::strerror(errsv);
        std::cerr << std::endl;
        exit(EXIT_FAILURE);
    }

    Lexer lexer(diag, C.get_pool(), path.c_str(), in);
    Parser parser(lexer);
    Sema sema(diag);

    while (parser.token()) {
        auto stmt = std::unique_ptr<Stmt>(parser.parse());
        if (diag.num_errors()) return;
        sema(*stmt);
        if (diag.num_errors()) return;
        execute_statement(diag, *stmt);
    }
}

m::StoreWriter::StoreWriter(Store &store)
    : store_(store)
{
    for (auto &attr : store.table())
        S.add({attr.table.name, attr.name}, attr.type);
}

m::StoreWriter::~StoreWriter() { }

void m::StoreWriter::append(const Tuple &tup) const
{
    store_.append();
    if (lin_ != &store_.linearization()) {
        lin_ = &store_.linearization();
        writer_ = std::make_unique<m::StackMachine>(m::Interpreter::compile_store(S, *lin_, store_.num_rows() - 1));
    }

    Tuple *args[] = { const_cast<Tuple*>(&tup) };
    (*writer_)(args);
}
