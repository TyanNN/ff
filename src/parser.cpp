#include <sstream>
#include <map>
#include <iostream>
#include <memory>
#include <vector>
#include <algorithm>

#include <assert.h>

#include "parser.hpp"

using namespace std;

TokenStream::TokenStream(string s) {
    text = make_unique<stringstream>(s);

    types = { "int", "float", "bool", "str" };

    while (!text->eof()) {
        vec.push_back(getTok());
    }
}

pair<Token, string> TokenStream::getTok() {
    string IdentStr;

    while (isspace(lastchr))
        lastchr = text->get();

    if (lastchr == '"') {
        lastchr = text->get();
        while (lastchr != '"') {
            IdentStr += lastchr;
            lastchr = text->get();
        }
        lastchr = text->get();
        return {Token::StrLit, IdentStr};
    }

    static vector<char> op_chars = {'!','~','@','#','$','%','^','&','*','-','+','\\','/','<','>','='};

    if (any_of(begin(op_chars), --end(op_chars), [&](char c) { return lastchr == c; })) {
        IdentStr = lastchr;
        lastchr = text->get();
        while (any_of(begin(op_chars), end(op_chars), [&](char c) { return lastchr == c; })) {
            IdentStr += lastchr;
            lastchr = text->get();
        }
        return {Token::Operator, IdentStr};
    }

    bool f = false;
    if (isalpha(lastchr) || lastchr == '_') {
        f = true;
        IdentStr = lastchr;
        while (isalnum((lastchr = text->get())) || lastchr == '_')
            IdentStr += lastchr;
    }

    if (lastchr == '=') {
        IdentStr = lastchr;
        lastchr = text->get();
        if (lastchr == '=') {
            IdentStr += lastchr;
            lastchr = text->get();
            return {Token::Operator, IdentStr};
        } else {
            lastchr = text->get();
            return {Token::Eq, IdentStr};
        }
    }

    if (isdigit(lastchr)) {
        string numstr;
        bool f = false;

        do {
            if (lastchr == '.')
                f = true;
            numstr += lastchr;
            lastchr = text->get();
        } while (isdigit(lastchr) || lastchr == '.');

        if (!f)
            return {Token::IntLit, numstr};
        else
            return {Token::FloatLit, numstr};
    }

    #define match(wh, to, type) if (wh == to)\
            return {type, IdentStr};

    #define match_char(to, type) if (lastchr == to) {\
        lastchr = text->get();\
        IdentStr = to;\
        return {type, IdentStr};\
    }

    match(IdentStr, "==", Token::Operator);
    match(IdentStr, "fnc", Token::Fnc);
    match(IdentStr, "extern", Token::Extern);
    match(IdentStr, "operator", Token::OperatorDef);
    match(IdentStr, "include", Token::Include);
    match(IdentStr, "type", Token::TypeDef);

    if (any_of(begin(types), end(types), [&](string s) { return s == IdentStr; })) {
        return {Token::Type, IdentStr};
    }

    match(IdentStr, "true", Token::BoolLit);
    match(IdentStr, "false", Token::BoolLit);

    match(IdentStr, "if", Token::If);
    match(IdentStr, "else", Token::Else);
    match(IdentStr, "ret", Token::Ret);

    if (!f) {
        match_char('(', Token::OpP);
        match_char(')', Token::ClP);
        match_char('{', Token::OpCB);
        match_char('}', Token::ClCB);
        match_char('=', Token::Eq);
        match_char(';', Token::Semicolon);
        match_char('.', Token::Dot);
    }

    return {Token::Ident, IdentStr};
}

ASTParser::ASTParser(string s) : tokens(TokenStream(s)) {
    types = tokens.getTypes();
    while (true) {
        try {
            getNextTok();
            switch(currTok) {
                case Token::Fnc:
                    functions.push_back(parseFncDef());
                    break;
                case Token::Extern:
                    ext_functions.push_back(parseExternFnc());
                    break;
                case Token::Include:
                    includes.push_back(parseInclude());
                    break;
                case Token::TypeDef:
                    parseTypeDef();
                default:
                    break;
            }
        } catch (TokenStream::EOFException) {
            break;
        }
    }
}

