/*
 * Author: doe300
 *
 * See the file "LICENSE" for the full license governing this code.
 */

#include "OpCodes.h"

#include "../HalfType.h"
#include "../Values.h"
#include "../intrinsics/Operators.h"
#include "CompilationError.h"

#include <cmath>
#include <map>

using namespace vc4c;

std::string ConditionCode::to_string() const
{
    switch(*this)
    {
    case COND_ALWAYS:
        return "";
    case COND_CARRY_CLEAR:
        return "ifcc";
    case COND_CARRY_SET:
        return "ifc";
    case COND_NEGATIVE_CLEAR:
        return "ifnc";
    case COND_NEGATIVE_SET:
        return "ifn";
    case COND_NEVER:
        return "never";
    case COND_ZERO_CLEAR:
        return "ifzc";
    case COND_ZERO_SET:
        return "ifz";
    }
    throw CompilationError(
        CompilationStep::CODE_GENERATION, "Unsupported condition", std::to_string(static_cast<unsigned>(value)));
}

ConditionCode ConditionCode::invert() const
{
    switch(*this)
    {
    case COND_ALWAYS:
        return COND_NEVER;
    case COND_CARRY_CLEAR:
        return COND_CARRY_SET;
    case COND_CARRY_SET:
        return COND_CARRY_CLEAR;
    case COND_NEGATIVE_CLEAR:
        return COND_NEGATIVE_SET;
    case COND_NEGATIVE_SET:
        return COND_NEGATIVE_CLEAR;
    case COND_NEVER:
        return COND_ALWAYS;
    case COND_ZERO_CLEAR:
        return COND_ZERO_SET;
    case COND_ZERO_SET:
        return COND_ZERO_CLEAR;
    }
    throw CompilationError(CompilationStep::CODE_GENERATION, "Unsupported conditions", to_string());
}

bool ConditionCode::isInversionOf(const ConditionCode other) const
{
    return other == invert();
}

BranchCond ConditionCode::toBranchCondition() const
{
    switch(value)
    {
    case COND_ALWAYS.value:
        return BranchCond::ALWAYS;
    case COND_CARRY_CLEAR.value:
        return BranchCond::ALL_C_CLEAR;
    case COND_CARRY_SET.value:
        return BranchCond::ANY_C_SET;
    case COND_NEGATIVE_CLEAR.value:
        return BranchCond::ALL_N_CLEAR;
    case COND_NEGATIVE_SET.value:
        return BranchCond::ANY_N_SET;
    case COND_ZERO_CLEAR.value:
        return BranchCond::ALL_Z_CLEAR;
    case COND_ZERO_SET.value:
        return BranchCond::ANY_Z_SET;
    }
    throw CompilationError(CompilationStep::CODE_GENERATION, "Invalid condition for branch", to_string());
}

std::string Signaling::to_string() const
{
    switch(*this)
    {
    case SIGNAL_LOAD_ALPHA:
        return "loada";
    case SIGNAL_ALU_IMMEDIATE:
        return "imm";
    case SIGNAL_BRANCH:
        return "br";
    case SIGNAL_LOAD_COLOR:
        return "loadc";
    case SIGNAL_LOAD_COLOR_END:
        return "loadc_end";
    case SIGNAL_LOAD_COVERAGE:
        return "loadcov";
    case SIGNAL_THREAD_SWITCH_LAST:
        return "lthrsw";
    case SIGNAL_LOAD_IMMEDIATE:
        return "load_imm";
    case SIGNAL_LOAD_TMU0:
        return "load_tmu0";
    case SIGNAL_LOAD_TMU1:
        return "load_tmu1";
    case SIGNAL_NONE:
        return "";
    case SIGNAL_END_PROGRAM:
        return "thrend";
    case SIGNAL_UNLOCK_SCORE:
        return "scoreu";
    case SIGNAL_SOFT_BREAK:
        return "bkpt";
    case SIGNAL_SWITCH_THREAD:
        return "thrsw";
    case SIGNAL_WAIT_FOR_SCORE:
        return "scorew";
    }
    throw CompilationError(
        CompilationStep::CODE_GENERATION, "Unsupported signal", std::to_string(static_cast<unsigned>(value)));
}

bool Signaling::hasSideEffects() const
{
    return *this != SIGNAL_NONE && *this != SIGNAL_ALU_IMMEDIATE && *this != SIGNAL_LOAD_IMMEDIATE;
}

bool Signaling::triggersReadOfR4() const
{
    return *this == SIGNAL_LOAD_ALPHA || *this == SIGNAL_LOAD_COLOR || *this == SIGNAL_LOAD_COLOR_END ||
        *this == SIGNAL_LOAD_COVERAGE || *this == SIGNAL_LOAD_TMU0 || *this == SIGNAL_LOAD_TMU1;
}

std::string Unpack::to_string() const
{
    // http://maazl.de/project/vc4asm/doc/extensions.html#pack
    switch(*this)
    {
    case UNPACK_NOP:
    case UNPACK_NOP_PM:
        return "";
    case UNPACK_16A_32:
        return "sextLow16to32";
    case UNPACK_16B_32:
        return "sextHigh16to32";
    case UNPACK_8888_32:
        return "replMSB";
    case UNPACK_8A_32:
        return "zextByte0To32";
    case UNPACK_8B_32:
        return "zextByte1To32";
    case UNPACK_8C_32:
        return "zextByte2To32";
    case UNPACK_8D_32:
        return "zextByte3To32";
    case UNPACK_R4_16A_32:
        return "r4HalfLowToFloat";
    case UNPACK_R4_16B_32:
        return "r4HalfHighToFloat";
    case UNPACK_R4_ALPHA_REPLICATE:
        return "r4ReplAlpha";
    case UNPACK_R4_COLOR0:
        return "r4Byte0ToFloat";
    case UNPACK_R4_COLOR1:
        return "r4Byte1ToFloat";
    case UNPACK_R4_COLOR2:
        return "r4Byte2ToFloat";
    case UNPACK_R4_COLOR3:
        return "r4Byte3ToFloat";
    }
    throw CompilationError(
        CompilationStep::CODE_GENERATION, "Unsupported unpack-mode", std::to_string(static_cast<unsigned>(value)));
}

