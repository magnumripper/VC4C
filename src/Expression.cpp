#include "Expression.h"

using namespace vc4c;

Optional<Expression> Expression::createExpression(const intermediate::IntermediateInstruction& instr)
{
    if(instr.hasSideEffects())
        return {};
    if(instr.hasConditionalExecution())
        return {};
    if(instr.readsRegister(REG_REPLICATE_ALL) || instr.readsRegister(REG_REPLICATE_QUAD))
        // not actually a side-effect, but cannot be combined with any other expression
        return {};
    if(dynamic_cast<const intermediate::Operation*>(&instr) == nullptr &&
        dynamic_cast<const intermediate::MoveOperation*>(&instr) == nullptr &&
        dynamic_cast<const intermediate::LoadImmediate*>(&instr) == nullptr)
        // not an ALU or load operation
        return {};
    if(dynamic_cast<const intermediate::VectorRotation*>(&instr) != nullptr)
        // skip vector rotations
        return {};
    if(dynamic_cast<const intermediate::LoadImmediate*>(&instr) &&
        dynamic_cast<const intermediate::LoadImmediate*>(&instr)->type != intermediate::LoadType::REPLICATE_INT32)
        // skip loading of masked values
        return {};

    auto code = dynamic_cast<const intermediate::Operation*>(&instr) != nullptr ?
        dynamic_cast<const intermediate::Operation&>(instr).op :
        OP_V8MIN;
    return Expression{code, instr.getArgument(0).value(),
        instr.getArgument(1) ? instr.getArgument(1) : code == OP_V8MIN ? instr.getArgument(0) : NO_VALUE,
        instr.unpackMode, instr.packMode, instr.decoration};
}

bool Expression::operator==(const Expression& other) const
{
    return code == other.code &&
        ((arg0 == other.arg0 && arg1 == other.arg1) ||
            (code.isCommutative() && arg0 == other.arg1 && arg1 == other.arg0)) &&
        unpackMode == other.unpackMode && packMode == other.packMode && deco == other.deco;
}

std::string Expression::to_string() const
{
    return std::string(code.name) + " " + arg0.to_string() +
        (arg1 ? std::string(", ") + arg1.to_string() : std::string{});
}

bool Expression::isMoveExpression() const
{
    return (code == OP_OR || code == OP_V8MAX || code == OP_V8MIN) && arg1 == arg0;
}

Optional<Value> Expression::getConstantExpression() const
{
    return code(arg0, arg1).first;
}

bool Expression::hasConstantOperand() const
{
    return arg0.getLiteralValue() || arg0.checkContainer() ||
        (arg1 && (arg1->getLiteralValue() || arg1->checkContainer()));
}