bool ASTParser::isType(string s) {
    if (any_of(begin(types), end(types), [&](string c) { return c == s; })) {
        return true;
    }

    if (any_of(begin(typedefs), end(typedefs), [&](pair<const string, shared_ptr<TypeDefAST>> c) { return c.first == s; })) {
        return true;
    }

    return false;
}

#define if_type(t, s) (t == Token::Type || isType(s))
#define if_ident(t, s) (t == Token::Ident && !isType(s))

void ASTParser::parseTypeDef() {
    getNextTok(); // eat type token
    assert(if_ident(currTok, IdentStr));
    string name = IdentStr;
    getNextTok();
    assert(currTok == Token::OpCB);
    getNextTok();

    vector<pair<string, TType>> fields;
    while (currTok != Token::ClCB) {
        assert(isType(IdentStr));
        TType tp = strToType(IdentStr);
        getNextTok();
        assert(if_ident(currTok, IdentStr));
        string name = IdentStr;
        getNextTok();
        assert(currTok == Token::Semicolon);
        getNextTok(); // eat ;
        fields.push_back({name, tp});
    }
    typedefs.emplace(name, make_shared<TypeDefAST>(name, fields));
}

/// include = include <string literal> <string literal>*
unique_ptr<IncludeAST> ASTParser::parseInclude() {
    getNextTok(); // eat include
    assert(currTok == Token::StrLit);
    vector<string> mods;
    while (currTok != Token::Semicolon) {
        assert(currTok == Token::StrLit);
        mods.push_back(IdentStr);
        getNextTok();
    }

    return make_unique<IncludeAST>(mods);
}

/// str ::= "char+"
unique_ptr<BaseAST> ASTParser::parseStrLiteral() {
    auto res = make_unique<StrAST>(IdentStr);
    getNextTok();
    return move(res);
}

/// number ::= <number>
unique_ptr<BaseAST> ASTParser::parseIntLiteral() {
    auto res = make_unique<IntAST>(stoi(IdentStr));
    getNextTok();
    return move(res);
}

unique_ptr<BaseAST> ASTParser::parseFloatLiteral() {
    auto res = make_unique<FloatAST>(stof(IdentStr));
    getNextTok();
    return move(res);
}

unique_ptr<BaseAST> ASTParser::parseBoolLiteral() {
    bool b = (IdentStr == "true") ? true : false;
    auto res = make_unique<BoolAST>(b);
    getNextTok();

    return move(res);
}

TType ASTParser::strToType(string s) {
    if (s == "int")
        return _TType::Int;
    else if (s == "float")
        return _TType::Float;
    else if (s == "bool")
        return _TType::Bool;
    else if (s == "str")
        return _TType::Str;
    else if (isType(s))
        return s; // custom type
    else
        throw runtime_error("Unknown type:" + s);
}

unique_ptr<ExternFncAST> ASTParser::parseExternFnc() {
    getNextTok(); // eat extern

    assert(if_ident(currTok, IdentStr));

    string name = IdentStr;

    getNextTok();

    assert(currTok == Token::OpP);

    getNextTok(); // eat (

    vector<TType> args;

    while (currTok != Token::ClP) {
        assert(if_type(currTok, IdentStr));

        TType t = strToType(IdentStr);
        args.push_back(t);

        getNextTok();
    }

    getNextTok(); // eat )

    TType ret_type = _TType::Void;

    if (if_type(currTok, IdentStr)) {
        ret_type = strToType(IdentStr);
        getNextTok(); // eat type
    }

    assert(currTok == Token::Semicolon);

    auto res = make_unique<ExternFncAST>(name, args, ret_type);

    return res;
}

unique_ptr<OperatorDefAST> ASTParser::parseOperatorDef() {
    getNextTok(); // eat operator

    assert(currTok == Token::Operator);

    string name = IdentStr;

    getNextTok(); // eat name

    pair<string, TType> lhs;
    pair<string, TType> rhs;

    assert(currTok == Token::OpP);

    getNextTok();
    assert(if_type(currTok, IdentStr));
    lhs.second = strToType(IdentStr);
    getNextTok();
    assert(if_ident(currTok, IdentStr));
    lhs.first = IdentStr;

    getNextTok();
    assert(if_type(currTok, IdentStr));
    rhs.second = strToType(IdentStr);
    getNextTok();
    assert(if_ident(currTok, IdentStr));
    rhs.first = IdentStr;
    getNextTok();

    assert(currTok == Token::ClP);
    getNextTok(); // eat )

    assert(if_type(currTok, IdentStr));
    TType ret_type = strToType(IdentStr);
    getNextTok(); // eat type

    assert(currTok == Token::OpCB);

    getNextTok(); //eat {

    vector< unique_ptr<BaseAST> > body;

    while (currTok != Token::ClCB) {
        bool skip_sm = false;

        if (currTok == Token::If)
            skip_sm = true;

        body.push_back(parseStmt());

        if (currTok != Token::ClCB && skip_sm == false) {
            assert(currTok == Token::Semicolon);
            getNextTok();
        }
    }

    return make_unique<OperatorDefAST>(name, lhs, rhs, ret_type, move(body));
}