static Value unpackLiteral(Unpack mode, Literal literal, DataType type)
{
    switch(mode)
    {
    case UNPACK_NOP:
        return Value(literal, type);
    case UNPACK_16A_32:
    {
        // signed conversion -> truncate to unsigned short, bit-cast to signed short and sign-extend
        uint16_t lowWord = static_cast<uint16_t>(literal.unsignedInt());
        if(type.isFloatingType())
            return Value(Literal(static_cast<float>(half_t(lowWord))), type);
        int16_t lowWordSigned = bit_cast<uint16_t, int16_t>(lowWord);
        return Value(Literal(static_cast<int32_t>(lowWordSigned)), type);
    }
    case UNPACK_16B_32:
    {
        // signed conversion -> truncate to unsigned short, bit-cast to signed short and sign-extend
        uint16_t highWord = static_cast<uint16_t>(literal.unsignedInt() >> 16);
        if(type.isFloatingType())
            return Value(Literal(static_cast<float>(half_t(highWord))), type);
        int16_t highWordSigned = bit_cast<uint16_t, int16_t>(highWord);
        return Value(Literal(static_cast<int32_t>(highWordSigned)), type);
    }
    case UNPACK_R4_ALPHA_REPLICATE:
        FALL_THROUGH
    case UNPACK_8888_32:
    {
        // unsigned cast required to guarantee cutting off the value
        uint8_t lsb = static_cast<uint8_t>(literal.unsignedInt());
        return Value(Literal((static_cast<uint32_t>(lsb) << 24) | (static_cast<uint32_t>(lsb) << 16) |
                         (static_cast<uint32_t>(lsb) << 8) | lsb),
            type);
    }
    case UNPACK_8A_32:
    {
        if(type.isFloatingType())
            return unpackLiteral(UNPACK_R4_COLOR0, literal, type);
        // unsigned cast required to guarantee cutting off the value
        uint8_t byte0 = static_cast<uint8_t>(literal.unsignedInt());
        return Value(Literal(static_cast<uint32_t>(byte0)), type);
    }
    case UNPACK_8B_32:
    {
        if(type.isFloatingType())
            return unpackLiteral(UNPACK_R4_COLOR1, literal, type);
        // unsigned cast required to guarantee cutting off the value
        uint8_t byte1 = static_cast<uint8_t>(literal.unsignedInt() >> 8);
        return Value(Literal(static_cast<uint32_t>(byte1)), type);
    }
    case UNPACK_8C_32:
    {
        if(type.isFloatingType())
            return unpackLiteral(UNPACK_R4_COLOR2, literal, type);
        // unsigned cast required to guarantee cutting off the value
        uint8_t byte2 = static_cast<uint8_t>(literal.unsignedInt() >> 16);
        return Value(Literal(static_cast<uint32_t>(byte2)), type);
    }
    case UNPACK_8D_32:
    {
        if(type.isFloatingType())
            return unpackLiteral(UNPACK_R4_COLOR3, literal, type);
        // unsigned cast required to guarantee cutting off the value
        uint8_t byte3 = static_cast<uint8_t>(literal.unsignedInt() >> 24);
        return Value(Literal(static_cast<uint32_t>(byte3)), type);
    }
    case UNPACK_R4_16A_32:
    {
        uint16_t lowWord = static_cast<uint16_t>(literal.unsignedInt());
        return Value(Literal(static_cast<float>(half_t(lowWord))), type);
    }
    case UNPACK_R4_16B_32:
    {
        uint16_t highWord = static_cast<uint16_t>(literal.unsignedInt() >> 16);
        return Value(Literal(static_cast<float>(half_t(highWord))), type);
    }
    case UNPACK_R4_COLOR0:
    {
        // unsigned cast required to guarantee cutting off the value
        uint8_t byte0 = static_cast<uint8_t>(literal.unsignedInt());
        return Value(Literal(static_cast<float>(byte0) / 255.0f), type);
    }
    case UNPACK_R4_COLOR1:
    {
        // unsigned cast required to guarantee cutting off the value
        uint8_t byte1 = static_cast<uint8_t>(literal.unsignedInt() >> 8);
        return Value(Literal(static_cast<float>(byte1) / 255.0f), type);
    }
    case UNPACK_R4_COLOR2:
    {
        // unsigned cast required to guarantee cutting off the value
        uint8_t byte2 = static_cast<uint8_t>(literal.unsignedInt() >> 16);
        return Value(Literal(static_cast<float>(byte2) / 255.0f), type);
    }
    case UNPACK_R4_COLOR3:
    {
        // unsigned cast required to guarantee cutting off the value
        uint8_t byte3 = static_cast<uint8_t>(literal.unsignedInt() >> 24);
        return Value(Literal(static_cast<float>(byte3) / 255.0f), type);
    }
    }
    throw CompilationError(
        CompilationStep::GENERAL, "Unsupported unpack-mode", std::to_string(static_cast<unsigned>(mode.value)));
}

Optional<Value> Unpack::operator()(const Value& val) const
{
    // TODO are the r4 unpack values additional or instead-of the "normal" ones?
    if(!hasEffect())
        return val;
    // we never can pack complex types (even pointer, there are always 32-bit)
    if(!val.type.isSimpleType())
        return NO_VALUE;
    if(auto container = val.checkContainer())
    {
        // unpack vectors per element
        ContainerValue result(container->elements.size());
        for(const Value& elem : container->elements)
        {
            if(auto lit = elem.getLiteralValue())
                result.elements.emplace_back(unpackLiteral(*this, *lit, elem.type));
            else
                return NO_VALUE;
        }
        return Value(std::move(result), val.type);
    }
    // can only unpack literals
    if(auto lit = val.getLiteralValue())
        return unpackLiteral(*this, *lit, val.type);
    return NO_VALUE;
}

const Unpack Unpack::unpackTo32Bit(DataType type)
{
    if(type.getScalarBitCount() >= DataType::WORD)
        return UNPACK_NOP;
    if(type.getScalarBitCount() == DataType::HALF_WORD)
        return UNPACK_16A_32;
    if(type.getScalarBitCount() == DataType::BYTE)
        return UNPACK_8A_32;
    throw CompilationError(CompilationStep::GENERAL, "Unhandled type-width for unpack-modes", type.to_string());
}

bool Unpack::isPMBitSet() const
{
    // check whether pm bit set
    return value & 0x1;
}

bool Unpack::hasEffect() const
{
    // exclude "normal" NOP and NOP with pm bit set
    return value != 0 && value != 1;
}