Expression Expression::combineWith(const FastMap<const Local*, Expression>& inputs) const
{
    const Expression* expr0 = nullptr;
    const Expression* expr1 = nullptr;
    if(arg0.checkLocal() && inputs.find(arg0.local()) != inputs.end())
        expr0 = &inputs.at(arg0.local());
    if(arg1 && arg1->checkLocal() && inputs.find(arg1->local()) != inputs.end())
        expr1 = &inputs.at(arg1->local());
    if(expr0 == nullptr && expr1 == nullptr)
        // no expression can be combined
        return *this;

    if(unpackMode.hasEffect() || packMode.hasEffect() ||
        (expr0 != nullptr && (expr0->unpackMode.hasEffect() || expr0->packMode.hasEffect())) ||
        ((expr1 != nullptr && (expr1->unpackMode.hasEffect() || expr1->packMode.hasEffect()))))
        // cannot combine pack modes
        return *this;

    if(code.numOperands == 1 && expr0 != nullptr)
    {
        if(code.isIdempotent() && expr0->code == code)
            // f(f(a)) = f(a)
            return Expression{code, expr0->arg0, expr1->arg1, UNPACK_NOP, PACK_NOP, add_flag(deco, expr0->deco)};
        // NOTE: ftoi(itof(i)) != i, itof(ftoi(f)) != f, since the truncation/rounding would get lost!
        if(code == OP_NOT && expr0->code == OP_NOT)
            // not(not(a)) = a
            return Expression{OP_V8MIN, expr0->arg0, NO_VALUE, UNPACK_NOP, PACK_NOP, add_flag(deco, expr0->deco)};
    }

    auto firstArgConstant = arg0.getLiteralValue() || arg0.checkContainer() ?
        Optional<Value>(arg0) :
        expr0 ? expr0->getConstantExpression() : NO_VALUE;

    auto secondArgConstant = arg1 && (arg1->getLiteralValue() || arg1->checkContainer()) ?
        arg1 :
        expr1 ? expr1->getConstantExpression() : NO_VALUE;

    if(code.numOperands == 2)
    {
        if(code.isIdempotent() && arg0 == arg1)
            // f(a, a) = a
            return Expression{OP_V8MIN, arg0, arg0, UNPACK_NOP, PACK_NOP, deco};
        if(OpCode::getLeftIdentity(code) == arg0)
            // f(id, a) = a
            return Expression{OP_V8MIN, arg1.value(), arg1, UNPACK_NOP, PACK_NOP, deco};
        if(OpCode::getRightIdentity(code) == arg1)
            // f(a, id) = a
            return Expression{OP_V8MIN, arg0, arg0, UNPACK_NOP, PACK_NOP, deco};
        if(OpCode::getLeftAbsorbingElement(code) == arg0)
            // f(absorb, a) = absorb
            return Expression{OP_V8MIN, arg0, arg0, UNPACK_NOP, PACK_NOP, deco};
        if(OpCode::getRightAbsorbingElement(code) == arg1)
            // f(a, absorb) = absorb
            return Expression{OP_V8MIN, arg1.value(), arg1, UNPACK_NOP, PACK_NOP, deco};

        // TODO can use associative, commutative properties?
        // f(constA, f(constB, a)) = f(f(constA, constB), a), if associative
        // f(constA, f(a, constB)) = f(f(constA, constB), a), if associative and commutative
        // f(a, f(a, b)) = f(a, b), if associative and idempotent
        // f(a, f(b, a)) = f(a, b), if associative, commutative and idempotent
        // g(f(a, b), f(a, c)) = f(a, g(b, c)), if left distributive
        // g(f(a, b), f(c, b)) = f(g(a, c), b), if right distributive

        if(code == OP_FADD && arg0 == arg1)
        {
            // doesn't save any instruction, but utilizes mul ALU
            return Expression{OP_FMUL, arg0, Value(Literal(2.0f), TYPE_FLOAT), UNPACK_NOP, PACK_NOP, deco};
        }

        // TODO generalize! E.g. for fsub
        if(code == OP_FADD && expr0 && expr0->code == OP_FMUL)
        {
            if(expr0->arg0 == arg1 && expr0->arg1->getLiteralValue())
                // fadd(fmul(a, constB), a) = fmul(a, constB+1)
                return Expression{OP_FMUL, arg1.value(),
                    Value(Literal(expr0->arg1->getLiteralValue()->real() + 1.0f), TYPE_FLOAT), UNPACK_NOP, PACK_NOP,
                    add_flag(deco, expr0->deco)};
            if(expr0->arg1 == arg1 && expr0->arg0.getLiteralValue())
                // fadd(fmul(constB, a), a) = fmul(a, constB+1)
                return Expression{OP_FMUL, arg1.value(),
                    Value(Literal(expr0->arg0.getLiteralValue()->real() + 1.0f), TYPE_FLOAT), UNPACK_NOP, PACK_NOP,
                    add_flag(deco, expr0->deco)};
        }
        if(code == OP_FADD && expr1 && expr1->code == OP_FMUL)
        {
            if(expr1->arg0 == arg0 && expr1->arg1->getLiteralValue())
                // fadd(a, fmul(a, constB)) = fmul(a, constB+1)
                return Expression{OP_FMUL, arg0,
                    Value(Literal(expr1->arg1->getLiteralValue()->real() + 1.0f), TYPE_FLOAT), UNPACK_NOP, PACK_NOP,
                    add_flag(deco, expr1->deco)};
            if(expr1->arg1 == arg0 && expr1->arg0.getLiteralValue())
                // fadd(a, fmul(constB, a)) = fmul(a, constB+1)
                return Expression{OP_FMUL, arg0,
                    Value(Literal(expr1->arg0.getLiteralValue()->real() + 1.0f), TYPE_FLOAT), UNPACK_NOP, PACK_NOP,
                    add_flag(deco, expr1->deco)};
        }
    }

    return *this;
}

size_t std::hash<vc4c::Expression>::operator()(const vc4c::Expression& expr) const noexcept
{
    hash<const char*> nameHash;
    hash<Value> valHash;

    return nameHash(expr.code.name) ^ valHash(expr.arg0) ^ (expr.arg1 ? valHash(expr.arg1.value()) : 0) ^
        expr.unpackMode.value ^ expr.packMode.value ^ static_cast<unsigned>(expr.deco);
}