/// fncdef ::= 'fnc' <literal> '(' (<type> <literal>)* ')' <type>*
unique_ptr<FncDefAST> ASTParser::parseFncDef() {
    getNextTok(); // eat fnc

    assert(if_ident(currTok, IdentStr));

    string name = IdentStr;

    getNextTok();

    assert(currTok == Token::OpP);

    getNextTok(); // eat (

    map <string, TType> args;
    while (currTok != Token::ClP) {
        TType t = strToType(IdentStr);

        getNextTok();

        assert(if_ident(currTok, IdentStr));

        args.insert(make_pair(IdentStr, t));

        getNextTok();
    }
    getNextTok(); // eat )

    TType ret_type = _TType::Void;

    if (if_type(currTok, IdentStr)) {
        ret_type = strToType(IdentStr);
        getNextTok(); // eat type
    }

    assert(currTok == Token::OpCB);

    getNextTok(); //eat {

    vector< unique_ptr<BaseAST> > body;

    while (currTok != Token::ClCB) {
        bool skip_sm = false;

        if (currTok == Token::If)
            skip_sm = true;

        body.push_back(parseStmt());

        if (currTok != Token::ClCB && skip_sm == false) {
            assert(currTok == Token::Semicolon);
            getNextTok();
        }
    }

    auto res = make_unique<FncDefAST>(name, args, ret_type, move(body));
    return res;
}

unique_ptr<BaseAST> ASTParser::parseRet() {
    getNextTok();

    if (currTok != Token::Semicolon)
        return make_unique<RetAST>(parseExpr());
    else
        return make_unique<RetAST>(nullptr);
}

// stmt ::= var | expr ';'
// expr ::= <expr> | <fncall> | <return>
unique_ptr<BaseAST> ASTParser::parseStmt() {
    auto cTok = currTok;
    auto nTok = tokens.peek().first;

    string maybe_tmp = IdentStr;
    string maybe_tmp2 = tokens.peek().second;

    // var ::= <type>* <literal> ( '=' <expr> )* ';'

    if ((if_type(cTok, maybe_tmp) && if_ident(nTok, maybe_tmp2)) || (if_ident(cTok, maybe_tmp) && nTok == Token::Eq)) {
        return parseVar();
    } else if (if_ident(cTok, maybe_tmp) && nTok == Token::OpP) {
        return parseFncCall();
    }

    switch (currTok) {
        case Token::IntLit:
            return parseIntLiteral();
        case Token::FloatLit:
            return parseFloatLiteral();
        case Token::StrLit:
            return parseStrLiteral();
        case Token::Ret:
            return parseRet();
        case Token::If:
            return parseIf();
        default:
            break;
    }

    return nullptr;
}

// if ::= if <expr> { <stmt>* }
unique_ptr<BaseAST> ASTParser::parseIf() {
    getNextTok(); // eat if

    auto cond = parseExpr();

    assert(currTok == Token::OpCB);

    vector< unique_ptr<BaseAST> > body;
    vector< unique_ptr<BaseAST> > else_body;

    getNextTok(); // eat {

    while (currTok != Token::ClCB) {
        body.push_back(parseStmt());

        if (currTok != Token::ClCB) {
            assert(currTok == Token::Semicolon);
            getNextTok();
        }
    }

    getNextTok(); // eat { or get to else

    if (currTok == Token::Else) {
        getNextTok(); // eat else
        
        assert(currTok == Token::OpCB);
        
        getNextTok(); // eat {

        else_body.push_back(parseStmt());

        if (currTok != Token::ClCB) {
            assert(currTok == Token::Semicolon);
            getNextTok();
        }
        getNextTok(); // eat }
    }

    return make_unique<IfAST>(move(cond), move(body), move(else_body));
}