std::string Pack::to_string() const
{
    // http://maazl.de/project/vc4asm/doc/extensions.html#pack
    switch(*this)
    {
    case PACK_NOP:
    case PACK_NOP_PM:
        return "";
    case PACK_32_16A:
        return "trunc32toLow16";
    case PACK_32_16A_S:
        return "sat16ToLow16";
    case PACK_32_16B:
        return "trunc32ToHigh16";
    case PACK_32_16B_S:
        return "sat16ToHigh16";
    case PACK_32_32:
        return "sat";
    case PACK_32_8888:
        return "replLSB";
    case PACK_32_8888_S:
        return "replLSBSat";
    case PACK_32_8A:
        return "truncLSBToByte0";
    case PACK_32_8A_S:
        return "satLSBToByte0";
    case PACK_32_8B:
        return "truncLSBToByte1";
    case PACK_32_8B_S:
        return "satLSBToByte1";
    case PACK_32_8C:
        return "truncLSBToByte2";
    case PACK_32_8C_S:
        return "satLSBToByte2";
    case PACK_32_8D:
        return "truncLSBToByte3";
    case PACK_32_8D_S:
        return "satLSBToByte3";
    case PACK_MUL_GRAY_REPLICATE:
        return "mulFloatToReplLSB";
    case PACK_MUL_COLOR0:
        return "mulFloatToByte0";
    case PACK_MUL_COLOR1:
        return "mulFloatToByte1";
    case PACK_MUL_COLOR2:
        return "mulFloatToByte2";
    case PACK_MUL_COLOR3:
        return "mulFloatToByte3";
    }
    throw CompilationError(
        CompilationStep::CODE_GENERATION, "Unsupported pack-mode", std::to_string(static_cast<unsigned>(value)));
}

Value packLiteral(Pack mode, Literal literal, DataType type, const ElementFlags& flags)
{
    switch(mode)
    {
    case PACK_NOP:
        return Value(literal, type);
    case PACK_32_16A:
        if(type.isFloatingType())
            return Value(Literal(static_cast<uint16_t>(half_t(literal.real()))), type);
        return Value(Literal(literal.unsignedInt() & 0xFFFF), type);
    case PACK_32_16A_S:
        if(type.isFloatingType())
            // TODO no saturation?
            return Value(Literal(static_cast<uint16_t>(half_t(literal.real()))), type);
        return Value(Literal(saturate<int16_t>(literal.signedInt()) & 0xFFFF), type);
    case PACK_32_16B:
        if(type.isFloatingType())
            return Value(Literal(static_cast<uint16_t>(half_t(literal.real())) << 16), type);
        return Value(Literal((literal.unsignedInt() & 0xFFFF) << 16), type);
    case PACK_32_16B_S:
        if(type.isFloatingType())
            // TODO no saturation?
            return Value(Literal(static_cast<uint16_t>(half_t(literal.real())) << 16), type);
        return Value(Literal(saturate<int16_t>(literal.signedInt()) << 16), type);
    case PACK_32_32:
        // this depends on signed integer overflow (to determine overflow and then saturate)
        switch(flags.overflow)
        {
        case FlagStatus::CLEAR:
            return Value(literal, type);
        case FlagStatus::SET:
            /*
             * Rationale:
             * add and sub can only add up t 1 bit:
             * - for signed positive overflow, the MSB is set (negative)
             * - for signed negative overflow, the MSB depends on the second MSB values?
             * TODO not correct for signed negative overflow?
             */
            return flags.negative == FlagStatus::CLEAR ? Value(Literal(0x80000000u), type) :
                                                         Value(Literal(0x7FFFFFFFu), type);
        case FlagStatus::UNDEFINED:
        default:
            throw CompilationError(CompilationStep::GENERAL, "Cannot saturate on unknown overflow flags",
                Value(literal, type).to_string());
        }
    case PACK_32_8888:
        return Value(Literal(((literal.unsignedInt() & 0xFF) << 24) | ((literal.unsignedInt() & 0xFF) << 16) |
                         ((literal.unsignedInt() & 0xFF) << 8) | (literal.unsignedInt() & 0xFF)),
            type);
    case PACK_32_8888_S:
        return Value(Literal((saturate<uint8_t>(literal.unsignedInt()) << 24) |
                         (saturate<uint8_t>(literal.unsignedInt()) << 16) |
                         (saturate<uint8_t>(literal.unsignedInt()) << 8) | saturate<uint8_t>(literal.unsignedInt())),
            type);
    case PACK_32_8A:
        return Value(Literal(literal.unsignedInt() & 0xFF), type);
    case PACK_32_8A_S:
        return Value(Literal(saturate<uint8_t>(literal.unsignedInt())), type);
    case PACK_32_8B:
        return Value(Literal((literal.unsignedInt() & 0xFF) << 8), type);
    case PACK_32_8B_S:
        return Value(Literal(saturate<uint8_t>(literal.unsignedInt()) << 8), type);
    case PACK_32_8C:
        return Value(Literal((literal.unsignedInt() & 0xFF) << 16), type);
    case PACK_32_8C_S:
        return Value(Literal(saturate<uint8_t>(literal.unsignedInt()) << 16), type);
    case PACK_32_8D:
        return Value(Literal((literal.unsignedInt() & 0xFF) << 24), type);
    case PACK_32_8D_S:
        return Value(Literal(saturate<uint8_t>(literal.unsignedInt()) << 24), type);
    case PACK_MUL_GRAY_REPLICATE:
    {
        auto tmp = static_cast<uint32_t>(literal.real() / 255.0f) & 0xFF;
        return Value(Literal(tmp << 24 | tmp << 16 | tmp << 8 | tmp), type);
    }
    case PACK_MUL_COLOR0:
    {
        auto tmp = static_cast<uint32_t>(literal.real() * 255.0f) & 0xFF;
        return Value(Literal(tmp), type);
    }
    case PACK_MUL_COLOR1:
    {
        auto tmp = static_cast<uint32_t>(literal.real() * 255.0f) & 0xFF;
        return Value(Literal(tmp << 8), type);
    }
    case PACK_MUL_COLOR2:
    {
        auto tmp = static_cast<uint32_t>(literal.real() * 255.0f) & 0xFF;
        return Value(Literal(tmp << 16), type);
    }
    case PACK_MUL_COLOR3:
    {
        auto tmp = static_cast<uint32_t>(literal.real() * 255.0f) & 0xFF;
        return Value(Literal(tmp << 24), type);
    }
    }
    throw CompilationError(
        CompilationStep::GENERAL, "Unsupported pack-mode", std::to_string(static_cast<unsigned>(mode.value)));
}

