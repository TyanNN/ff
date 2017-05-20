#include "gtest/gtest.h"
#include "ASTParser.hpp"
#include "ParserShared.hpp"

TEST(OperatorDef, IntPlusStr) {
    ASTParser par("operator +(int x, str y) int { ret x; }");

    map<string, OperatorDefAST> ops = par.operators;

    ASSERT_GT(ops.size(), 0);

    OperatorDefAST op = ops.at("int+str");

    ASSERT_EQ(op.ret_type, _TType::Int);
    ASSERT_EQ(op.args[0].second, _TType::Int);
    ASSERT_EQ(op.args[1].second, _TType::Str);

    ASSERT_EQ(op.body.size(), 1);
}

TEST(OperatorDef, RefIntPlusRefStr) {
    ASTParser par("operator +(ref int x, ref str y) ref int { ret x; }");

    map<string, OperatorDefAST> ops = par.operators;

    ASSERT_GT(ops.size(), 0);

    OperatorDefAST op = ops.at("ref_int+ref_str");

    ASSERT_TRUE(op.ret_type.isRef());
    ASSERT_EQ(*op.ret_type.referenceTo, _TType::Int);

    ASSERT_EQ(*op.args[0].second.referenceTo, _TType::Int);
    ASSERT_EQ(*op.args[1].second.referenceTo, _TType::Str);

    ASSERT_EQ(op.body.size(), 1);
}