unique_ptr<BaseAST> ASTParser::parseExpr() {
    static bool parsing_op = false;

    Token nxt = tokens.peek().first;

    if (!parsing_op && nxt == Token::Operator) {
        parsing_op = true;
        auto lhs = parseExpr();
        parsing_op = false;
        return parseOperator(move(lhs));
    }

    switch(currTok) { // Simple casese
        case Token::IntLit:
            return parseIntLiteral();
        case Token::BoolLit:
            return parseFloatLiteral();
        case Token::StrLit:
            return parseStrLiteral();
        default: // Cases with variables
            if (currTok == Token::OpP) {
                getNextTok(); // eat (
                auto res = parseExpr();
                assert(currTok == Token::ClP);
                getNextTok(); // eat )

                if (currTok == Token::Operator) // FIXME
                    return parseOperator(move(res));

                return res;
            } else if (if_ident(currTok, IdentStr)) {
                if (nxt == Token::OpP) {
                    return parseFncCall();
                } else if (nxt == Token::Dot) {
                    return parseTypeFieldLoad();
                } else {
                    auto res = make_unique<IdentAST>(IdentStr);
                    getNextTok();
                    return move(res);
                }
            } else if (if_type(currTok, IdentStr)) {
                return parseType();
            }
    }

    throw runtime_error("Unknown expr");
}

unique_ptr<BaseAST> ASTParser::parseTypeFieldLoad() {
    string st_name = IdentStr;
    assert(!if_type(currTok, st_name));
    getNextTok();
    assert(currTok == Token::Dot); 
    getNextTok(); // eat dor
    string f_name = IdentStr;
    getNextTok();
    return make_unique<TypeFieldLoadAST>(st_name, f_name);
}

unique_ptr<BaseAST> ASTParser::parseType() {
    string name = IdentStr;
    getNextTok();
    assert(currTok == Token::OpCB);
    getNextTok();
    map<string, unique_ptr<BaseAST>> fields;
    while (currTok != Token::ClCB) {
        assert(if_ident(currTok, IdentStr));
        string f_name = IdentStr;
        getNextTok();
        assert(currTok == Token::Eq);
        getNextTok(); // eat =
        unique_ptr<BaseAST> f_val = parseExpr();
        fields.emplace(f_name, move(f_val));
    }
    getNextTok();
    return make_unique<TypeAST>(name, move(fields));  
}

// var ::= <type>* <literal> ( '=' <expr> )* ';'
unique_ptr<BaseAST> ASTParser::parseVar() {
    if (if_type(currTok, IdentStr)) { // Varible creation
        string type = IdentStr;

        TType t = strToType(type);
        
        getNextTok(); // eat type

        assert(if_ident(currTok, IdentStr));

        string name = IdentStr;

        getNextTok();

        if (currTok == Token::Semicolon) {
            return make_unique<DeclAST>(name, t, unique_ptr<BaseAST>(nullptr));
        } else if (currTok == Token::Eq) {
            getNextTok();

            return make_unique<DeclAST>(name, t, parseExpr());
        }
    } else if (if_ident(currTok, IdentStr)) { // varible assignment
        string name = IdentStr;

        getNextTok(); // eat =
        getNextTok(); // set to next

        return make_unique<AssAST>(name, parseExpr());
    }

    throw runtime_error("Unknown token");
}

unique_ptr<BaseAST> ASTParser::parseOperator(unique_ptr<BaseAST> lhs) {
    string name = IdentStr;
    getNextTok(); // eat op
    auto rhs = parseExpr();

    return make_unique<OperatorAST>(name, move(lhs), move(rhs));
}

// fncall ::= <literal> '(' <literal>* ')'
unique_ptr<BaseAST> ASTParser::parseFncCall() {
    vector< unique_ptr<BaseAST> > args;

    assert(if_ident(currTok, IdentStr));

    string name = IdentStr;

    getNextTok(); // eat name
    getNextTok(); // eat (

    while (currTok != Token::ClP) {
        args.push_back(parseExpr());
    }

    getNextTok(); // eat ;

    auto res = make_unique<FncCallAST>(name, move(args));

    return move(res);
}