Optional<Value> Pack::operator()(const Value& val, const VectorFlags& flags) const
{
    // TODO are the mul pack modes additional or instead-of the "normal" ones? Can mul ALU also use"normal" pack mode?
    if(!hasEffect())
        return val;
    // we never can pack complex types (even pointer, there are always 32-bit)
    if(!val.type.isSimpleType())
        return NO_VALUE;
    if(auto container = val.checkContainer())
    {
        // pack vectors per element
        ContainerValue result(container->elements.size());
        for(std::size_t i = 0; i < container->elements.size(); ++i)
        {
            auto& elem = container->elements[i];
            if(auto lit = elem.getLiteralValue())
                result.elements.emplace_back(packLiteral(*this, *lit, elem.type, flags[i]));
            else
                return NO_VALUE;
        }
        return Value(std::move(result), val.type);
    }
    // can only pack literals
    if(auto lit = val.getLiteralValue())
        return packLiteral(*this, *lit, val.type, flags[0]);
    return NO_VALUE;
}

bool Pack::isPMBitSet() const
{
    // check for pm bit set
    return value & 0x10;
}

bool Pack::hasEffect() const
{
    // exclude "normal" NOP and NOP with pm bit set
    return value != 0 && value != 0x10;
}

std::string vc4c::toString(const SetFlag flag)
{
    switch(flag)
    {
    case SetFlag::DONT_SET:
        return "";
    case SetFlag::SET_FLAGS:
        return "setf";
    }
    throw CompilationError(
        CompilationStep::CODE_GENERATION, "Unsupported set-flags flag", std::to_string(static_cast<unsigned>(flag)));
}

bool vc4c::isFlagSetByMulALU(unsigned char opAdd, unsigned char opMul)
{
    // despite what the Broadcom specification states, only using mul ALU if add ALU executes nop.
    return opAdd == OP_NOP.opAdd && opMul != OP_NOP.opMul;
}

bool ElementFlags::matchesCondition(ConditionCode cond) const
{
    switch(cond.value)
    {
    case COND_ALWAYS.value:
        return true;
    case COND_CARRY_CLEAR.value:
    {
        if(carry == FlagStatus::UNDEFINED)
            throw CompilationError(CompilationStep::GENERAL, "Reading undefined carry flags");
        return carry == FlagStatus::CLEAR;
    }
    case COND_CARRY_SET.value:
    {
        if(carry == FlagStatus::UNDEFINED)
            throw CompilationError(CompilationStep::GENERAL, "Reading undefined carry flags");
        return carry == FlagStatus::SET;
    }
    case COND_NEGATIVE_CLEAR.value:
    {
        if(negative == FlagStatus::UNDEFINED)
            throw CompilationError(CompilationStep::GENERAL, "Reading undefined negative flags");
        return negative == FlagStatus::CLEAR;
    }
    case COND_NEGATIVE_SET.value:
    {
        if(negative == FlagStatus::UNDEFINED)
            throw CompilationError(CompilationStep::GENERAL, "Reading undefined negative flags");
        return negative == FlagStatus::SET;
    }
    case COND_NEVER.value:
        return false;
    case COND_ZERO_CLEAR.value:
    {
        if(zero == FlagStatus::UNDEFINED)
            throw CompilationError(CompilationStep::GENERAL, "Reading undefined zero flags");
        return zero == FlagStatus::CLEAR;
    }
    case COND_ZERO_SET.value:
    {
        if(zero == FlagStatus::UNDEFINED)
            throw CompilationError(CompilationStep::GENERAL, "Reading undefined zero flags");
        return zero == FlagStatus::SET;
    }
    }
    throw CompilationError(CompilationStep::GENERAL, "Unhandled condition code", cond.to_string());
}

static std::string toFlagString(FlagStatus flag, char flagChar)
{
    if(flag == FlagStatus::CLEAR)
        return "-";
    if(flag == FlagStatus::SET)
        return std::string(&flagChar, 1);
    return "?";
}

std::string ElementFlags::to_string() const
{
    return toFlagString(zero, 'z') + toFlagString(negative, 'n') + toFlagString(carry, 'c');
}

ElementFlags ElementFlags::fromValue(const Value& val)
{
    ElementFlags flags;
    if(auto lit = val.getLiteralValue())
    {
        // for both unsigned and float, the MSB is the sign and MBS(x) == 1 means x < 0
        flags.negative = lit->signedInt() < 0 ? FlagStatus::SET : FlagStatus::CLEAR;
        // for signed, unsigned and float, zero is all bits zero
        flags.zero = lit->unsignedInt() == 0 ? FlagStatus::SET : FlagStatus::CLEAR;
    }
    return flags;
}

VectorFlags VectorFlags::fromValue(const Value& val)
{
    if(val.getLiteralValue())
        return ElementFlags::fromValue(val);
    // TODO extract for vector of values
    return {};
}

bool OpCode::operator==(const OpCode& right) const
{
    if(opAdd > 0 && opAdd == right.opAdd)
        return true;
    if(opMul > 0 && opMul == right.opMul)
        return true;
    if(opAdd == 0 && opMul == 0 && right.opAdd == 0 && right.opMul == 0)
        return true;
    return false;
}

bool OpCode::operator!=(const OpCode& right) const
{
    return !(*this == right);
}

bool OpCode::operator<(const OpCode& right) const
{
    return opAdd < right.opAdd || opMul < right.opMul;
}

// Taken from https://stackoverflow.com/questions/2835469/how-to-perform-rotate-shift-in-c?noredirect=1&lq=1
static unsigned int rotate_right(unsigned int value, int shift)
{
    if((shift &= 31) == 0)
        return value;
    return (value >> shift) | (value << (32 - shift));
}

static std::pair<Optional<Value>, VectorFlags> setFlags(Value&& val)
{
    auto flags = VectorFlags::fromValue(val);
    return std::make_pair(std::forward<Value>(val), flags);
}

static std::pair<Optional<Value>, VectorFlags> setFlags(Value&& val, bool is32BitOverflow)
{
    auto tmp = setFlags(std::forward<Value>(val));
    auto flags = tmp.second[0];
    flags.carry = is32BitOverflow ? FlagStatus::SET : FlagStatus::CLEAR;
    return std::make_pair(std::move(tmp.first), flags);
}

static std::pair<Optional<Value>, VectorFlags> setFlags(Value&& val, bool is32BitOverflow, bool isSignedOverflow)
{
    auto tmp = setFlags(std::forward<Value>(val), is32BitOverflow);
    auto flags = tmp.second[0];
    flags.overflow = isSignedOverflow ? FlagStatus::SET : FlagStatus::CLEAR;
    return std::make_pair(std::move(tmp.first), flags);
}

static bool checkMinMaxCarry(const Literal& arg0, const Literal& arg1, bool useAbs = false)
{
    // VideoCore IV sets carry flag for fmin/fmax/fminabs/fmaxabs(a, b) if a > b
    // VideoCore IV considers NaN > Inf for min/fmax/fminabs/fmaxabs

    if(std::isnan(arg0.real()) && std::isnan(arg1.real()))
        // works, since the bit-representation is ordered same as integers
        return arg0.signedInt() > arg1.signedInt();
    if(std::isnan(arg0.real()))
        return true;
    if(std::isnan(arg1.real()))
        return false;
    return useAbs ? (std::abs(arg0.real()) > std::abs(arg1.real())) : (arg0.real() > arg1.real());
}

static PrecalculatedValue calcLiteral(
    const OpCode& code, Literal firstLit, Literal secondLit, DataType resultType, DataType firstType)
{
    if(code == OP_ADD)
    {
        auto extendedVal =
            static_cast<uint64_t>(firstLit.unsignedInt()) + static_cast<uint64_t>(secondLit.unsignedInt());
        auto signedVal = static_cast<int64_t>(firstLit.signedInt()) + static_cast<int64_t>(secondLit.signedInt());
        return setFlags(Value(Literal(firstLit.signedInt() + secondLit.signedInt()), resultType),
            extendedVal > static_cast<uint64_t>(0xFFFFFFFFul),
            signedVal > static_cast<int64_t>(std::numeric_limits<int32_t>::max()) ||
                signedVal < static_cast<int64_t>(std::numeric_limits<int32_t>::min()));
    }
    if(code == OP_AND)
        return setFlags(Value(Literal(firstLit.unsignedInt() & secondLit.unsignedInt()), resultType), false, false);
    if(code == OP_ASR)
    {
        // carry is set if bits set are shifted out of the register: val & (2^shift-offset-1) != 0
        auto shiftLoss = firstLit.unsignedInt() & ((1 << secondLit.signedInt()) - 1);
        return setFlags(Value(intermediate::asr(resultType, firstLit, secondLit), resultType), shiftLoss != 0, false);
    }
    if(code == OP_CLZ)
        return setFlags(Value(intermediate::clz(resultType, firstLit), resultType), false, false);
    if(code == OP_FADD)
        return setFlags(Value(Literal(firstLit.real() + secondLit.real()), resultType),
            (firstLit.real() + secondLit.real()) > 0.0f);
    if(code == OP_FMAX)
    {
        if(std::isnan(firstLit.real()))
            return setFlags(Value(firstLit, resultType), checkMinMaxCarry(firstLit, secondLit));
        if(std::isnan(secondLit.real()))
            return setFlags(Value(secondLit, resultType), checkMinMaxCarry(firstLit, secondLit));
        static_assert(std::max(std::numeric_limits<float>::infinity(), 0.0f) != 0.0f, "");
        static_assert(std::max(-std::numeric_limits<float>::infinity(), 0.0f) == 0.0f, "");
        return setFlags(Value(Literal(std::max(firstLit.real(), secondLit.real())), resultType),
            firstLit.real() > secondLit.real(), false);
    }
    if(code == OP_FMAXABS)
    {
        if(std::isnan(firstLit.real()))
            return setFlags(Value(firstLit, resultType), checkMinMaxCarry(firstLit, secondLit, true));
        if(std::isnan(secondLit.real()))
            return setFlags(Value(secondLit, resultType), checkMinMaxCarry(firstLit, secondLit, true));
        if(std::isinf(firstLit.real()))
            return setFlags(Value(firstLit, resultType), checkMinMaxCarry(firstLit, secondLit, true));
        if(std::isinf(secondLit.real()))
            return setFlags(Value(secondLit, resultType), checkMinMaxCarry(firstLit, secondLit, true));
        return setFlags(Value(Literal(std::max(std::fabs(firstLit.real()), std::fabs(secondLit.real()))), resultType),
            std::fabs(firstLit.real()) > std::fabs(secondLit.real()), false);
    }
    if(code == OP_FMIN)
    {
        if(std::isnan(firstLit.real()))
            return setFlags(Value(secondLit, resultType), checkMinMaxCarry(firstLit, secondLit));
        if(std::isnan(secondLit.real()))
            return setFlags(Value(firstLit, resultType), checkMinMaxCarry(firstLit, secondLit));
        static_assert(std::min(std::numeric_limits<float>::infinity(), 0.0f) == 0.0f, "");
        static_assert(std::min(-std::numeric_limits<float>::infinity(), 0.0f) != 0.0f, "");
        return setFlags(Value(Literal(std::min(firstLit.real(), secondLit.real())), resultType),
            firstLit.real() > secondLit.real(), false);
    }
    if(code == OP_FMINABS)
    {
        if(std::isnan(firstLit.real()))
            return setFlags(Value(secondLit, resultType), checkMinMaxCarry(firstLit, secondLit, true));
        if(std::isnan(secondLit.real()))
            return setFlags(Value(firstLit, resultType), checkMinMaxCarry(firstLit, secondLit, true));
        return setFlags(Value(Literal(std::min(std::fabs(firstLit.real()), std::fabs(secondLit.real()))), resultType),
            std::fabs(firstLit.real()) > std::fabs(secondLit.real()), false);
    }
    if(code == OP_FMUL)
        return setFlags(Value(Literal(firstLit.real() * secondLit.real()), resultType));
    if(code == OP_FSUB)
        return setFlags(Value(Literal(firstLit.real() - secondLit.real()), resultType),
            (firstLit.real() - secondLit.real()) > 0.0f);
    if(code == OP_FTOI)
    {
        if(std::isnan(firstLit.real()) || std::isinf(firstLit.real()) ||
            std::abs(static_cast<int64_t>(firstLit.real())) > std::numeric_limits<int32_t>::max())
            return setFlags(Value(Literal(0u), TYPE_INT32.toVectorType(firstType.getVectorWidth())));
        return setFlags(
            Value(Literal(static_cast<int32_t>(firstLit.real())), TYPE_INT32.toVectorType(firstType.getVectorWidth())),
            false);
    }
    if(code == OP_ITOF)
        return setFlags(Value(Literal(static_cast<float>(firstLit.signedInt())),
                            TYPE_FLOAT.toVectorType(firstType.getVectorWidth())),
            false);
    if(code == OP_MAX)
        return setFlags(Value(Literal(std::max(firstLit.signedInt(), secondLit.signedInt())), resultType),
            firstLit.signedInt() > secondLit.signedInt(), false);
    if(code == OP_MIN)
        return setFlags(Value(Literal(std::min(firstLit.signedInt(), secondLit.signedInt())), resultType),
            firstLit.signedInt() > secondLit.signedInt(), false);
    if(code == OP_MUL24)
    {
        auto extendedVal = static_cast<uint64_t>(firstLit.unsignedInt() & 0xFFFFFFu) *
            static_cast<uint64_t>(secondLit.unsignedInt() & 0xFFFFFFu);
        return setFlags(
            Value(Literal((firstLit.unsignedInt() & 0xFFFFFF) * (secondLit.unsignedInt() & 0xFFFFFF)), resultType),
            extendedVal > static_cast<uint64_t>(0xFFFFFFFFul));
    }
    if(code == OP_NOT)
        return setFlags(Value(Literal(~firstLit.unsignedInt()), resultType), false);
    if(code == OP_OR)
        return setFlags(Value(Literal(firstLit.unsignedInt() | secondLit.unsignedInt()), resultType), false, false);
    if(code == OP_ROR)
        return setFlags(Value(Literal(rotate_right(firstLit.unsignedInt(), secondLit.signedInt())), resultType), false);
    if(code == OP_SHL)
    {
        auto extendedVal = static_cast<uint64_t>(firstLit.unsignedInt())
            << static_cast<uint64_t>(secondLit.unsignedInt());
        return setFlags(Value(Literal(firstLit.unsignedInt() << secondLit.signedInt()), resultType),
            extendedVal > static_cast<uint64_t>(0xFFFFFFFFul));
    }
    if(code == OP_SHR)
    {
        // carry is set if bits set are shifted out of the register: val & (2^shift-offset-1) != 0
        auto shiftLoss = firstLit.unsignedInt() & ((1 << secondLit.signedInt()) - 1);
        return setFlags(Value(Literal(firstLit.unsignedInt() >> secondLit.signedInt()), resultType), shiftLoss != 0);
    }
    if(code == OP_SUB)
    {
        auto extendedVal = static_cast<int64_t>(firstLit.signedInt()) - static_cast<int64_t>(secondLit.signedInt());
        return setFlags(Value(Literal(firstLit.signedInt() - secondLit.signedInt()), resultType),
            extendedVal<0, extendedVal> static_cast<int64_t>(std::numeric_limits<int32_t>::max()) ||
                extendedVal < static_cast<int64_t>(std::numeric_limits<int32_t>::min()));
    }
    if(code == OP_XOR)
        return setFlags(Value(Literal(firstLit.unsignedInt() ^ secondLit.unsignedInt()), resultType), false, false);
    if(code == OP_V8ADDS || code == OP_V8SUBS || code == OP_V8MAX || code == OP_V8MIN || code == OP_V8MULD)
    {
        std::array<uint32_t, 4> bytesA, bytesB, bytesOut;
        bytesA[0] = firstLit.unsignedInt() & 0xFF;
        bytesA[1] = firstLit.unsignedInt() >> 8 & 0xFF;
        bytesA[2] = firstLit.unsignedInt() >> 16 & 0xFF;
        bytesA[3] = firstLit.unsignedInt() >> 24 & 0xFF;
        bytesB[0] = secondLit.unsignedInt() & 0xFF;
        bytesB[1] = secondLit.unsignedInt() >> 8 & 0xFF;
        bytesB[2] = secondLit.unsignedInt() >> 16 & 0xFF;
        bytesB[3] = secondLit.unsignedInt() >> 24 & 0xFF;
        std::transform(
            bytesA.begin(), bytesA.end(), bytesB.begin(), bytesOut.begin(), [&](uint32_t a, uint32_t b) -> uint32_t {
                if(code == OP_V8ADDS)
                    return std::min(a + b, 255u);
                if(code == OP_V8SUBS)
                    return static_cast<uint32_t>(std::max(std::min(static_cast<int32_t>(a - b), 255), 0));
                if(code == OP_V8MAX)
                    return std::max(a, b);
                if(code == OP_V8MIN)
                    return std::min(a, b);
                if(code == OP_V8MULD)
                    return (a * b + 127) / 255;
                throw CompilationError(CompilationStep::GENERAL, "Unhandled op-code", code.name);
            });
        uint32_t result = ((bytesOut[3] & 0xFF) << 24) | ((bytesOut[2] & 0xFF) << 16) | ((bytesOut[1] & 0xFF) << 8) |
            (bytesOut[0] & 0xFF);
        return setFlags(Value(Literal(result), resultType));
    }

    return std::make_pair(NO_VALUE, VectorFlags{});
}

PrecalculatedValue OpCode::operator()(const Value& firstOperand, const Optional<Value>& secondOperand) const
{
    if(numOperands > 1 && !secondOperand)
        return std::make_pair(NO_VALUE, VectorFlags{});

    if(numOperands == 1 && firstOperand.isUndefined())
        // returns an undefined value (of the correct type)
        return std::make_pair(
            (acceptsFloat == returnsFloat) ? Value(firstOperand.type) : UNDEFINED_VALUE, VectorFlags{});
    if(numOperands == 2 && secondOperand->isUndefined())
        // returns an undefined value (of the correct type)
        return std::make_pair((acceptsFloat == returnsFloat && firstOperand.type == secondOperand->type) ?
                Value(firstOperand.type) :
                UNDEFINED_VALUE,
            VectorFlags{});

    // extract the literal value behind the operands
    if(!firstOperand.getLiteralValue() && !firstOperand.checkContainer())
        return std::make_pair(NO_VALUE, VectorFlags{});
    Optional<Value> secondVal =
        !secondOperand || (secondOperand->getLiteralValue() || secondOperand->checkContainer()) ? secondOperand :
                                                                                                  NO_VALUE;
    if(numOperands > 1 && !secondVal)
        return std::make_pair(NO_VALUE, VectorFlags{});

    auto firstContainer = firstOperand.checkContainer();
    auto secondContainer = secondVal ? secondVal->checkContainer() : nullptr;

    // do not calculate vector rotations
    if(firstOperand.checkImmediate() && firstOperand.immediate().isVectorRotation())
        return std::make_pair(NO_VALUE, VectorFlags{});
    if(numOperands > 1 && secondVal->checkImmediate() && secondVal->immediate().isVectorRotation())
        return std::make_pair(NO_VALUE, VectorFlags{});

    // both (used) values are literals (or literal containers)
    bool calcPerComponent = (firstContainer && firstContainer->elements.size() > 1 && !firstContainer->isAllSame()) ||
        (numOperands > 1 && secondContainer && secondContainer->elements.size() > 1 && !secondContainer->isAllSame());
    DataType resultType = firstOperand.type;
    if(numOperands > 1 &&
        (secondVal->type.getVectorWidth() > resultType.getVectorWidth() ||
            secondVal->type.containsType(firstOperand.type)))
        resultType = secondVal->type;

    // at least one used value is a container, need to calculate component-wise
    if(calcPerComponent)
    {
        auto numElements = std::max(firstContainer ? firstContainer->elements.size() : 1,
            secondVal ? (secondContainer ? secondContainer->elements.size() : 1) : 0);
        ContainerValue res(numElements);
        VectorFlags flags;
        for(unsigned char i = 0; i < numElements; ++i)
        {
            PrecalculatedValue tmp{NO_VALUE, {}};
            // TODO call calcLiteral directly? Need to perform some checks
            if(numOperands == 1)
                tmp = operator()(firstContainer ? firstContainer->elements.at(i) : firstOperand, NO_VALUE);
            else
                tmp = operator()(firstContainer ? firstContainer->elements.at(i) : firstOperand,
                    secondContainer ? secondContainer->elements.at(i) : secondVal.value());
            if(!tmp.first)
                // result could not be calculated for a single component of the vector, abort
                return std::make_pair(NO_VALUE, VectorFlags{});
            res.elements.emplace_back(std::move(tmp.first).value());
            flags[i] = tmp.second[0];
        }
        return std::make_pair(Value(std::move(res), resultType), flags);
    }

    if(firstOperand.isUndefined() || (numOperands > 1 && secondVal && secondVal->isUndefined()))
        return std::make_pair(UNDEFINED_VALUE, VectorFlags{});

    // TODO throws if first element is no literal
    const Literal firstLit = firstOperand.getLiteralValue() ? firstOperand.getLiteralValue().value() :
                                                              firstContainer->elements.at(0).getLiteralValue().value();
    const Literal secondLit = (!secondVal || numOperands == 1) ?
        INT_ZERO.literal() :
        secondVal->getLiteralValue() ? secondVal->getLiteralValue().value() :
                                       secondContainer->elements.at(0).getLiteralValue().value();
    return calcLiteral(*this, firstLit, secondLit, resultType, firstOperand.type);
}

const OpCode& OpCode::toOpCode(const std::string& name)
{
    const OpCode& code = findOpCode(name);
    if(code == OP_NOP && name.compare("nop") != 0)
        throw CompilationError(CompilationStep::GENERAL, "No machine code operation for this op-code", name);
    return code;
}

static const std::multimap<std::string, OpCode> opCodes = {{OP_ADD.name, OP_ADD}, {OP_AND.name, OP_AND},
    {OP_ASR.name, OP_ASR}, {OP_CLZ.name, OP_CLZ}, {OP_FADD.name, OP_FADD}, {OP_FMAX.name, OP_FMAX},
    {OP_FMAXABS.name, OP_FMAXABS}, {OP_FMIN.name, OP_FMIN}, {OP_FMINABS.name, OP_FMINABS}, {OP_FMUL.name, OP_FMUL},
    {OP_FSUB.name, OP_FSUB}, {OP_FTOI.name, OP_FTOI}, {OP_ITOF.name, OP_ITOF}, {OP_MAX.name, OP_MAX},
    {OP_MIN.name, OP_MIN}, {OP_MUL24.name, OP_MUL24}, {OP_NOP.name, OP_NOP}, {OP_NOT.name, OP_NOT}, {OP_OR.name, OP_OR},
    {OP_ROR.name, OP_ROR}, {OP_SHL.name, OP_SHL}, {OP_SHR.name, OP_SHR}, {OP_SUB.name, OP_SUB},
    {OP_V8ADDS.name, OP_V8ADDS}, {OP_V8MAX.name, OP_V8MAX}, {OP_V8MIN.name, OP_V8MIN}, {OP_V8MULD.name, OP_V8MULD},
    {OP_V8SUBS.name, OP_V8SUBS}, {OP_XOR.name, OP_XOR}};

// NOTE: The indices MUST correspond to the op-codes!
static const std::array<OpCode, 32> addCodes = {OP_NOP, OP_FADD, OP_FSUB, OP_FMIN, OP_FMAX, OP_FMINABS, OP_FMAXABS,
    OP_FTOI, OP_ITOF, OP_NOP, OP_NOP, OP_NOP, OP_ADD, OP_SUB, OP_SHR, OP_ASR, OP_ROR, OP_SHL, OP_MIN, OP_MAX, OP_AND,
    OP_OR, OP_XOR, OP_NOT, OP_CLZ, OP_NOP, OP_NOP, OP_NOP, OP_NOP, OP_NOP, OP_V8ADDS, OP_V8SUBS};

// NOTE: The indices MUST correspond to the op-codes!
static const std::array<OpCode, 8> mulCodes = {
    OP_NOP, OP_FMUL, OP_MUL24, OP_V8MULD, OP_V8MIN, OP_V8MAX, OP_V8ADDS, OP_V8SUBS};

bool OpCode::isIdempotent() const
{
    if(*this == OP_AND || *this == OP_FMAX || *this == OP_FMIN || *this == OP_MAX || *this == OP_MIN ||
        *this == OP_OR || *this == OP_V8MAX || *this == OP_V8MIN)
    {
        return true;
    }
    return false;
}

bool OpCode::isAssociative() const
{
    return *this == OP_ADD || *this == OP_AND || *this == OP_FADD || *this == OP_FMAX || *this == OP_FMAXABS ||
        *this == OP_FMIN || *this == OP_FMINABS || *this == OP_FMUL || *this == OP_MAX || *this == OP_MIN ||
        *this == OP_OR || *this == OP_V8MAX || *this == OP_V8MIN || *this == OP_XOR;
}

bool OpCode::isCommutative() const
{
    return *this == OP_ADD || *this == OP_AND || *this == OP_FADD || *this == OP_FMAX || *this == OP_FMAXABS ||
        *this == OP_FMIN || *this == OP_FMINABS || *this == OP_FMUL || *this == OP_MAX || *this == OP_MIN ||
        *this == OP_MUL24 || *this == OP_OR || *this == OP_V8ADDS || *this == OP_V8MAX || *this == OP_V8MIN ||
        *this == OP_V8MULD || *this == OP_XOR;
}

bool OpCode::isLeftDistributiveOver(const OpCode& other) const
{
    if(*this == OP_FMUL)
        return other == OP_FADD || other == OP_FSUB;
    if(*this == OP_FADD)
        return other == OP_FMIN || other == OP_FMAX;
    if(*this == OP_ADD)
        return other == OP_MIN || other == OP_MAX;
    if(*this == OP_AND)
        return other == OP_OR || other == OP_XOR;
    return false;
}

bool OpCode::isRightDistributiveOver(const OpCode& other) const
{
    if(*this == OP_FMUL)
        return other == OP_FADD || other == OP_FSUB;
    if(*this == OP_FADD)
        return other == OP_FMIN || other == OP_FMAX;
    if(*this == OP_ADD)
        return other == OP_MIN || other == OP_MAX;
    if(*this == OP_AND)
        return other == OP_OR || other == OP_XOR;
    return false;
}

const OpCode& OpCode::toOpCode(const unsigned char opCode, const bool isMulALU)
{
    if(opCode == 0)
        return OP_NOP;
    if(isMulALU)
        return mulCodes.at(opCode);
    else
        return addCodes.at(opCode);
}

const OpCode& OpCode::findOpCode(const std::string& name)
{
    auto it = opCodes.find(name);
    if(it != opCodes.end())
        return it->second;
    return OP_NOP;
}

Optional<Value> OpCode::getLeftIdentity(const OpCode& code)
{
    if(code == OP_ADD)
        return INT_ZERO;
    if(code == OP_AND)
        return VALUE_ALL_BITS_SET;
    if(code == OP_FADD)
        return FLOAT_ZERO;
    if(code == OP_FMIN)
        return FLOAT_NAN;
    if(code == OP_FMAX)
        // -Inf
        return Value(Literal(0xFF800000), TYPE_FLOAT);
    if(code == OP_FMUL)
        return FLOAT_ONE;
    if(code == OP_MUL24)
        return INT_ONE;
    if(code == OP_OR)
        return INT_ZERO;
    if(code == OP_XOR)
        return INT_ZERO;
    return NO_VALUE;
}

Optional<Value> OpCode::getRightIdentity(const OpCode& code)
{
    if(code == OP_ADD)
        return INT_ZERO;
    if(code == OP_AND)
        return VALUE_ALL_BITS_SET;
    if(code == OP_ASR)
        return INT_ZERO;
    if(code == OP_FADD)
        return FLOAT_ZERO;
    if(code == OP_FMIN)
        return FLOAT_NAN;
    if(code == OP_FMUL)
        return FLOAT_ONE;
    if(code == OP_FSUB)
        return FLOAT_ZERO;
    if(code == OP_MUL24)
        return INT_ONE;
    if(code == OP_OR)
        return INT_ZERO;
    if(code == OP_ROR)
        return INT_ZERO;
    if(code == OP_SHL)
        return INT_ZERO;
    if(code == OP_SHR)
        return INT_ZERO;
    if(code == OP_SUB)
        return INT_ZERO;
    if(code == OP_XOR)
        return INT_ZERO;
    return NO_VALUE;
}

Optional<Value> OpCode::getLeftAbsorbingElement(const OpCode code)
{
    if(code == OP_AND)
        return INT_ZERO;
    if(code == OP_ASR)
        // XXX actually all bits set too
        return INT_ZERO;
    if(code == OP_FMAX)
        return FLOAT_NAN;
    if(code == OP_FMAXABS)
        return FLOAT_NAN;
    if(code == OP_FMINABS)
        return FLOAT_ZERO;
    if(code == OP_FMUL)
        return FLOAT_ZERO;
    if(code == OP_MUL24)
        return INT_ZERO;
    if(code == OP_OR)
        return VALUE_ALL_BITS_SET;
    if(code == OP_ROR)
        // XXX actually all bits set too
        return INT_ZERO;
    if(code == OP_SHL)
        return INT_ZERO;
    if(code == OP_SHR)
        return INT_ZERO;
    if(code == OP_V8MIN)
        return INT_ZERO;
    if(code == OP_V8MAX)
        return VALUE_ALL_BITS_SET;
    if(code == OP_V8MULD)
        return INT_ZERO;
    return NO_VALUE;
}

Optional<Value> OpCode::getRightAbsorbingElement(const OpCode code)
{
    if(code == OP_AND)
        return INT_ZERO;
    if(code == OP_FMAX)
        return FLOAT_NAN;
    if(code == OP_FMAXABS)
        return FLOAT_NAN;
    if(code == OP_FMINABS)
        return FLOAT_ZERO;
    if(code == OP_FMUL)
        return FLOAT_ZERO;
    if(code == OP_MUL24)
        return INT_ZERO;
    if(code == OP_OR)
        return VALUE_ALL_BITS_SET;
    if(code == OP_V8MIN)
        return INT_ZERO;
    if(code == OP_V8MAX)
        return VALUE_ALL_BITS_SET;
    if(code == OP_V8MULD)
        return INT_ZERO;
    return NO_VALUE;
}

std::string vc4c::toString(const BranchCond cond)
{
    switch(cond)
    {
    case BranchCond::ALL_C_CLEAR:
        return "ifallcc";
    case BranchCond::ALL_C_SET:
        return "ifallc";
    case BranchCond::ALL_N_CLEAR:
        return "ifallnc";
    case BranchCond::ALL_N_SET:
        return "ifalln";
    case BranchCond::ALL_Z_CLEAR:
        return "ifallzc";
    case BranchCond::ALL_Z_SET:
        return "ifallz";
    case BranchCond::ALWAYS:
        return "";
    case BranchCond::ANY_C_CLEAR:
        return "ifanycc";
    case BranchCond::ANY_C_SET:
        return "ifanyc";
    case BranchCond::ANY_N_CLEAR:
        return "ifanync";
    case BranchCond::ANY_N_SET:
        return "ifanyn";
    case BranchCond::ANY_Z_CLEAR:
        return "ifanyzc";
    case BranchCond::ANY_Z_SET:
        return "ifanyz";
    }
    throw CompilationError(
        CompilationStep::GENERAL, "Invalid branch-condition", std::to_string(static_cast<int>(cond)));
}
