﻿#include "FunctionSignatures.h"
#include "GEPTracker.h"
#include "OperandUtils.h"
#include "includes.h"
#include "lifterClass.h"
#include "utils.h"
#include <Zydis/Mnemonic.h>
#include <immintrin.h>
#include <iostream>
#include <llvm/IR/Constant.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PatternMatch.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/ErrorHandling.h>

FunctionType* lifterClass::parseArgsType(funcsignatures::functioninfo* funcInfo,
                                         LLVMContext& context) {
  if (!funcInfo) {
    FunctionType* externFuncType = FunctionType::get(
        Type::getInt64Ty(context),
        {llvm::Type::getInt64Ty(context), llvm::Type::getInt64Ty(context),
         llvm::Type::getInt64Ty(context), llvm::Type::getInt64Ty(context),
         llvm::Type::getInt64Ty(context), llvm::Type::getInt64Ty(context),
         llvm::Type::getInt64Ty(context), llvm::Type::getInt64Ty(context),
         llvm::Type::getInt64Ty(context), llvm::Type::getInt64Ty(context),
         llvm::Type::getInt64Ty(context), llvm::Type::getInt64Ty(context),
         llvm::Type::getInt64Ty(context), llvm::Type::getInt64Ty(context),
         llvm::Type::getInt64Ty(context), llvm::Type::getInt64Ty(context)},
        false);

    return externFuncType;
  }
  std::vector<llvm::Type*> argTypes;
  for (const auto& arg : funcInfo->args) {
    llvm::Type* type = nullptr;
    type = llvm::Type::getIntNTy(context, 8 << (arg.argtype.size - 1));

    if (arg.argtype.isPtr) {
      type = type->getPointerTo();
    }
    argTypes.push_back(type);
  }

  return llvm::FunctionType::get(llvm::Type::getInt64Ty(context), argTypes,
                                 false);
}

vector<Value*> lifterClass::parseArgs(funcsignatures::functioninfo* funcInfo) {
  auto& context = builder.getContext();

  auto RspRegister = GetRegisterValue(ZYDIS_REGISTER_RSP);
  if (!funcInfo)
    return {createZExtFolder(GetRegisterValue(ZYDIS_REGISTER_RAX),
                             Type::getInt64Ty(context)),
            createZExtFolder(GetRegisterValue(ZYDIS_REGISTER_RCX),
                             Type::getInt64Ty(context)),
            createZExtFolder(GetRegisterValue(ZYDIS_REGISTER_RDX),
                             Type::getInt64Ty(context)),
            createZExtFolder(GetRegisterValue(ZYDIS_REGISTER_RBX),
                             Type::getInt64Ty(context)),
            RspRegister,
            createZExtFolder(GetRegisterValue(ZYDIS_REGISTER_RBP),
                             Type::getInt64Ty(context)),
            createZExtFolder(GetRegisterValue(ZYDIS_REGISTER_RSI),
                             Type::getInt64Ty(context)),
            createZExtFolder(GetRegisterValue(ZYDIS_REGISTER_RDI),
                             Type::getInt64Ty(context)),
            createZExtFolder(GetRegisterValue(ZYDIS_REGISTER_RDI),
                             Type::getInt64Ty(context)),
            createZExtFolder(GetRegisterValue(ZYDIS_REGISTER_R8),
                             Type::getInt64Ty(context)),
            createZExtFolder(GetRegisterValue(ZYDIS_REGISTER_R9),
                             Type::getInt64Ty(context)),
            createZExtFolder(GetRegisterValue(ZYDIS_REGISTER_R10),
                             Type::getInt64Ty(context)),
            createZExtFolder(GetRegisterValue(ZYDIS_REGISTER_R11),
                             Type::getInt64Ty(context)),
            createZExtFolder(GetRegisterValue(ZYDIS_REGISTER_R12),
                             Type::getInt64Ty(context)),
            createZExtFolder(GetRegisterValue(ZYDIS_REGISTER_R13),
                             Type::getInt64Ty(context)),
            createZExtFolder(GetRegisterValue(ZYDIS_REGISTER_R14),
                             Type::getInt64Ty(context)),
            createZExtFolder(GetRegisterValue(ZYDIS_REGISTER_R15),
                             Type::getInt64Ty(context)),
            getMemory()};

  std::vector<Value*> args;
  for (const auto& arg : funcInfo->args) {
    Value* argValue = GetRegisterValue(arg.reg);
    argValue = createZExtOrTruncFolder(
        argValue, Type::getIntNTy(context, 8 << (arg.argtype.size - 1)));
    if (arg.argtype.isPtr)
      argValue = ConvertIntToPTR(builder, argValue);
    //  now convert to pointer if its a pointer
    args.push_back(argValue);
  }
  return args;
}

// probably move this stuff somewhere else
Value* lifterClass::callFunctionIR(const string& functionName,
                                   funcsignatures::functioninfo* funcInfo) {
  auto& context = builder.getContext();

  /*
  if (functionName == "GetTickCount64") {
      SetRegisterValue(
          builder, ZYDIS_REGISTER_RAX,
          ConstantInt::get(builder.getInt64Ty(), 1)); // rax = externalfunc()
      return;
  }
  */

  if (!funcInfo) {
    // try to get funcinfo from name
    funcInfo = funcsignatures::getFunctionInfo(functionName);
  }
  FunctionType* externFuncType = parseArgsType(funcInfo, context);
  auto M = builder.GetInsertBlock()->getParent()->getParent();

  // what about ordinals???????
  Function* externFunc = cast<Function>(
      M->getOrInsertFunction(functionName, externFuncType).getCallee());
  // fix calling
  vector<Value*> args = parseArgs(funcInfo);
  auto callresult = builder.CreateCall(externFunc, args);

  SetRegisterValue(ZYDIS_REGISTER_RAX,
                   callresult); // rax = externalfunc()
  // check if the function is exit or something similar to that
  return callresult;
}

Value* lifterClass::computeOverflowFlagAdc(Value* Lvalue, Value* Rvalue,
                                           Value* cf, Value* add) {
  auto cfc = createZExtOrTruncFolder(cf, add->getType(), "ofadc1");
  auto ofAdd = createAddFolder(add, cfc, "ofadc2");
  auto xor0 = createXorFolder(Lvalue, ofAdd, "ofadc3");
  auto xor1 = createXorFolder(Rvalue, ofAdd, "ofadc4");
  auto ofAnd = createAndFolder(xor0, xor1, "ofadc5");
  return createICMPFolder(CmpInst::ICMP_SLT, ofAnd,
                          ConstantInt::get(ofAnd->getType(), 0), "ofadc6");
}

Value* lifterClass::computeOverflowFlagAdd(Value* Lvalue, Value* Rvalue,
                                           Value* add) {
  auto xor0 = createXorFolder(Lvalue, add, "ofadd");
  auto xor1 = createXorFolder(Rvalue, add, "ofadd1");
  auto ofAnd = createAndFolder(xor0, xor1, "ofadd2");
  return createICMPFolder(CmpInst::ICMP_SLT, ofAnd,
                          ConstantInt::get(ofAnd->getType(), 0), "ofadd3");
}

Value* lifterClass::computeOverflowFlagSub(Value* Lvalue, Value* Rvalue,
                                           Value* sub) {
  auto xor0 = createXorFolder(Lvalue, Rvalue, "ofsub");
  auto xor1 = createXorFolder(Lvalue, sub, "ofsub1");
  auto ofAnd = createAndFolder(xor0, xor1, "ofsub2");
  return createICMPFolder(CmpInst::ICMP_SLT, ofAnd,
                          ConstantInt::get(ofAnd->getType(), 0), "ofsub3");
}

Value* lifterClass::computeOverflowFlagSbb(Value* Lvalue, Value* Rvalue,
                                           Value* cf, Value* sub) {
  auto cfc = createZExtOrTruncFolder(cf, sub->getType(), "ofsbb");
  auto ofSub = createSubFolder(sub, cfc, "ofsbb1");
  auto xor0 = createXorFolder(Lvalue, Rvalue, "ofsbb2");
  auto xor1 = createXorFolder(Lvalue, ofSub, "ofsbb3");
  auto ofAnd = createAndFolder(xor0, xor1, "ofsbb4");
  return createICMPFolder(CmpInst::ICMP_SLT, ofAnd,
                          ConstantInt::get(ofAnd->getType(), 0), "ofsbb5");
}

Value* lifterClass::computeAuxFlag(Value* Lvalue, Value* Rvalue,
                                   Value* result) {
  auto auxc = ConstantInt::get(result->getType(), 0x10);
  auto aux1 = createXorFolder(result, createXorFolder(Lvalue, Rvalue));
  auto aux2 = createAndFolder(auxc, aux1);
  auto af = createICMPFolder(CmpInst::ICMP_EQ, aux2, auxc);
  return af;
}

/*
https://graphics.stanford.edu/~seander/bithacks.html#ParityWith64Bits

Compute parity of a byte using 64-bit multiply and modulus division
unsigned char b;  // byte value to compute the parity of
bool parity =
  (((b * 0x0101010101010101ULL) & 0x8040201008040201ULL) % 0x1FF) & 1;
The method above takes around 4 operations, but only works on bytes.
*/
Value* lifterClass::computeParityFlag(Value* value) {
  LLVMContext& context = value->getContext();

  Value* lsb = createZExtFolder(
      createAndFolder(value, ConstantInt::get(value->getType(), 0xFF), "lsb"),
      Type::getInt64Ty(context));

  // s or u rem?
  Value* parity = createAndFolder(

      createURemFolder(
          createAndFolder(

              createMulFolder(
                  lsb, ConstantInt::get(lsb->getType(), 0x0101010101010101),
                  "pf1"),
              ConstantInt::get(lsb->getType(), 0x8040201008040201ULL), "pf2"),
          ConstantInt::get(lsb->getType(), 0x1FF), "pf3"),
      ConstantInt::get(lsb->getType(), 1), "pf4");
  // parity
  parity = createICMPFolder(CmpInst::ICMP_EQ,
                            ConstantInt::get(lsb->getType(), 0), parity, "pf5");
  return parity; // Returns 1 if even parity, 0 if odd
}

Value* lifterClass::computeZeroFlag(Value* value) { // x == 0 = zf
  return createICMPFolder(CmpInst::ICMP_EQ, value,
                          ConstantInt::get(value->getType(), 0), "zeroflag");
}

Value* lifterClass::computeSignFlag(Value* value) { // x < 0 = sf
  return createICMPFolder(CmpInst::ICMP_SLT, value,
                          ConstantInt::get(value->getType(), 0), "signflag");
}

// this function is used for jumps that are related to user, ex: vms using
// different handlers, jmptables, etc.

void lifterClass::branchHelper(Value* condition, const string& instname,
                               int numbered, bool reverse) {
  // TODO:
  // save the current state of memory, registers etc.,
  // after execution is finished, return to latest state and continue
  // execution from the other branch

  auto block = builder.GetInsertBlock();
  block->setName(instname + to_string(numbered));
  auto function = block->getParent();

  auto dest = operands[0];
  auto true_jump_addr = dest.imm.value.s + blockInfo.runtime_address;
  Value* true_jump =
      ConstantInt::get(function->getReturnType(), true_jump_addr);
  auto false_jump_addr = blockInfo.runtime_address;
  Value* false_jump =
      ConstantInt::get(function->getReturnType(), false_jump_addr);
  Value* next_jump = nullptr;

  if (!reverse)
    next_jump = createSelectFolder(condition, true_jump, false_jump);
  else
    next_jump = createSelectFolder(condition, false_jump, true_jump);

  uint64_t destination = 0;
  solvePath(function, destination, next_jump);

  block->setName("previousjmp_block-" + to_string(destination) + "-");
  // cout << "pathInfo:" << pathInfo << " dest: " << destination  <<
  // "\n";
}

void lifterClass::lift_bextr() {
  auto src2 = operands[2];
  auto src1 = operands[1];
  auto dst = operands[0];
  auto info = GetOperandValue(src2, src2.size);
  auto source = GetOperandValue(src1, src1.size);

  auto start = createTruncFolder(info, Type::getInt8Ty(fnc->getContext()));

  auto len = createTruncFolder(
      createLShrFolder(info, ConstantInt::get(info->getType(), 8)),
      Type::getInt8Ty(fnc->getContext()));

  Value* bitmask = createAShrFolder(
      createShlFolder(ConstantInt::get(len->getType(), 1), len), len);
  auto source2 =
      createAndFolder(source, createZExtFolder(bitmask, source->getType()));

  SetOperandValue(dst, source2);
  setFlag(FLAG_ZF, createICMPFolder(CmpInst::ICMP_EQ, source2,
                                    ConstantInt::get(source->getType(), 0)));
}

void lifterClass::lift_movs_X() {
  LLVMContext& context = builder.getContext();
  // Get the size based on the operand
  int size = operands[1].size;

  // DEST := SRC;
  // Parameterize size and direction
  // sign = DF (-1/+1)
  // incdecv = size*sign

  Value* DSTptrvalue = GetOperandValue(operands[1], size);
  SetOperandValue(operands[0], DSTptrvalue);

  bool isREP = (instruction.attributes & ZYDIS_ATTRIB_HAS_REP) != 0;

  Value* DF = getFlag(FLAG_DF);

  // sign = (DF*(DF+1)) - 1
  // v = sign * byteSize

  auto byteSizeValue = size;

  auto SRCop = operands[2 + isREP];
  auto DSTop = operands[3 + isREP];
  printvalue(DF);
  Value* Direction = createSelectFolder(
      DF,
      ConstantInt::get(Type::getIntNTy(context, SRCop.size), 1 * byteSizeValue),
      ConstantInt::get(Type::getIntNTy(context, SRCop.size),
                       -1 * byteSizeValue));
  printvalue(Direction);

  Value* SRCvalue = GetOperandValue(SRCop, SRCop.size);
  Value* DSTvalue = GetOperandValue(DSTop, DSTop.size);

  if (isREP) {
    Value* count = GetOperandValue(operands[2], operands[2].size);
    if (auto countci = dyn_cast<ConstantInt>(count)) {
      Value* UpdateSRCvalue = SRCvalue;
      Value* UpdateDSTvalue = DSTvalue;
      uint64_t looptime = countci->getZExtValue();

      for (int i = looptime; i > 0; i--) {
        DSTptrvalue = GetOperandValue(operands[1], size);
        SetOperandValue(operands[0], DSTptrvalue);

        UpdateSRCvalue = createAddFolder(UpdateSRCvalue, Direction);
        UpdateDSTvalue = createAddFolder(UpdateDSTvalue, Direction);

        SetOperandValue(SRCop, UpdateSRCvalue);
        SetOperandValue(DSTop, UpdateDSTvalue);

        if (i > 1)
          debugging::increaseInstCounter();
      }

      SetOperandValue(operands[2], ConstantInt::get(count->getType(), 0));
      return;
    } else {
      UNREACHABLE("fix rep");
    }
  }

  Value* UpdateSRCvalue = createAddFolder(SRCvalue, Direction);
  Value* UpdateDSTvalue = createAddFolder(DSTvalue, Direction);

  SetOperandValue(SRCop, UpdateSRCvalue);
  SetOperandValue(DSTop, UpdateDSTvalue);
}
/*
void lifterClass::lift_movaps() {
  auto dest = operands[0];
  auto src = operands[1];

  auto Rvalue =
      GetOperandValue(src, src.size, to_string(blockInfo.runtime_address));
  SetOperandValue(dest, Rvalue, to_string(blockInfo.runtime_address));
}
*/
void lifterClass::lift_mov() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];
  auto src = operands[1];

  auto Rvalue =
      GetOperandValue(src, src.size, to_string(blockInfo.runtime_address));

  switch (instruction.mnemonic) {
  case ZYDIS_MNEMONIC_MOVSX: {
    Rvalue =
        createSExtFolder(Rvalue, Type::getIntNTy(context, dest.size),
                         "movsx-" + to_string(blockInfo.runtime_address) + "-");
    break;
  }
  case ZYDIS_MNEMONIC_MOVZX: {
    Rvalue =
        createZExtFolder(Rvalue, Type::getIntNTy(context, dest.size),
                         "movzx-" + to_string(blockInfo.runtime_address) + "-");
    break;
  }
  case ZYDIS_MNEMONIC_MOVSXD: {
    Rvalue = createSExtFolder(Rvalue, Type::getIntNTy(context, dest.size),
                              "movsxd-" + to_string(blockInfo.runtime_address) +
                                  "-");
    break;
  }
  default: {
    break;
  }
  }
  printvalue(Rvalue);
  if (src.type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
    Rvalue = GetOperandValue(src, dest.size);
  }

  printvalue(Rvalue);

  SetOperandValue(dest, Rvalue, to_string(blockInfo.runtime_address));
}

void lifterClass::lift_cmovbz() {

  auto dest = operands[0];
  auto src = operands[1];

  Value* zf = getFlag(FLAG_ZF);
  Value* cf = getFlag(FLAG_CF);

  Value* condition = createOrFolder(zf, cf, "cmovbz-or");

  Value* Rvalue = GetOperandValue(src, dest.size);
  Value* Lvalue = GetOperandValue(dest, dest.size);

  Value* result = createSelectFolder(condition, Rvalue, Lvalue);

  SetOperandValue(dest, result);
}

void lifterClass::lift_cmovnbz() {
  auto dest = operands[0];
  auto src = operands[1];

  Value* Lvalue = GetOperandValue(dest, dest.size);
  Value* Rvalue = GetOperandValue(src, src.size);

  Value* cf = getFlag(FLAG_CF);
  Value* zf = getFlag(FLAG_ZF);

  Value* nbeCondition =
      createAndFolder(createNotFolder(cf), createNotFolder(zf), "nbeCondition");

  Value* resultValue =
      createSelectFolder(nbeCondition, Rvalue, Lvalue, "cmovnbe");

  SetOperandValue(dest, resultValue, to_string(blockInfo.runtime_address));
}

void lifterClass::lift_cmovz() {
  auto dest = operands[0];
  auto src = operands[1];

  Value* Lvalue = GetOperandValue(dest, dest.size);
  Value* Rvalue = GetOperandValue(src, src.size);

  Value* zf = getFlag(FLAG_ZF);

  Value* resultValue = createSelectFolder(zf, Rvalue, Lvalue, "cmovz");

  SetOperandValue(dest, resultValue, to_string(blockInfo.runtime_address));
}

void lifterClass::lift_cmovnz() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];
  auto src = operands[1];

  Value* zf = getFlag(FLAG_ZF);
  zf = createICMPFolder(CmpInst::ICMP_EQ, zf,
                        ConstantInt::get(Type::getInt1Ty(context), 0));

  Value* Rvalue = GetOperandValue(src, dest.size);
  Value* Lvalue = GetOperandValue(dest, dest.size);

  Value* result = createSelectFolder(zf, Rvalue, Lvalue);

  SetOperandValue(dest, result);
}
void lifterClass::lift_cmovl() {

  auto dest = operands[0];
  auto src = operands[1];

  Value* sf = getFlag(FLAG_SF);
  Value* of = getFlag(FLAG_OF);

  Value* condition = createICMPFolder(CmpInst::ICMP_NE, sf, of);

  Value* Rvalue = GetOperandValue(src, dest.size);
  Value* Lvalue = GetOperandValue(dest, dest.size);

  Value* result = createSelectFolder(condition, Rvalue, Lvalue);
  printvalue(sf);
  printvalue(sf);
  printvalue(Rvalue);
  printvalue(Lvalue);
  printvalue(result);
  SetOperandValue(dest, result);
}

void lifterClass::lift_cmovb() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];
  auto src = operands[1];

  Value* cf = getFlag(FLAG_CF);

  Value* condition = createICMPFolder(
      CmpInst::ICMP_EQ, cf, ConstantInt::get(Type::getInt1Ty(context), 1));

  Value* Rvalue = GetOperandValue(src, dest.size);
  Value* Lvalue = GetOperandValue(dest, dest.size);

  Value* result = createSelectFolder(condition, Rvalue, Lvalue);
  printvalue(condition);
  printvalue(Lvalue);
  printvalue(Rvalue);
  printvalue(result);
  SetOperandValue(dest, result);
}

void lifterClass::lift_cmovnb() {
  auto dest = operands[0];
  auto src = operands[1];

  Value* Lvalue = GetOperandValue(dest, dest.size);
  Value* Rvalue = GetOperandValue(src, src.size);

  Value* cf = getFlag(FLAG_CF);

  Value* resultValue =
      createSelectFolder(createNotFolder(cf), Rvalue, Lvalue, "cmovnb");

  SetOperandValue(dest, resultValue, to_string(blockInfo.runtime_address));
}

void lifterClass::lift_cmovns() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];
  auto src = operands[1];

  Value* sf = getFlag(FLAG_SF);

  Value* condition = createICMPFolder(
      CmpInst::ICMP_EQ, sf, ConstantInt::get(Type::getInt1Ty(context), 0));

  Value* Rvalue = GetOperandValue(src, dest.size);
  Value* Lvalue = GetOperandValue(dest, dest.size);

  Value* result = createSelectFolder(condition, Rvalue, Lvalue);

  SetOperandValue(dest, result);
}
// cmovnl = cmovge
void lifterClass::lift_cmovnl() {
  auto dest = operands[0];
  auto src = operands[1];

  Value* sf = getFlag(FLAG_SF);
  Value* of = getFlag(FLAG_OF);
  Value* condition = createICMPFolder(CmpInst::ICMP_EQ, sf, of);

  Value* Rvalue = GetOperandValue(src, dest.size);
  Value* Lvalue = GetOperandValue(dest, dest.size);
  printvalue(sf);
  printvalue(of);
  printvalue(condition);
  printvalue(Lvalue);
  printvalue(Rvalue);

  Value* result = createSelectFolder(condition, Rvalue, Lvalue);

  SetOperandValue(dest, result);
}
void lifterClass::lift_cmovs() {
  auto dest = operands[0];
  auto src = operands[1];

  Value* sf = getFlag(FLAG_SF);

  Value* Rvalue = GetOperandValue(src, dest.size);
  Value* Lvalue = GetOperandValue(dest, dest.size);

  Value* result = createSelectFolder(sf, Rvalue, Lvalue);

  SetOperandValue(dest, result);
}

void lifterClass::lift_cmovnle() {

  auto dest = operands[0];
  auto src = operands[1];

  Value* zf = getFlag(FLAG_ZF);
  Value* sf = getFlag(FLAG_SF);
  Value* of = getFlag(FLAG_OF);

  Value* condition = createAndFolder(
      createNotFolder(zf, "notZF"),
      createICMPFolder(CmpInst::ICMP_EQ, sf, of, "sf_eq_of"), "cmovnle_cond");

  Value* Rvalue = GetOperandValue(src, dest.size);
  Value* Lvalue = GetOperandValue(dest, dest.size);

  Value* result = createSelectFolder(condition, Rvalue, Lvalue);

  SetOperandValue(dest, result);
}

void lifterClass::lift_cmovle() {
  auto dest = operands[0];
  auto src = operands[1];

  Value* zf = getFlag(FLAG_ZF);
  Value* sf = getFlag(FLAG_SF);
  Value* of = getFlag(FLAG_OF);

  Value* sf_neq_of = createICMPFolder(CmpInst::ICMP_NE, sf, of);
  Value* condition = createOrFolder(zf, sf_neq_of, "cmovle-or");

  Value* Rvalue = GetOperandValue(src, dest.size);
  Value* Lvalue = GetOperandValue(dest, dest.size);

  Value* result = createSelectFolder(condition, Rvalue, Lvalue);
  printvalue(zf);
  printvalue(sf);
  printvalue(of);
  printvalue(sf_neq_of);
  printvalue(condition);
  printvalue(Rvalue);
  printvalue(Lvalue);
  printvalue(result);
  SetOperandValue(dest, result);
}

void lifterClass::lift_cmovo() {
  auto dest = operands[0];
  auto src = operands[1];

  Value* of = getFlag(FLAG_OF);

  Value* Rvalue = GetOperandValue(src, dest.size);
  Value* Lvalue = GetOperandValue(dest, dest.size);

  Value* result = createSelectFolder(of, Rvalue, Lvalue);
  printvalue(Lvalue);
  printvalue(Rvalue);
  printvalue(of);
  printvalue(result);
  SetOperandValue(dest, result);
}
void lifterClass::lift_cmovno() {
  auto dest = operands[0];
  auto src = operands[1];

  Value* of = getFlag(FLAG_OF);

  printvalue(of) of = createNotFolder(of, "negateOF");

  Value* Rvalue = GetOperandValue(src, dest.size);
  Value* Lvalue = GetOperandValue(dest, dest.size);

  Value* result = createSelectFolder(of, Rvalue, Lvalue);

  printvalue(Lvalue) printvalue(Rvalue) printvalue(result)
      SetOperandValue(dest, result);
}

void lifterClass::lift_cmovp() {
  auto dest = operands[0];
  auto src = operands[1];

  Value* pf = getFlag(FLAG_PF);

  Value* Rvalue = GetOperandValue(src, dest.size);
  Value* Lvalue = GetOperandValue(dest, dest.size);
  printvalue(pf) printvalue(Lvalue) printvalue(Rvalue)

      Value* result = createSelectFolder(pf, Rvalue, Lvalue);

  SetOperandValue(dest, result);
}

void lifterClass::lift_cmovnp() {
  auto dest = operands[0];
  auto src = operands[1];

  Value* pf = getFlag(FLAG_PF);

  pf = createNotFolder(pf, "negatePF");

  Value* Rvalue = GetOperandValue(src, dest.size);
  Value* Lvalue = GetOperandValue(dest, dest.size);

  Value* result = createSelectFolder(pf, Rvalue, Lvalue);

  SetOperandValue(dest, result);
}

// for now assume every call is fake
void lifterClass::lift_call() {
  LLVMContext& context = builder.getContext();
  // 0 = function
  // 1 = rip
  // 2 = register rsp
  // 3 = [rsp]
  auto src = operands[0];        // value that we are pushing
  auto rsp = operands[2];        // value that we are pushing
  auto rsp_memory = operands[3]; // value that we are pushing

  auto RspValue = GetOperandValue(rsp, rsp.size);

  auto val = ConstantInt::getSigned(Type::getInt64Ty(context),
                                    BinaryOperations::getBitness() / 8);
  auto result = createSubFolder(RspValue, val, "pushing_newrsp");

  string block_name = "jmp-call";

  uint64_t jump_address = blockInfo.runtime_address;
  switch (src.type) {
  case ZYDIS_OPERAND_TYPE_IMMEDIATE: {
    jump_address += src.imm.value.s;
    break;
  }
  case ZYDIS_OPERAND_TYPE_MEMORY:
  case ZYDIS_OPERAND_TYPE_REGISTER: {
    auto registerValue = GetOperandValue(src, src.size);
    if (!isa<ConstantInt>(registerValue)) {

      auto idltvm =
          builder.CreateIntToPtr(registerValue, PointerType::get(context, 0));

      builder.CreateCall(parseArgsType(nullptr, context), idltvm,
                         parseArgs(nullptr));

      // callFunctionIR(registerValue->getName().str() + "_call_fnc", nullptr);

      SetOperandValue(rsp, RspValue, to_string(blockInfo.runtime_address));
      break;
      // registerValue =
      //    ConstantInt::get(Type::getInt32Ty(context), 0x1337);

      // throw("trying to call an unknown value");
    }
    auto registerCValue = cast<ConstantInt>(registerValue);
    jump_address = registerCValue->getZExtValue();
    break;
  }
  default:
    break;
  }

  SetOperandValue(rsp, result, to_string(blockInfo.runtime_address));
  ; // sub rsp 8 last,

  auto push_into_rsp = GetRegisterValue(ZYDIS_REGISTER_RIP);

  SetOperandValue(rsp_memory, push_into_rsp,
                  to_string(blockInfo.runtime_address));
  ; // sub rsp 8 last,

  auto bb = BasicBlock::Create(context, block_name.c_str(),
                               builder.GetInsertBlock()->getParent());
  // if its trying to jump somewhere else than our binary, call it and
  // continue from [rsp]
  APInt temp;

  builder.CreateBr(bb);

  printvalue2(jump_address);

  blockInfo = BBInfo(jump_address, bb);
  run = 0;
}

int ret_count = 0;
void lifterClass::lift_ret() {
  LLVMContext& context = builder.getContext();
  // [0] = rip
  // [1] = rsp
  // [2] = [rsp]

  // if its ret 0x10
  // then its
  // [0] = 0x10
  // [1] = rip
  // [2] = rsp
  // [3] = [rsp]

  auto rspaddr = operands[2];

  auto rsp = ZYDIS_REGISTER_RSP;
  auto rspvalue = GetRegisterValue(rsp);
  if (operands[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
    rspaddr = operands[3];
  }

  auto realval = GetOperandValue(rspaddr, rspaddr.size);

  auto block = builder.GetInsertBlock();
  block->setName("ret_check" + to_string(ret_count));
  auto function = block->getParent();
  auto lastinst = builder.CreateRet(realval);

  printvalue(rspvalue);
  debugging::doIfDebug([&]() {
    std::string Filename = "output_rets.ll";
    std::error_code EC;
    raw_fd_ostream OS(Filename, EC);
    function->getParent()->print(OS, nullptr);
  });

  uint64_t destination = 0;

  uint8_t rop_result = ROP_return;

  if (llvm::ConstantInt* constInt =
          llvm::dyn_cast<llvm::ConstantInt>(rspvalue)) {
    int64_t rspval = constInt->getSExtValue();
    printvalue2(rspval);
    rop_result = rspval == STACKP_VALUE ? REAL_return : ROP_return;
    /* Edge cases
    if (rspval > STACKP_VALUE) {
      // UNREACHABLE("missing context");
      Function* externFunc = cast<Function>(
          fnc->getParent()
              ->getOrInsertFunction("missing_context", fnc->getReturnType())
              .getCallee());
      builder.CreateRet(builder.CreateCall(externFunc));
      run = 0;
      finished = 1;
      return;
    }
    */
  }
  printvalue2(rop_result);
  if (rop_result == REAL_return) {
    lastinst->eraseFromParent();
    block->setName("real_ret");
    auto rax = GetRegisterValue(ZYDIS_REGISTER_RAX);
    builder.CreateRet(
        createZExtFolder(rax, Type::getInt64Ty(rax->getContext())));
    Function* originalFunc_finalnopt = builder.GetInsertBlock()->getParent();

    debugging::doIfDebug([&]() {
      std::string Filename_finalnopt = "output_finalnoopt.ll";
      std::error_code EC_finalnopt;
      raw_fd_ostream OS_finalnopt(Filename_finalnopt, EC_finalnopt);
      originalFunc_finalnopt->print(OS_finalnopt);
    });
    // function->print(outs());

    debugging::doIfDebug([&]() {
      std::string Filename = "output_finalopt.ll";
      std::error_code EC;
      raw_fd_ostream OS(Filename, EC);
      originalFunc_finalnopt->print(OS);
    });
    run = 0;
    finished = 1;
    printvalue2(finished);
    return;
  }

  block->setName("previousret_block");

  lastinst->eraseFromParent();
  block->setName("fake_ret");

  auto val = ConstantInt::getSigned(Type::getInt64Ty(context),
                                    BinaryOperations::getBitness() / 8);
  auto rsp_result = createAddFolder(
      rspvalue, val,
      "ret-new-rsp-" + to_string(blockInfo.runtime_address) + "-");

  if (operands[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
    rspaddr = operands[3];
    rsp_result =
        createAddFolder(rsp_result, ConstantInt::get(rsp_result->getType(),
                                                     operands[0].imm.value.u));
  }

  SetRegisterValue(rsp, rsp_result); // then add rsp 8

  solvePath(function, destination, realval);
}

int jmpcount = 0;
void lifterClass::lift_jmp() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];

  auto Value = GetOperandValue(dest, BinaryOperations::getBitness());
  auto ripval = GetRegisterValue(ZYDIS_REGISTER_RIP);
  auto newRip = createAddFolder(
      Value, ripval, "jump-xd-" + to_string(blockInfo.runtime_address) + "-");

  jmpcount++;
  auto targetv = GetOperandValue(dest, BinaryOperations::getBitness());
  auto trunc = createZExtOrTruncFolder(targetv, Type::getInt64Ty(context),
                                       "jmp-register");
  printvalue(ripval);
  printvalue(trunc);
  uint64_t destination = 0;
  auto function = builder.GetInsertBlock()->getParent();
  if (dest.type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {

    trunc = createAddFolder(trunc, ripval);
    printvalue(trunc);
  }
  solvePath(function, destination, trunc);
  printvalue2(destination);
  printvalue(newRip);
  SetRegisterValue(ZYDIS_REGISTER_RIP, newRip);
}

int branchnumber = 0;
// jnz and jne
void lifterClass::lift_jnz() {

  auto zf = getFlag(FLAG_ZF);

  // auto dest = operands[0];

  // auto Value = GetOperandValue( dest, 64);
  // auto ripval = GetRegisterValue( ZYDIS_REGISTER_RIP);

  // auto newRip = createAddFolder( Value, ripval, "jnz");

  printvalue(zf);

  branchHelper(zf, "jnz", branchnumber, 1);

  branchnumber++;
}

void lifterClass::lift_js() {

  auto sf = getFlag(FLAG_SF);

  // auto dest = operands[0];

  // auto Value = GetOperandValue( dest, 64);
  // auto ripval = GetRegisterValue( ZYDIS_REGISTER_RIP);

  // auto newRip = createAddFolder( Value, ripval, "js");

  branchHelper(sf, "js", branchnumber);

  branchnumber++;
}
void lifterClass::lift_jns() {

  auto sf = getFlag(FLAG_SF);

  // auto dest = operands[0];

  // auto Value = GetOperandValue( dest, 64);
  // auto ripval = GetRegisterValue( ZYDIS_REGISTER_RIP);

  // auto newRip = createAddFolder( Value, ripval, "jns");

  branchHelper(sf, "jns", branchnumber, 1);

  branchnumber++;
}

void lifterClass::lift_jz() {

  // if 0, then jmp, if not then not jump

  auto zf = getFlag(FLAG_ZF);

  // auto dest = operands[0];

  // auto Value = GetOperandValue( dest, 64);
  // auto ripval = GetRegisterValue( ZYDIS_REGISTER_RIP);

  // auto newRip = createAddFolder( Value, ripval, "jnz");

  branchHelper(zf, "jz", branchnumber);

  branchnumber++;
}

void lifterClass::lift_jle() {
  // If SF != OF or ZF = 1, then jump. Otherwise, do not jump.

  auto sf = getFlag(FLAG_SF);
  auto of = getFlag(FLAG_OF);
  auto zf = getFlag(FLAG_ZF);

  // auto dest = operands[0];
  // auto Value = GetOperandValue( dest, 64);
  // auto ripval = GetRegisterValue( ZYDIS_REGISTER_RIP);
  // auto newRip = createAddFolder( Value, ripval, "jle");

  // Check if SF != OF or ZF is set
  auto sf_neq_of = createXorFolder(sf, of, "jle_SF_NEQ_OF");
  auto condition = createOrFolder(sf_neq_of, zf, "jle_Condition");

  branchHelper(condition, "jle", branchnumber);

  branchnumber++;
}

void lifterClass::lift_jl() {
  auto sf = getFlag(FLAG_SF);
  auto of = getFlag(FLAG_OF);

  // auto dest = operands[0];
  // auto Value = GetOperandValue( dest, 64);
  // auto ripval = GetRegisterValue( ZYDIS_REGISTER_RIP);
  // auto newRip = createAddFolder( Value, ripval, "jl");
  printvalue(sf);
  printvalue(of);
  auto condition = createXorFolder(sf, of, "jl_Condition");

  branchHelper(condition, "jl", branchnumber);

  branchnumber++;
}
void lifterClass::lift_jnl() {
  auto sf = getFlag(FLAG_SF);
  auto of = getFlag(FLAG_OF);

  // auto dest = operands[0];
  // auto Value = GetOperandValue( dest, 64);
  // auto ripval = GetRegisterValue( ZYDIS_REGISTER_RIP);
  // auto newRip = createAddFolder( Value, ripval, "jnl");

  printvalue(sf);
  printvalue(of);

  auto condition = createXorFolder(sf, of, "jl_condition");

  branchHelper(condition, "jnl", branchnumber, 1);

  branchnumber++;
}

void lifterClass::lift_jnle() {
  // If SF != OF or ZF = 1, then jump. Otherwise, do not jump.

  auto sf = getFlag(FLAG_SF);
  auto of = getFlag(FLAG_OF);
  auto zf = getFlag(FLAG_ZF);

  // auto dest = operands[0];
  // auto Value = GetOperandValue( dest, 64);
  // auto ripval = GetRegisterValue( ZYDIS_REGISTER_RIP);
  // auto newRip = createAddFolder( Value, ripval, "jle");

  // Check if SF != OF or ZF is set
  auto sf_neq_of = createXorFolder(sf, of, "jle_SF_NEQ_OF");
  auto condition = createOrFolder(sf_neq_of, zf, "jle_Condition");

  branchHelper(condition, "jnle", branchnumber, 1);

  branchnumber++;
}

void lifterClass::lift_jbe() {

  auto cf = getFlag(FLAG_CF);
  auto zf = getFlag(FLAG_ZF);
  printvalue(cf) printvalue(zf) // auto dest = operands[0];

      // auto Value = GetOperandValue( dest, 64);
      // auto ripval = GetRegisterValue( ZYDIS_REGISTER_RIP);
      // auto newRip = createAddFolder( Value, ripval, "jbe");

      auto condition = createOrFolder(cf, zf, "jbe_Condition");

  branchHelper(condition, "jbe", branchnumber);

  branchnumber++;
}

void lifterClass::lift_jb() {

  auto cf = getFlag(FLAG_CF);
  printvalue(cf);
  // auto dest = operands[0];

  // auto Value = GetOperandValue( dest, 64);
  // auto ripval = GetRegisterValue( ZYDIS_REGISTER_RIP);
  // auto newRip = createAddFolder( Value, ripval, "jb");

  auto condition = cf;
  branchHelper(condition, "jb", branchnumber);

  branchnumber++;
}

void lifterClass::lift_jnb() {

  auto cf = getFlag(FLAG_CF);
  printvalue(cf);
  // auto dest = operands[0];

  // auto Value = GetOperandValue( dest, 64);
  // auto ripval = GetRegisterValue( ZYDIS_REGISTER_RIP);
  // auto newRip = createAddFolder( Value, ripval, "jnb");

  auto condition = cf;
  branchHelper(condition, "jnb", branchnumber, 1);

  branchnumber++;
}
void lifterClass::lift_jnbe() {

  auto cf = getFlag(FLAG_CF);
  auto zf = getFlag(FLAG_ZF);
  printvalue(cf) printvalue(zf) // auto dest = operands[0];

      // auto Value = GetOperandValue( dest, 64);
      // auto ripval = GetRegisterValue( ZYDIS_REGISTER_RIP);
      // auto newRip = createAddFolder( Value, ripval, "jbe");

      auto condition = createOrFolder(cf, zf, "jbe_Condition");

  branchHelper(condition, "jnbe", branchnumber, 1);

  branchnumber++;
}

void lifterClass::lift_jo() {

  auto of = getFlag(FLAG_OF);

  // auto dest = operands[0];

  // auto Value = GetOperandValue( dest, 64);
  // auto ripval = GetRegisterValue( ZYDIS_REGISTER_RIP);

  // auto newRip = createAddFolder( Value, ripval, "jo");

  printvalue(of);
  branchHelper(of, "jo", branchnumber);

  branchnumber++;
}

void lifterClass::lift_jno() {

  auto of = getFlag(FLAG_OF);

  // auto dest = operands[0];

  // auto Value = GetOperandValue( dest, 64);
  // auto ripval = GetRegisterValue( ZYDIS_REGISTER_RIP);

  // auto newRip = createAddFolder( Value, ripval, "jno");

  branchHelper(of, "jno", branchnumber, 1);

  branchnumber++;
}

void lifterClass::lift_jp() {

  auto pf = getFlag(FLAG_PF);
  printvalue(pf);
  // auto dest = operands[0];

  // auto Value = GetOperandValue( dest, 64);
  // auto ripval = GetRegisterValue( ZYDIS_REGISTER_RIP);

  // auto newRip = createAddFolder( Value, ripval, "jp");

  branchHelper(pf, "jp", branchnumber);

  branchnumber++;
}

void lifterClass::lift_jnp() {

  auto pf = getFlag(FLAG_PF);

  // auto dest = operands[0];

  // auto Value = GetOperandValue( dest, 64);
  // auto ripval = GetRegisterValue( ZYDIS_REGISTER_RIP);

  // auto newRip = createAddFolder( Value, ripval, "jnp");

  printvalue(pf);
  branchHelper(pf, "jnp", branchnumber, 1);

  branchnumber++;
}

void lifterClass::lift_sbb() {

  auto dest = operands[0];
  auto src = operands[1];

  Value* Lvalue = GetOperandValue(dest, dest.size);
  Value* Rvalue = GetOperandValue(src, dest.size);
  Value* cf = createZExtOrTruncFolder(getFlag(FLAG_CF), Rvalue->getType());

  Value* tmpResult = createAddFolder(Rvalue, cf, "srcPlusCF");
  Value* result = createSubFolder(Lvalue, tmpResult, "sbbTempResult");
  SetOperandValue(dest, result);

  Value* newCF =
      createICMPFolder(CmpInst::ICMP_ULT, Lvalue, tmpResult, "newCF");
  Value* sf = computeSignFlag(result);
  Value* zf = computeZeroFlag(result);
  Value* pf = computeParityFlag(result);
  Value* af = computeAuxFlag(Lvalue, Rvalue, result);

  auto of = computeOverflowFlagSbb(Lvalue, Rvalue, cf, result);

  setFlag(FLAG_CF, newCF);
  setFlag(FLAG_SF, sf);
  setFlag(FLAG_ZF, zf);
  setFlag(FLAG_PF, pf);
  setFlag(FLAG_AF, af);
  setFlag(FLAG_OF, of);
  printvalue(Lvalue);
  printvalue(Rvalue);
  printvalue(tmpResult);
  printvalue(result);
  printvalue(sf);
  printvalue(of);
}

/*

(* RCL and RCR Instructions *)
SIZE := OperandSize;
CASE (determine count) OF
        SIZE := 8: tempCOUNT := (COUNT AND 1FH) MOD 9;
        SIZE := 16: tempCOUNT := (COUNT AND 1FH) MOD 17;
        SIZE := 32: tempCOUNT := COUNT AND 1FH;
        SIZE := 64: tempCOUNT := COUNT AND 3FH;
ESAC;
IF OperandSize = 64
        THEN COUNTMASK = 3FH;
        ELSE COUNTMASK = 1FH;
FI;
(* RCL Instruction Operation *)
WHILE (tempCOUNT ≠ 0)
        DO
        tempCF := MSB(DEST);
        DEST := (DEST ∗ 2) + CF;
        CF := tempCF;
        tempCOUNT := tempCOUNT – 1;
        OD;
ELIHW;
IF (COUNT & COUNTMASK) = 1
        THEN OF := MSB(DEST) XOR CF;
        ELSE OF is undefined;
FI;
*/

void lifterClass::lift_rcl() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];
  auto count = operands[1];

  auto Lvalue = GetOperandValue(dest, dest.size);
  auto countValue = GetOperandValue(count, dest.size);
  auto carryFlag = getFlag(FLAG_CF);

  unsigned long bitWidth = Lvalue->getType()->getIntegerBitWidth();
  unsigned maskC = bitWidth == 64 ? 0x3f : 0x1f;

  auto actualCount = createAndFolder(
      countValue, ConstantInt::get(countValue->getType(), maskC),
      "actualCount");
  printvalue(actualCount);
  actualCount = bitWidth <= 16
                    ? createURemFolder(
                          actualCount,
                          ConstantInt::get(countValue->getType(), bitWidth + 1),
                          "actualCount2")
                    : actualCount;
  printvalue(actualCount);
  auto wideType = Type::getIntNTy(context, dest.size * 2);
  auto wideLvalue = createZExtFolder(Lvalue, wideType);
  auto cf_extended = createZExtFolder(carryFlag, wideType);
  auto shiftedInCF = cf_extended;

  actualCount = createZExtFolder(actualCount, wideLvalue->getType());
  auto leftShifted = createShlFolder(
      wideLvalue,
      createZExtFolder(actualCount, wideType, "actualCountExtended"),
      "leftshifted");
  auto rightShiftAmount =
      createSubFolder(ConstantInt::get(actualCount->getType(), dest.size),
                      actualCount, "rightshiftamount");
  auto rightShifted = createLShrFolder(
      wideLvalue, createZExtFolder(rightShiftAmount, wideType), "rightshifted");
  auto rotated =
      createOrFolder(leftShifted, createZExtFolder(rightShifted, wideType,
                                                   "rightShiftedExtended"));

  rotated = createLShrFolder(rotated, actualCount);
  rotated = createShlFolder(rotated, actualCount);
  rotated = createOrFolder(rotated, shiftedInCF);

  auto result = createZExtOrTruncFolder(rotated, Lvalue->getType());

  auto newCFBitPosition = ConstantInt::get(rotated->getType(), dest.size);
  auto newCF =
      createZExtOrTruncFolder(createLShrFolder(rotated, newCFBitPosition),
                              Type::getInt1Ty(context), "rclnewcf");

  auto msbAfterRotate =
      createZExtOrTruncFolder(createLShrFolder(result, dest.size),
                              Type::getInt1Ty(context), "rclmsbafterrotate");
  auto isCountOne =
      createICMPFolder(CmpInst::ICMP_EQ, actualCount,
                       ConstantInt::get(actualCount->getType(), 1));
  auto newOF = createZExtOrTruncFolder(createXorFolder(newCF, msbAfterRotate),
                                       Type::getInt1Ty(context));
  newOF = createSelectFolder(isCountOne, newOF, getFlag(FLAG_OF));

  auto isCountZero =
      createICMPFolder(CmpInst::ICMP_EQ, actualCount,
                       ConstantInt::get(actualCount->getType(), 0));
  result = createSelectFolder(isCountZero, Lvalue, result);
  newCF = createSelectFolder(isCountZero, carryFlag, newCF);
  newOF = createSelectFolder(isCountZero, getFlag(FLAG_OF), newOF);
  printvalue(newCFBitPosition);
  printvalue(newCF);
  printvalue(Lvalue) printvalue(countValue) printvalue(carryFlag);
  printvalue(cf_extended) printvalue(shiftedInCF) printvalue(actualCount);
  printvalue(wideLvalue) printvalue(leftShifted) printvalue(rightShifted);
  printvalue(rotated) printvalue(result);

  SetOperandValue(dest, result);
  setFlag(FLAG_CF, newCF);
  setFlag(FLAG_OF, newOF);
}

/*
        (* RCL and RCR Instructions *)
SIZE := OperandSize;
CASE (determine count) OF
        SIZE := 8: tempCOUNT := (COUNT AND 1FH) MOD 9;
        SIZE := 16: tempCOUNT := (COUNT AND 1FH) MOD 17;
        SIZE := 32: tempCOUNT := COUNT AND 1FH;
        SIZE := 64: tempCOUNT := COUNT AND 3FH;
ESAC;
IF OperandSize = 64
        THEN COUNTMASK = 3FH;
        ELSE COUNTMASK = 1FH;
FI;
(* RCR Instruction Operation *)
IF (COUNT & COUNTMASK) = 1
        THEN OF := MSB(DEST) XOR CF;
        ELSE OF is undefined;
FI;
WHILE (tempCOUNT ≠ 0)
        DO
        tempCF := LSB(SRC);
        DEST := (DEST / 2) + (CF * 2SIZE);
        CF := tempCF;
        tempCOUNT := tempCOUNT – 1;
        OD;
ELIHW;

*/
void lifterClass::lift_rcr() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];
  auto count = operands[1];

  auto Lvalue = GetOperandValue(dest, dest.size);
  auto countValue = GetOperandValue(count, dest.size);
  auto carryFlag = getFlag(FLAG_CF);

  unsigned long bitWidth = Lvalue->getType()->getIntegerBitWidth();
  unsigned maskC = bitWidth == 64 ? 0x3f : 0x1f;

  auto actualCount = createAndFolder(
      countValue, ConstantInt::get(countValue->getType(), maskC),
      "actualCount");

  actualCount = bitWidth <= 16
                    ? createURemFolder(
                          actualCount,
                          ConstantInt::get(countValue->getType(), bitWidth + 1),
                          "actualCount2")
                    : actualCount;
  printvalue(Lvalue);
  printvalue(countValue);
  printvalue(carryFlag);
  printvalue(actualCount);
  // Create a double-width value with CF as the highest bit
  auto wideType = Type::getIntNTy(context, dest.size * 2);
  auto wideLvalue = createZExtFolder(Lvalue, wideType);
  auto shiftedCF =
      createShlFolder(createZExtFolder(carryFlag, wideType), bitWidth);
  actualCount = createZExtFolder(actualCount, wideLvalue->getType());
  // Perform the rotation
  auto rightShifted = createLShrFolder(wideLvalue, actualCount);
  auto leftshift_max = ConstantInt::get(actualCount->getType(), bitWidth);
  auto leftshiftct = createSubFolder(leftshift_max, actualCount);
  auto leftShifted = createShlFolder(wideLvalue, leftshiftct);
  auto rotated = createOrFolder(rightShifted, leftShifted);
  auto one = ConstantInt::get(rotated->getType(), 1);
  rotated = createShlFolder(rotated, one);
  rotated = createLShrFolder(rotated, one);
  rotated = createOrFolder(rotated, shiftedCF);
  printvalue(shiftedCF);
  printvalue(wideLvalue);
  printvalue(leftshift_max);
  printvalue(leftshiftct);
  printvalue(rightShifted);
  printvalue(leftShifted);
  printvalue(rotated);
  // Extract result and new CF
  auto result = createTruncFolder(rotated, Lvalue->getType());

  auto newCF = createTruncFolder(createLShrFolder(rotated, bitWidth),
                                 Type::getInt1Ty(context));

  // Calculate new OF (XOR of two most significant bits after rotate when count
  // is 1)
  auto msbAfterRotate = createTruncFolder(createLShrFolder(result, bitWidth),
                                          Type::getInt1Ty(context));
  auto newOF = createXorFolder(newCF, msbAfterRotate);

  // OF is only valid when count is 1, otherwise it's unchanged
  auto isCountOne =
      createICMPFolder(CmpInst::ICMP_EQ, actualCount,
                       ConstantInt::get(actualCount->getType(), 1));
  newOF = createSelectFolder(isCountOne, newOF, getFlag(FLAG_OF));

  // If count is 0, keep original value and flags
  auto isCountZero =
      createICMPFolder(CmpInst::ICMP_EQ, actualCount,
                       ConstantInt::get(actualCount->getType(), 0));
  result = createSelectFolder(isCountZero, Lvalue, result);
  newCF = createSelectFolder(isCountZero, carryFlag, newCF);
  newOF = createSelectFolder(isCountZero, getFlag(FLAG_OF), newOF);
  printvalue(result);

  SetOperandValue(dest, result);
  setFlag(FLAG_CF, newCF);
  setFlag(FLAG_OF, newOF);
}
void lifterClass::lift_not() {

  auto dest = operands[0];

  auto Rvalue = GetOperandValue(dest, dest.size);
  Rvalue =
      createXorFolder(Rvalue, Constant::getAllOnesValue(Rvalue->getType()),
                      "realnot-" + to_string(blockInfo.runtime_address) + "-");
  SetOperandValue(dest, Rvalue, to_string(blockInfo.runtime_address));

  printvalue(Rvalue);
  //  Flags Affected
  // None
}

void lifterClass::lift_neg() {

  auto dest = operands[0];
  auto Rvalue = GetOperandValue(dest, dest.size);

  auto cf = createICMPFolder(CmpInst::ICMP_NE, Rvalue,
                             ConstantInt::get(Rvalue->getType(), 0), "cf");
  auto result = createSubFolder(
      builder.getIntN(Rvalue->getType()->getIntegerBitWidth(), 0), Rvalue,
      "neg");
  SetOperandValue(dest, result);

  auto sf = computeSignFlag(result);
  auto zf = computeZeroFlag(result);
  auto pf = computeParityFlag(result);
  Value* fifteen = ConstantInt::get(Rvalue->getType(), 0xf);
  auto af = createICMPFolder(CmpInst::ICMP_NE, createAndFolder(Rvalue, fifteen),
                             ConstantInt::get(Rvalue->getType(), 0), "af");
  auto isZero = createICMPFolder(
      CmpInst::ICMP_NE, Rvalue, ConstantInt::get(Rvalue->getType(), 0), "zero");

  printvalue(Rvalue) printvalue(result) printvalue(sf);
  // if of is not 0 and input and output is equal, of is set (input is just sign
  // bit)

  Value* of;

  of = createICMPFolder(CmpInst::ICMP_EQ, result, Rvalue);
  of = createSelectFolder(isZero, of, ConstantInt::get(of->getType(), 0));

  printvalue(of);
  // The CF flag set to 0 if the source operand is 0; otherwise it is set
  // to 1. The OF, SF, ZF, AF, and PF flags are set according to the
  // result.
  setFlag(FLAG_CF, cf);
  setFlag(FLAG_SF, sf);
  setFlag(FLAG_ZF, zf);
  setFlag(FLAG_PF, pf);
  setFlag(FLAG_OF, of);
  setFlag(FLAG_AF, af);
}

/*

IF 64-Bit Mode and using REX.W
        THEN
                countMASK := 3FH;
        ELSE
                countMASK := 1FH;
FI
tempCOUNT := (COUNT AND countMASK);
tempDEST := DEST;
WHILE (tempCOUNT ≠ 0)
DO
        IF instruction is SAL or SHL
                THEN
                CF := MSB(DEST);
        ELSE (* Instruction is SAR or SHR *)
                CF := LSB(DEST);
        FI;
        IF instruction is SAL or SHL
                THEN
                        DEST := DEST ∗ 2;
        ELSE
                IF instruction is SAR
                        THEN
                                DEST := DEST / 2; (* Signed divide, rounding
toward negative infinity *) ELSE (* Instruction is SHR *) DEST := DEST / 2 ;
(* Unsigned divide *) FI; FI; tempCOUNT := tempCOUNT – 1; OD;

(* Determine overflow for the various instructions *)
IF (COUNT and countMASK) = 1
        THEN
        IF instruction is SAL or SHL
                THEN
                OF := MSB(DEST) XOR CF;
        ELSE
        IF instruction is SAR
                THEN
                OF := 0;
        ELSE (* Instruction is SHR *)
                OF := MSB(tempDEST);
        FI;
FI;

ELSE IF (COUNT AND countMASK) = 0
        THEN
        All flags unchanged;
ELSE (* COUNT not 1 or 0 *)
OF := undefined;
FI;
FI;

*/
// maybe

void lifterClass::lift_sar() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0 + (instruction.mnemonic == ZYDIS_MNEMONIC_SARX)];
  auto count = operands[1 + (instruction.mnemonic == ZYDIS_MNEMONIC_SARX)];

  Value* Lvalue = GetOperandValue(dest, dest.size);
  Value* countValue = GetOperandValue(count, dest.size);

  unsigned bitWidth = Lvalue->getType()->getIntegerBitWidth();
  unsigned maskC = bitWidth == 64 ? 0x3f : 0x1f;

  Value* clampedCount = createAndFolder(
      countValue, ConstantInt::get(countValue->getType(), maskC), "sarclamp");

  Value* zero = ConstantInt::get(clampedCount->getType(), 0);
  Value* maxShift = ConstantInt::get(clampedCount->getType(), bitWidth - 1);

  Value* isZeroed = createICMPFolder(CmpInst::ICMP_UGT, clampedCount, maxShift);

  // Shift by bitWidth - 1 if clampedCount exceeds bitWidth - 1
  clampedCount = createSelectFolder(isZeroed, maxShift, clampedCount);
  Value* result = createAShrFolder(
      Lvalue, clampedCount,
      "sar-ashr-" + std::to_string(blockInfo.runtime_address) + "-");

  auto last_shift = createAShrFolder(
      Lvalue,
      createSubFolder(clampedCount,
                      ConstantInt::get(clampedCount->getType(), 1)),
      "sarcf");
  Value* cfValue = createTruncFolder(last_shift, builder.getInt1Ty());

  Value* isCountZero =
      createICMPFolder(CmpInst::ICMP_EQ, clampedCount,
                       ConstantInt::get(clampedCount->getType(), 0));
  Value* oldcf = getFlag(FLAG_CF);
  printvalue(last_shift);
  printvalue(isCountZero);
  cfValue = createSelectFolder(isCountZero, oldcf, cfValue, "cfValue");

  // OF is cleared for SAR
  Value* of = ConstantInt::get(Type::getInt1Ty(context), 0);

  // Update flags only when count is not zero
  LazyValue isNotZero([this, clampedCount, zero]() {
    return createICMPFolder(CmpInst::ICMP_NE, clampedCount, zero);
  });
  LazyValue oldsf = getLazyFlag(FLAG_SF);
  LazyValue oldzf = getLazyFlag(FLAG_PF);
  LazyValue oldpf = getLazyFlag(FLAG_ZF);
  if (instruction.mnemonic != ZYDIS_MNEMONIC_SARX) {
    setFlag(FLAG_SF, [this, isNotZero, oldsf, result]() {
      return createSelectFolder(isNotZero.get(), computeSignFlag(result),
                                oldsf.get());
    });
    setFlag(FLAG_ZF, [this, isNotZero, oldzf, result]() {
      return createSelectFolder(isNotZero.get(), computeZeroFlag(result),
                                oldzf.get());
    });
    setFlag(FLAG_PF, [this, isNotZero, oldpf, result]() {
      return createSelectFolder(isNotZero.get(), computeParityFlag(result),
                                oldpf.get());
    });

    setFlag(FLAG_CF, cfValue);
    setFlag(FLAG_OF, of);
  }

  SetOperandValue(dest, result, std::to_string(blockInfo.runtime_address));
}

// TODO fix
void lifterClass::lift_shr() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0 + (instruction.mnemonic == ZYDIS_MNEMONIC_SHRX)];
  auto count = operands[1 + (instruction.mnemonic == ZYDIS_MNEMONIC_SHRX)];

  Value* Lvalue = GetOperandValue(dest, dest.size);
  Value* countValue = GetOperandValue(count, dest.size);

  unsigned bitWidth = Lvalue->getType()->getIntegerBitWidth();
  unsigned maskC = bitWidth == 64 ? 0x3f : 0x1f;

  Value* clampedCount = createAndFolder(
      countValue, ConstantInt::get(countValue->getType(), maskC), "shrclamp");

  Value* result = createLShrFolder(
      Lvalue, clampedCount,
      "shr-lshr-" + to_string(blockInfo.runtime_address) + "-");
  Value* zero = ConstantInt::get(countValue->getType(), 0);
  Value* isZeroed =
      createICMPFolder(CmpInst::ICMP_UGT, clampedCount,
                       ConstantInt::get(clampedCount->getType(), bitWidth - 1));
  result = createSelectFolder(isZeroed, zero, result, "shiftValue");

  Value* cfValue = createTruncFolder(
      createLShrFolder(
          Lvalue,
          createSubFolder(clampedCount,
                          ConstantInt::get(clampedCount->getType(), 1)),
          "shrcf"),
      builder.getInt1Ty());

  Value* isCountOne =
      createICMPFolder(CmpInst::ICMP_EQ, clampedCount,
                       ConstantInt::get(clampedCount->getType(), 1));
  Value* of = createICMPFolder(CmpInst::ICMP_SLT, Lvalue,
                               ConstantInt::get(Lvalue->getType(), 0));
  of = createSelectFolder(isCountOne, of, getFlag(FLAG_OF), "of");

  Value* isNotZero = createICMPFolder(CmpInst::ICMP_NE, clampedCount, zero);
  Value* oldcf = getFlag(FLAG_CF);
  cfValue = createSelectFolder(isNotZero, cfValue, oldcf, "cfValue1");
  cfValue = createSelectFolder(
      isZeroed, createTruncFolder(zero, Type::getInt1Ty(context)), cfValue,
      "cfValue2");
  Value* sf =
      createSelectFolder(isNotZero, computeSignFlag(result), getFlag(FLAG_SF));
  Value* zf =
      createSelectFolder(isNotZero, computeZeroFlag(result), getFlag(FLAG_ZF));
  Value* pf = createSelectFolder(isNotZero, computeParityFlag(result),
                                 getFlag(FLAG_PF));
  printvalue(sf);
  printvalue(result);

  if (instruction.mnemonic != ZYDIS_MNEMONIC_SHRX) {
    setFlag(FLAG_CF, cfValue);
    setFlag(FLAG_OF, of);
    setFlag(FLAG_SF, sf);
    setFlag(FLAG_ZF, zf);
    setFlag(FLAG_PF, pf);
  }
  printvalue(Lvalue) printvalue(clampedCount) printvalue(result) printvalue(
      isNotZero) printvalue(oldcf) printvalue(cfValue)
      SetOperandValue(dest, result, std::to_string(blockInfo.runtime_address));
}

void lifterClass::lift_shl() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0 + (instruction.mnemonic == ZYDIS_MNEMONIC_SHLX)];
  auto count = operands[1 + (instruction.mnemonic == ZYDIS_MNEMONIC_SHLX)];
  Value* Lvalue = GetOperandValue(dest, dest.size,
                                  std::to_string(blockInfo.runtime_address));
  Value* countValue = GetOperandValue(count, dest.size);
  unsigned bitWidth = Lvalue->getType()->getIntegerBitWidth();
  unsigned maskC = bitWidth == 64 ? 0x3f : 0x1f;

  auto bitWidthValue = ConstantInt::get(countValue->getType(), bitWidth);

  Value* clampedCountValue = createAndFolder(
      countValue, ConstantInt::get(countValue->getType(), maskC), "shlclamp");
  printvalue(clampedCountValue);

  Value* result = createShlFolder(Lvalue, clampedCountValue, "shl-shift");
  Value* zero = ConstantInt::get(countValue->getType(), 0);
  Value* isZeroed = createICMPFolder(
      CmpInst::ICMP_UGT, clampedCountValue,
      ConstantInt::get(clampedCountValue->getType(), bitWidth - 1));
  result = createSelectFolder(isZeroed, zero, result);

  Value* cfValue = createLShrFolder(
      Lvalue, createSubFolder(bitWidthValue, clampedCountValue), "shlcf");
  Value* one = ConstantInt::get(cfValue->getType(), 1);
  cfValue = createAndFolder(cfValue, one, "shlcf");
  cfValue = createZExtOrTruncFolder(cfValue, Type::getInt1Ty(context));

  auto countIsNotZero =
      createICMPFolder(CmpInst::ICMP_NE, clampedCountValue,
                       ConstantInt::get(clampedCountValue->getType(), 0));

  auto cfRvalue = createSubFolder(
      clampedCountValue, ConstantInt::get(clampedCountValue->getType(), 1));
  auto cfShl = createShlFolder(Lvalue, cfRvalue);
  auto cfIntT = cast<IntegerType>(cfShl->getType());
  auto cfRightCount = ConstantInt::get(cfIntT, cfIntT->getBitWidth() - 1);
  auto cfLow = createLShrFolder(cfShl, cfRightCount, "lowcfshr");
  cfValue = createSelectFolder(
      countIsNotZero, createZExtOrTruncFolder(cfLow, Type::getInt1Ty(context)),
      getFlag(FLAG_CF));
  cfValue = createSelectFolder(
      isZeroed, createZExtOrTruncFolder(zero, Type::getInt1Ty(context)),
      cfValue);

  Value* isCountOne =
      createICMPFolder(CmpInst::ICMP_EQ, clampedCountValue,
                       ConstantInt::get(clampedCountValue->getType(), 1));

  Value* originalMSB = createLShrFolder(
      Lvalue, ConstantInt::get(Lvalue->getType(), bitWidth - 1), "shlmsb");
  originalMSB = createAndFolder(
      originalMSB, ConstantInt::get(Lvalue->getType(), 1), "shlmsb");
  originalMSB = createZExtOrTruncFolder(originalMSB, Type::getInt1Ty(context));

  Value* cfAsMSB = createZExtOrTruncFolder(
      createLShrFolder(Lvalue,
                       ConstantInt::get(Lvalue->getType(), bitWidth - 1),
                       "shlcfasmsb"),
      Type::getInt1Ty(context));

  Value* resultMSB = createZExtOrTruncFolder(
      createLShrFolder(result,
                       ConstantInt::get(result->getType(), bitWidth - 1),
                       "shlresultmsb"),
      Type::getInt1Ty(context));

  Value* ofValue = createSelectFolder(
      isCountOne, createXorFolder(resultMSB, cfAsMSB), getFlag(FLAG_OF));

  if (instruction.mnemonic != ZYDIS_MNEMONIC_SHRX) {
    setFlag(FLAG_CF, cfValue);
    setFlag(FLAG_OF, ofValue);

    Value* sf = createSelectFolder(countIsNotZero, computeSignFlag(result),
                                   getFlag(FLAG_SF));
    Value* oldpf = getFlag(FLAG_PF);
    printvalue(Lvalue);
    printvalue(countValue);
    printvalue(clampedCountValue);
    printvalue(isCountOne);
    printvalue(result);
    printvalue(ofValue);
    printvalue(cfValue);

    setFlag(FLAG_SF, sf);
    auto oldZF = getLazyFlag(FLAG_ZF);
    setFlag(FLAG_ZF, [this, countIsNotZero, result, oldZF]() mutable {
      return createSelectFolder(countIsNotZero, computeZeroFlag(result),
                                oldZF.get());
    });
    setFlag(FLAG_PF, [this, result, oldpf, countIsNotZero]() {
      return createSelectFolder(countIsNotZero, computeParityFlag(result),
                                oldpf);
    });
  }
  SetOperandValue(dest, result, std::to_string(blockInfo.runtime_address));
}

void lifterClass::lift_bswap() {
  auto dest = operands[0];

  auto Lvalue = GetOperandValue(dest, dest.size);
  // if 16bit, 0 it
  if (dest.size == 16) {
    Value* zero = ConstantInt::get(Lvalue->getType(), 0);
    SetOperandValue(dest, zero);
    return;
  }
  Value* newswappedvalue = ConstantInt::get(Lvalue->getType(), 0);
  Value* mask = ConstantInt::get(Lvalue->getType(), 0xff);
  for (unsigned i = 0; i < Lvalue->getType()->getIntegerBitWidth() / 8; i++) {
    // 0xff
    // b = a & 0xff >> 0
    // b = 0x78
    // nb |=  b << 24
    // nb |= 0x78000000
    // 0xff00
    // b = a & 0xff00 >> 8
    // b = 0x56
    // nb |= b << 16
    // nb = 0x78560000
    auto byte =
        createLShrFolder(createAndFolder(Lvalue, mask), i * 8, "shlresultmsb");
    auto shiftby = Lvalue->getType()->getIntegerBitWidth() - (i + 1) * 8;
    auto newposbyte = createShlFolder(byte, shiftby);
    newswappedvalue = createOrFolder(newswappedvalue, newposbyte);
    mask = createShlFolder(mask, 8);
  }

  SetOperandValue(dest, newswappedvalue);
}

void lifterClass::lift_cmpxchg() {

  auto dest = operands[0];
  auto src = operands[1];
  auto accop = operands[2];

  auto Rvalue = GetOperandValue(src, src.size);
  auto Lvalue = GetOperandValue(dest, dest.size);
  auto accum = GetOperandValue(accop, dest.size);

  auto sub = createSubFolder(accum, Lvalue);

  auto of = computeOverflowFlagSub(Lvalue, Rvalue, sub);

  auto lowerNibbleMask = ConstantInt::get(Lvalue->getType(), 0xF);
  auto RvalueLowerNibble =
      createAndFolder(Lvalue, lowerNibbleMask, "lvalLowerNibble");
  auto op2LowerNibble =
      createAndFolder(Rvalue, lowerNibbleMask, "rvalLowerNibble");

  auto cf = createICMPFolder(CmpInst::ICMP_UGT, Rvalue, Lvalue, "add_cf");
  auto af = createICMPFolder(CmpInst::ICMP_ULT, RvalueLowerNibble,
                             op2LowerNibble, "add_af");

  auto sf = computeSignFlag(sub);

  /*
  TEMP := DEST
  IF accumulator = TEMP
          THEN
                  ZF := 1;
                  DEST := SRC;
          ELSE
                  ZF := 0;
                  accumulator := TEMP;
                  DEST := TEMP;
  FI;
  */
  auto zf = createICMPFolder(CmpInst::ICMP_EQ, accum, Lvalue);
  // if zf dest = src
  auto result = createSelectFolder(zf, Rvalue, Lvalue);

  SetOperandValue(dest, result);
  setFlag(FLAG_OF, of);
  setFlag(FLAG_CF, cf);
  setFlag(FLAG_AF, af);
  setFlag(FLAG_SF, sf);
  setFlag(FLAG_ZF, zf);
}

void lifterClass::lift_xchg() {

  auto dest = operands[0];
  auto src = operands[1];

  auto Rvalue = GetOperandValue(src, src.size);
  auto Lvalue = GetOperandValue(dest, dest.size);

  printvalue(Lvalue) printvalue(Rvalue);

  SetOperandValue(dest, Rvalue, to_string(blockInfo.runtime_address));
  ;
  SetOperandValue(src, Lvalue);
}

void lifterClass::lift_popcnt() {
  auto dest = operands[0]; // count
  auto src = operands[1];  // src

  auto zero = builder.getIntN(dest.size, 0);
  auto one = builder.getIntN(dest.size, 1);

  auto destsize = builder.getIntN(dest.size, dest.size + 1);

  auto srcV = GetOperandValue(src, src.size);
  printvalue(srcV); // if src is 0, count 0

  // create intrinsic for popct
  auto popcnt = Intrinsic::getDeclaration(builder.GetInsertBlock()->getModule(),
                                          Intrinsic::ctpop, srcV->getType());
  auto popcntV = builder.CreateCall(popcnt, {srcV});
  auto destV = simplifyValue(
      popcntV,
      builder.GetInsertBlock()->getParent()->getParent()->getDataLayout());
  printvalue(destV);

  setFlag(FLAG_OF, builder.getInt1(0));

  setFlag(FLAG_SF, builder.getInt1(0));

  setFlag(FLAG_ZF, computeZeroFlag(destV));

  setFlag(FLAG_AF, builder.getInt1(0));

  setFlag(FLAG_CF, builder.getInt1(0));

  setFlag(FLAG_PF, builder.getInt1(0));

  SetOperandValue(dest, destV);
}

void lifterClass::lift_shld() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];
  auto source = operands[1];
  auto count = operands[2];

  auto Lvalue = GetOperandValue(dest, dest.size);
  auto sourceValue = GetOperandValue(source, dest.size);
  auto countValue = GetOperandValue(count, dest.size);

  unsigned bitWidth = Lvalue->getType()->getIntegerBitWidth();
  auto effectiveCountValue = createURemFolder(
      countValue, ConstantInt::get(countValue->getType(), bitWidth),
      "effectiveShiftCount");

  auto shiftedDest =
      createShlFolder(Lvalue, effectiveCountValue, "shiftedDest");
  auto complementCount =
      createSubFolder(ConstantInt::get(countValue->getType(), bitWidth),
                      effectiveCountValue, "complementCount");
  auto shiftedSource =
      createLShrFolder(sourceValue, complementCount, "shiftedSource");
  auto resultValue = createOrFolder(shiftedDest, shiftedSource, "shldResult");

  auto countIsNotZero =
      createICMPFolder(CmpInst::ICMP_NE, effectiveCountValue,
                       ConstantInt::get(effectiveCountValue->getType(), 0));
  auto lastShiftedBitPosition = createSubFolder(
      effectiveCountValue, ConstantInt::get(effectiveCountValue->getType(), 1));
  auto lastShiftedBit =
      createAndFolder(createLShrFolder(Lvalue, lastShiftedBitPosition),
                      ConstantInt::get(Lvalue->getType(), 1), "shldresultmsb");
  auto cf = createSelectFolder(
      countIsNotZero,
      createZExtOrTruncFolder(lastShiftedBit, Type::getInt1Ty(context)),
      getFlag(FLAG_CF));
  resultValue = createSelectFolder(countIsNotZero, resultValue, Lvalue);

  auto isOne =
      createICMPFolder(CmpInst::ICMP_EQ, effectiveCountValue,
                       ConstantInt::get(effectiveCountValue->getType(), 1));
  auto newOF = createXorFolder(
      createLShrFolder(
          Lvalue, ConstantInt::get(Lvalue->getType(), bitWidth - 1), "subof"),
      createLShrFolder(resultValue,
                       ConstantInt::get(resultValue->getType(), bitWidth - 1),
                       "subof2"),
      "subxorof");
  auto of = createSelectFolder(
      isOne, createZExtOrTruncFolder(newOF, Type::getInt1Ty(context)),
      getFlag(FLAG_OF));

  //  CF := BIT[DEST, SIZE – COUNT]; if shifted,
  setFlag(FLAG_CF, cf);
  setFlag(FLAG_OF, of);

  setFlag(FLAG_SF, computeSignFlag(resultValue));
  setFlag(FLAG_ZF, computeZeroFlag(resultValue));
  setFlag(FLAG_PF, computeParityFlag(resultValue));

  SetOperandValue(dest, resultValue, std::to_string(blockInfo.runtime_address));
}

void lifterClass::lift_shrd() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];
  auto source = operands[1];
  auto count = operands[2];

  auto Lvalue = GetOperandValue(dest, dest.size);
  auto sourceValue = GetOperandValue(source, dest.size);
  auto countValue = GetOperandValue(count, dest.size);

  unsigned bitWidth = Lvalue->getType()->getIntegerBitWidth();
  auto effectiveCountValue = createURemFolder(
      countValue, ConstantInt::get(countValue->getType(), bitWidth),
      "effectiveShiftCount");

  auto shiftedDest =
      createLShrFolder(Lvalue, effectiveCountValue, "shiftedDest");
  auto complementCount =
      createSubFolder(ConstantInt::get(countValue->getType(), bitWidth),
                      effectiveCountValue, "complementCount");
  auto shiftedSource =
      createShlFolder(sourceValue, complementCount, "shiftedSource");
  auto resultValue = createOrFolder(shiftedDest, shiftedSource, "shrdResult");

  // Calculate CF
  auto cfBitPosition = createSubFolder(
      effectiveCountValue, ConstantInt::get(effectiveCountValue->getType(), 1));
  Value* cf = createLShrFolder(Lvalue, cfBitPosition);
  cf = createAndFolder(cf, ConstantInt::get(cf->getType(), 1), "shrdcf");
  cf = createZExtOrTruncFolder(cf, Type::getInt1Ty(context));

  // Calculate OF, only when count is 1
  Value* isCountOne =
      createICMPFolder(CmpInst::ICMP_EQ, effectiveCountValue,
                       ConstantInt::get(effectiveCountValue->getType(), 1));
  Value* mostSignificantBitOfDest = createLShrFolder(
      Lvalue, ConstantInt::get(Lvalue->getType(), bitWidth - 1), "shlmsbdest");
  mostSignificantBitOfDest = createAndFolder(
      mostSignificantBitOfDest,
      ConstantInt::get(mostSignificantBitOfDest->getType(), 1), "shrdmsb");
  Value* mostSignificantBitOfResult = createLShrFolder(
      resultValue, ConstantInt::get(resultValue->getType(), bitWidth - 1),
      "shlmsbresult");
  mostSignificantBitOfResult = createAndFolder(
      mostSignificantBitOfResult,
      ConstantInt::get(mostSignificantBitOfResult->getType(), 1), "shrdmsb2");
  Value* of =
      createXorFolder(mostSignificantBitOfDest, mostSignificantBitOfResult);
  of = createZExtOrTruncFolder(of, Type::getInt1Ty(context));
  of = createSelectFolder(isCountOne, of, ConstantInt::getFalse(context));
  of = createZExtFolder(of, Type::getInt1Ty(context));

  setFlag(FLAG_CF, cf);
  setFlag(FLAG_OF, of);

  setFlag(FLAG_SF, computeSignFlag(resultValue));
  setFlag(FLAG_ZF, computeZeroFlag(resultValue));
  setFlag(FLAG_PF, computeParityFlag(resultValue));

  SetOperandValue(dest, resultValue, std::to_string(blockInfo.runtime_address));
}

void lifterClass::lift_lea() {

  auto dest = operands[0];
  auto src = operands[1];

  auto Rvalue = GetEffectiveAddress(src, dest.size);

  printvalue(Rvalue)

      SetOperandValue(dest, Rvalue, to_string(blockInfo.runtime_address));
  ;
}

// extract sub from this function, this is convoluted for no reason
void lifterClass::lift_add_sub() {
  auto dest = operands[0];
  auto src = operands[1];

  auto Rvalue = GetOperandValue(src, dest.size);
  auto Lvalue = GetOperandValue(dest, dest.size);

  Value* result = nullptr;

  switch (instruction.mnemonic) {
  case ZYDIS_MNEMONIC_ADD: {
    result = createAddFolder(Lvalue, Rvalue,
                             "realadd-" + to_string(blockInfo.runtime_address) +
                                 "-");

    setFlag(FLAG_AF, [this, result, Lvalue, Rvalue]() {
      auto lowerNibbleMask = ConstantInt::get(Lvalue->getType(), 0xF);
      auto RvalueLowerNibble =
          createAndFolder(Lvalue, lowerNibbleMask, "lvalLowerNibble");
      auto op2LowerNibble =
          createAndFolder(Rvalue, lowerNibbleMask, "rvalLowerNibble");
      auto sumLowerNibble = createAddFolder(RvalueLowerNibble, op2LowerNibble,
                                            "add_sumLowerNibble");
      return createICMPFolder(CmpInst::ICMP_UGT, sumLowerNibble,
                              lowerNibbleMask, "add_af");
    });
    setFlag(FLAG_CF, [this, result, Lvalue, Rvalue]() {
      return createOrFolder(
          createICMPFolder(CmpInst::ICMP_ULT, result, Lvalue, "add_cf1"),
          createICMPFolder(CmpInst::ICMP_ULT, result, Rvalue, "add_cf2"),
          "add_cf");
    });
    setFlag(FLAG_OF, [this, result, Lvalue, Rvalue]() {
      return computeOverflowFlagAdd(Lvalue, Rvalue, result);
    });
    break;
  }
  case ZYDIS_MNEMONIC_SUB: {
    result = createSubFolder(Lvalue, Rvalue,
                             "realsub-" + to_string(blockInfo.runtime_address) +
                                 "-");

    setFlag(FLAG_AF, [this, result, Lvalue, Rvalue]() {
      auto lowerNibbleMask = ConstantInt::get(Lvalue->getType(), 0xF);
      auto RvalueLowerNibble =
          createAndFolder(Lvalue, lowerNibbleMask, "lvalLowerNibble");
      auto op2LowerNibble =
          createAndFolder(Rvalue, lowerNibbleMask, "rvalLowerNibble");
      return createICMPFolder(CmpInst::ICMP_ULT, RvalueLowerNibble,
                              op2LowerNibble, "sub_af");
    });

    setFlag(FLAG_CF, [this, result, Lvalue, Rvalue]() {
      return createICMPFolder(CmpInst::ICMP_UGT, Rvalue, Lvalue, "add_cf");
    });

    setFlag(FLAG_OF, [this, result, Lvalue, Rvalue]() {
      return computeOverflowFlagSub(Lvalue, Rvalue, result);
    });
    break;
  }
  default:
    break;
  }

  /*
  Flags Affected
  The OF, SF, ZF, AF, CF, and PF flags are set according to the result.
  */

  setFlag(FLAG_SF, [this, result]() { return computeSignFlag(result); });

  setFlag(FLAG_ZF, [this, result]() { return computeZeroFlag(result); });

  setFlag(FLAG_PF, [this, result]() { return computeParityFlag(result); });

  printvalue(Lvalue);
  printvalue(Rvalue);
  printvalue(result);

  SetOperandValue(dest, result);
}

void lifterClass::lift_imul2(bool isSigned) {
  LLVMContext& context = builder.getContext();
  auto src = operands[0];
  auto Rvalue = GetRegisterValue(ZYDIS_REGISTER_AL);

  Value* Lvalue = GetOperandValue(src, src.size);
  if (isSigned) { // do this in a prettier way
    Lvalue = createSExtFolder(Lvalue, Type::getIntNTy(context, src.size * 2));

    Rvalue = createSExtOrTruncFolder(
        Rvalue, Type::getIntNTy(context,
                                src.size)); // make sure the size is correct,
                                            // 1 byte, GetRegisterValue doesnt
                                            // ensure we have the correct size
    Rvalue = createSExtOrTruncFolder(Rvalue, Lvalue->getType());
  } else {
    Lvalue = createZExtFolder(Lvalue, Type::getIntNTy(context, src.size * 2));

    Rvalue = createZExtOrTruncFolder(
        Rvalue, Type::getIntNTy(context,
                                src.size)); // make sure the size is correct, 1
                                            // byte, GetRegisterValue doesnt
                                            // ensure we have the correct size
    Rvalue = createZExtOrTruncFolder(Rvalue, Lvalue->getType());
  }
  Value* result = createMulFolder(Rvalue, Lvalue);
  Value* lowerresult = createTruncFolder(
      result, Type::getIntNTy(context, src.size), "lowerResult");
  Value* of;
  Value* cf;
  if (isSigned) {
    of = createICMPFolder(CmpInst::ICMP_NE, result,
                          createSExtFolder(lowerresult, result->getType()));
    cf = of;
  } else {
    Value* highPart = createLShrFolder(result, src.size, "highPart");
    Value* highPartTruncated = createTruncFolder(
        highPart, Type::getIntNTy(context, src.size), "truncatedHighPart");
    cf = createICMPFolder(CmpInst::ICMP_NE, highPartTruncated,
                          ConstantInt::get(result->getType(), 0), "cf");
    of = cf;
  }

  setFlag(FLAG_CF, cf);
  setFlag(FLAG_OF, of);
  printvalue(cf);
  printvalue(of);
  SetRegisterValue(ZYDIS_REGISTER_AX, result);
  printvalue(Lvalue);
  printvalue(Rvalue);
  printvalue(result);
  // if imul modify cf and of flags
  // if not, dont do anything else
}

// TODO rewrite this
void lifterClass::lift_imul() {
  LLVMContext& context = builder.getContext();

  auto dest = operands[0]; // dest ?
  if (dest.size == 8 && instruction.operand_count_visible == 1) {
    lift_imul2(1);
    return;
  }
  auto src = operands[1];
  auto src2 = (instruction.operand_count_visible == 3)
                  ? operands[2]
                  : dest; // if exists third operand

  Value* Lvalue = GetOperandValue(src, src.size);
  Value* Rvalue = GetOperandValue(src2, src2.size);
  uint8_t initialSize = src.size;
  printvalue2(initialSize);
  printvalue(Rvalue);
  printvalue(Lvalue);
  Rvalue = createSExtFolder(Rvalue, Type::getIntNTy(context, initialSize * 2));
  Lvalue = createSExtFolder(Lvalue, Type::getIntNTy(context, initialSize * 2));

  Value* result = createMulFolder(Lvalue, Rvalue, "intmul");

  // Flags

  Value* highPart = createLShrFolder(result, initialSize, "highPart");
  Value* highPartTruncated = createTruncFolder(
      highPart, Type::getIntNTy(context, initialSize), "truncatedHighPart");

  /*
  For the one operand form of the instruction, the CF and OF flags are set
  when significant bits are carried into the upper half of the result and
  cleared when the result fits exactly in the lower half of the result.
  For the two- and three-operand forms of the instruction, the CF and OF
  flags are set when the result must be truncated to fit in the
  destination operand size and cleared when the result fits exactly in the
  destination operand size. The SF, ZF, AF, and PF flags are undefined.
  */

  /*
  DEST := TruncateToOperandSize(TMP_XP);
  IF SignExtend(DEST) ≠ TMP_XP
  THEN CF := 1; OF := 1;
          ELSE CF := 0; OF := 0; FI;
  */

  Value* truncresult = createTruncFolder(
      result, Type::getIntNTy(context, initialSize), "truncRes");
  printvalue(truncresult);
  Value* cf =
      createICMPFolder(CmpInst::ICMP_NE, result,
                       createSExtFolder(truncresult, result->getType()), "cf");
  Value* of = cf;

  if (instruction.operand_count_visible == 3) {
    SetOperandValue(dest, truncresult);
  } else if (instruction.operand_count_visible == 2) {
    SetOperandValue(operands[0], truncresult);
  } else { // For one operand, result goes into ?dx:?ax if not a byte
           // operation
    auto splitResult = createTruncFolder(
        result, Type::getIntNTy(context, initialSize), "splitResult");
    Value* SEsplitResult = createSExtFolder(splitResult, result->getType());
    printvalue(splitResult);
    printvalue(result);
    cf = createICMPFolder(CmpInst::ICMP_NE, SEsplitResult, result);
    of = cf;
    printvalue(of);
    printvalue(result);
    printvalue(SEsplitResult);

    if (initialSize == 8) {
      SetOperandValue(operands[1], result);
    } else {

      SetOperandValue(operands[1], splitResult);
      SetOperandValue(operands[2], highPartTruncated);
    }
  }

  setFlag(FLAG_CF, cf);
  setFlag(FLAG_OF, of);
  printvalue(Lvalue) printvalue(Rvalue) printvalue(result);
  printvalue(highPartTruncated) printvalue(of) printvalue(cf);
}
// rewrite this too
void lifterClass::lift_mul() {
  /*
  mul rdx
  [0] rdx
  [1] rax
  [2] rdx
  [3] flags
  */
  /*
  IF (Byte operation)
          THEN
                  AX := AL ∗ SRC;
          ELSE (* Word or doubleword operation *)
                  IF OperandSize = 16
                          THEN
                                  DX:AX := AX ∗ SRC;
                          ELSE IF OperandSize = 32
                                  THEN EDX:EAX := EAX ∗ SRC; FI;
                          ELSE (* OperandSize = 64 *)
                                  RDX:RAX := RAX ∗ SRC;
                  FI;
  FI;
  */

  LLVMContext& context = builder.getContext();
  auto src = operands[0];

  if (src.size == 8 && instruction.operand_count_visible == 1) {
    lift_imul2(0);
    return;
  }
  auto dest1 = operands[1]; // ax
  auto dest2 = operands[2];

  Value* Rvalue = GetOperandValue(src, dest1.size);
  Value* Lvalue = GetOperandValue(dest1, dest1.size);

  uint8_t initialSize = Rvalue->getType()->getIntegerBitWidth();
  printvalue2(initialSize);
  Rvalue = createZExtFolder(Rvalue, Type::getIntNTy(context, initialSize * 2));
  Lvalue = createZExtFolder(Lvalue, Type::getIntNTy(context, initialSize * 2));

  Value* result = createMulFolder(Lvalue, Rvalue, "intmul");

  // Flags
  auto resultType = Type::getIntNTy(context, initialSize);

  Value* highPart = createLShrFolder(result, initialSize, "highPart");
  Value* highPartTruncated = createTruncFolder(
      highPart, Type::getIntNTy(context, initialSize), "truncatedHighPart");

  /* The OF and CF flags are set to 0 if the upper half of the result is
   * 0; otherwise, they are set to 1. The SF, ZF, AF, and PF flags are
   * undefined.
   */
  Value* cf = createICMPFolder(CmpInst::ICMP_NE, highPartTruncated,
                               ConstantInt::get(resultType, 0), "cf");
  Value* of = cf;
  setFlag(FLAG_CF, cf);
  setFlag(FLAG_OF, of);

  auto splitResult = createTruncFolder(
      result, Type::getIntNTy(context, initialSize), "splitResult");
  // if not byte operation, result goes into ?dx:?ax

  SetOperandValue(dest1, splitResult);
  SetOperandValue(dest2, highPartTruncated);

  printvalue(Lvalue) printvalue(Rvalue) printvalue(result) printvalue(highPart);
  printvalue(highPartTruncated) printvalue(splitResult) printvalue(of);
  printvalue(cf);
}

void lifterClass::lift_div() {

  LLVMContext& context = builder.getContext();
  auto src = operands[0];

  Value *divisor, *dividend, *quotient, *remainder;

  // When operand size is 8 bit
  if (src.size == 8) {
    dividend = GetRegisterValue(ZYDIS_REGISTER_AX);
    divisor = GetOperandValue(src, src.size);

    divisor = createZExtFolder(divisor, Type::getIntNTy(context, src.size * 2));
    dividend = createZExtOrTruncFolder(dividend, divisor->getType());

    remainder = createURemFolder(dividend, divisor);
    quotient = createUDivFolder(dividend, divisor);

    SetRegisterValue(
        ZYDIS_REGISTER_AL,
        createZExtOrTruncFolder(quotient, Type::getIntNTy(context, src.size)));

    SetRegisterValue(
        ZYDIS_REGISTER_AH,
        createZExtOrTruncFolder(remainder, Type::getIntNTy(context, src.size)));
  } else {
    auto dividendLowop = operands[1];  // eax
    auto dividendHighop = operands[2]; // edx

    divisor = GetOperandValue(src, src.size);

    Value* dividendLow = GetOperandValue(dividendLowop, src.size);
    Value* dividendHigh = GetOperandValue(dividendHighop, src.size);

    dividendLow =
        createZExtFolder(dividendLow, Type::getIntNTy(context, src.size * 2));
    dividendHigh = createZExtFolder(dividendHigh, dividendLow->getType());
    uint8_t bitWidth = src.size;

    dividendHigh = createShlFolder(dividendHigh, bitWidth);

    printvalue2(bitWidth);
    printvalue(dividendLow);
    printvalue(dividendHigh);

    dividend = createOrFolder(dividendHigh, dividendLow);
    printvalue(dividend);
    Value* ZExtdivisor = createZExtFolder(divisor, dividend->getType());

    if (isa<ConstantInt>(ZExtdivisor) && isa<ConstantInt>(dividend)) {

      APInt divideCI = cast<ConstantInt>(ZExtdivisor)->getValue();
      APInt dividendCI = cast<ConstantInt>(dividend)->getValue();

      APInt quotientCI = dividendCI.udiv(divideCI);
      APInt remainderCI = dividendCI.urem(divideCI);

      printvalue2(divideCI);
      printvalue2(dividendCI);
      printvalue2(quotientCI);
      printvalue2(remainderCI);

      quotient = ConstantInt::get(divisor->getType(), quotientCI);
      remainder = ConstantInt::get(divisor->getType(), remainderCI);
    } else {
      quotient = createUDivFolder(dividend, ZExtdivisor);
      remainder = createURemFolder(dividend, ZExtdivisor);
    }

    SetOperandValue(dividendLowop,
                    createZExtOrTruncFolder(quotient, divisor->getType()));

    SetOperandValue(dividendHighop,
                    createZExtOrTruncFolder(remainder, divisor->getType()));
  }

  printvalue(divisor) printvalue(dividend) printvalue(remainder)
      printvalue(quotient)
}

void lifterClass::lift_idiv() {
  LLVMContext& context = builder.getContext();
  auto src = operands[0];
  if (src.size == 8) {
    auto dividend = GetRegisterValue(ZYDIS_REGISTER_AX);

    Value* divisor = GetOperandValue(src, src.size);
    divisor = createSExtFolder(divisor, Type::getIntNTy(context, src.size * 2));
    dividend = createSExtOrTruncFolder(dividend, divisor->getType());
    Value* remainder = createSRemFolder(dividend, divisor);
    Value* quotient = createSDivFolder(dividend, divisor);

    SetRegisterValue(
        ZYDIS_REGISTER_AL,
        createZExtOrTruncFolder(quotient, Type::getIntNTy(context, src.size)));

    SetRegisterValue(
        ZYDIS_REGISTER_AH,
        createZExtOrTruncFolder(remainder, Type::getIntNTy(context, src.size)));

    printvalue(remainder);
    printvalue(quotient);
    printvalue(divisor);
    printvalue(dividend);
    return;
  }
  auto dividendLowop = operands[1];  // eax
  auto dividendHighop = operands[2]; // edx

  auto Rvalue = GetOperandValue(src, src.size);

  Value *dividendLow, *dividendHigh, *dividend;

  dividendLow = GetOperandValue(dividendLowop, src.size);
  dividendHigh = GetOperandValue(dividendHighop, src.size);

  dividendLow =
      createZExtFolder(dividendLow, Type::getIntNTy(context, src.size * 2));
  dividendHigh = createZExtFolder(dividendHigh, dividendLow->getType());
  uint8_t bitWidth = src.size;

  dividendHigh = createShlFolder(dividendHigh, bitWidth);
  printvalue2(bitWidth);
  printvalue(dividendLow);
  printvalue(dividendHigh);

  dividend = createOrFolder(dividendHigh, dividendLow);
  printvalue(dividend);
  Value* divide = createSExtFolder(Rvalue, dividend->getType());
  Value *quotient, *remainder;
  if (isa<ConstantInt>(divide) && isa<ConstantInt>(dividend)) {

    APInt divideCI = cast<ConstantInt>(divide)->getValue();
    APInt dividendCI = cast<ConstantInt>(dividend)->getValue();

    APInt quotientCI = dividendCI.sdiv(divideCI);
    APInt remainderCI = dividendCI.srem(divideCI);

    printvalue2(divideCI);
    printvalue2(dividendCI);
    printvalue2(quotientCI);
    printvalue2(remainderCI);
    quotient = ConstantInt::get(Rvalue->getType(), quotientCI);
    remainder = ConstantInt::get(Rvalue->getType(), remainderCI);
  } else {
    quotient = createSDivFolder(dividend, divide);
    remainder = createSRemFolder(dividend, divide);
  }
  SetOperandValue(dividendLowop,
                  createZExtOrTruncFolder(quotient, Rvalue->getType()));

  SetOperandValue(dividendHighop,
                  createZExtOrTruncFolder(remainder, Rvalue->getType()));

  printvalue(Rvalue) printvalue(dividend) printvalue(remainder)
      printvalue(quotient)
}

void lifterClass::lift_xor() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];
  auto src = operands[1];
  auto Rvalue = GetOperandValue(src, dest.size);
  auto Lvalue = GetOperandValue(dest, dest.size);
  auto result = createXorFolder(
      Lvalue, Rvalue, "realxor-" + to_string(blockInfo.runtime_address) + "-");

  printvalue(Lvalue) printvalue(Rvalue) printvalue(result);

  auto sf = computeSignFlag(result);
  auto zf = computeZeroFlag(result);
  // auto pf = computeParityFlag(result);
  //  The OF and CF flags are cleared; the SF, ZF, and PF flags are set
  //  according to the result. The state of the AF flag is undefined.

  setFlag(FLAG_SF, sf);
  setFlag(FLAG_ZF, zf);
  setFlag(FLAG_PF, [this, result]() { return computeParityFlag(result); });

  setFlag(FLAG_OF, ConstantInt::getSigned(Type::getInt1Ty(context), 0));
  setFlag(FLAG_CF, ConstantInt::getSigned(Type::getInt1Ty(context), 0));

  SetOperandValue(dest, result);
}

void lifterClass::lift_or() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];
  auto src = operands[1];
  auto Rvalue = GetOperandValue(src, dest.size);
  auto Lvalue = GetOperandValue(dest, dest.size);
  auto result = createOrFolder(
      Lvalue, Rvalue, "realor-" + to_string(blockInfo.runtime_address) + "-");

  printvalue(Lvalue);
  printvalue(Rvalue);
  printvalue(result);

  auto sf = computeSignFlag(result);
  auto zf = computeZeroFlag(result);
  // auto pf = computeParityFlag(result);
  printvalue(sf);
  // The OF and CF flags are cleared; the SF, ZF, and PF flags are set
  // according to the result. The state of the AF flag is undefined.

  setFlag(FLAG_SF, sf);
  setFlag(FLAG_ZF, zf);
  setFlag(FLAG_PF, [this, result]() { return computeParityFlag(result); });

  setFlag(FLAG_OF, ConstantInt::getSigned(Type::getInt1Ty(context), 0));
  setFlag(FLAG_CF, ConstantInt::getSigned(Type::getInt1Ty(context), 0));

  SetOperandValue(dest, result);
}

void lifterClass::lift_and() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];
  auto src = operands[1];
  auto Rvalue = GetOperandValue(src, dest.size);
  auto Lvalue = GetOperandValue(dest, dest.size);

  auto result = createAndFolder(
      Lvalue, Rvalue, "realand-" + to_string(blockInfo.runtime_address) + "-");

  auto sf = computeSignFlag(result);
  auto zf = computeZeroFlag(result);
  // auto pf = computeParityFlag(result);

  // The OF and CF flags are cleared; the SF, ZF, and PF flags are set
  // according to the result. The state of the AF flag is undefined.
  setFlag(FLAG_SF, sf);
  setFlag(FLAG_ZF, zf);
  setFlag(FLAG_PF, [this, result]() { return computeParityFlag(result); });

  setFlag(FLAG_OF, ConstantInt::getSigned(Type::getInt1Ty(context), 0));
  setFlag(FLAG_CF, ConstantInt::getSigned(Type::getInt1Ty(context), 0));

  printvalue(Lvalue) printvalue(Rvalue) printvalue(result);

  SetOperandValue(dest, result, "and" + to_string(blockInfo.runtime_address));
}

void lifterClass::lift_andn() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];
  auto src = operands[1];
  auto Rvalue = GetOperandValue(src, dest.size);
  auto Lvalue = GetOperandValue(dest, dest.size);

  auto result =
      createAndFolder(createNotFolder(Lvalue), Rvalue,
                      "realand-" + to_string(blockInfo.runtime_address) + "-");

  auto sf = computeSignFlag(result);
  auto zf = computeZeroFlag(result);
  // auto pf = computeParityFlag(result);

  // The OF and CF flags are cleared; the SF, ZF, and PF flags are set
  // according to the result. The state of the AF flag is undefined.
  setFlag(FLAG_SF, sf);
  setFlag(FLAG_ZF, zf);

  // setFlag(FLAG_PF, [this, result]() { return computeParityFlag(result); });

  setFlag(FLAG_OF, ConstantInt::getSigned(Type::getInt1Ty(context), 0));
  setFlag(FLAG_CF, ConstantInt::getSigned(Type::getInt1Ty(context), 0));

  printvalue(Lvalue) printvalue(Rvalue) printvalue(result);

  SetOperandValue(dest, result, "and" + to_string(blockInfo.runtime_address));
}

/*

tempCOUNT := (COUNT & COUNTMASK) MOD SIZE
WHILE (tempCOUNT ≠ 0)
        DO
        tempCF := MSB(DEST);
        DEST := (DEST ∗ 2) + tempCF;
        tempCOUNT := tempCOUNT – 1;
        OD;
ELIHW;
IF (COUNT & COUNTMASK) ≠ 0
        THEN CF := LSB(DEST);
FI;
IF (COUNT & COUNTMASK) = 1
        THEN OF := MSB(DEST) XOR CF;
        ELSE OF is undefined;
FI
*/
void lifterClass::lift_rol() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];
  auto src = operands[1];
  auto Lvalue = GetOperandValue(dest, dest.size);
  auto Rvalue = GetOperandValue(src, dest.size);

  auto bitWidth = ConstantInt::get(Lvalue->getType(), dest.size);
  auto bitWidthplusone = ConstantInt::get(Lvalue->getType(), dest.size + 1);
  auto countmask =
      ConstantInt::get(Lvalue->getType(), dest.size == 64 ? 0x3f : 0x1f);

  auto one = ConstantInt::get(Lvalue->getType(), 1);
  auto zero = ConstantInt::get(Lvalue->getType(), 0);

  auto MSBpos = ConstantInt::get(Lvalue->getType(), dest.size - 1);
  Rvalue = createURemFolder(createAndFolder(Rvalue, countmask, "maskRvalue"),
                            bitWidthplusone);

  Value* shiftedLeft = createShlFolder(Lvalue, Rvalue);
  Value* shiftedRight =
      createLShrFolder(Lvalue, createSubFolder(bitWidth, Rvalue), "rol");
  Value* result = createOrFolder(shiftedLeft, shiftedRight);

  Value* cf = createZExtOrTruncFolder(shiftedRight, Type::getInt1Ty(context));

  Value* isZeroBitRotation = createICMPFolder(CmpInst::ICMP_EQ, Rvalue, zero);
  Value* oldcf = getFlag(FLAG_CF);
  cf = createSelectFolder(isZeroBitRotation, oldcf, cf);
  result = createSelectFolder(isZeroBitRotation, Lvalue, result);

  // of = cf ^ MSB
  Value* newMSB = createLShrFolder(result, MSBpos, "rolmsb");
  Value* of = createXorFolder(
      cf, createZExtOrTruncFolder(newMSB, Type::getInt1Ty(context)));

  // Use Select to conditionally update OF based on whether the shift
  // amount is 1
  Value* isOneBitRotation = createICMPFolder(CmpInst::ICMP_EQ, Rvalue, one);
  Value* ofCurrent = getFlag(FLAG_OF);

  of = createSelectFolder(isOneBitRotation, of, ofCurrent);

  setFlag(FLAG_CF, cf);
  setFlag(FLAG_OF, of);

  printvalue(Lvalue) printvalue(Rvalue) printvalue(result);
  SetOperandValue(dest, result);
}

/*

tempCOUNT := (COUNT & COUNTMASK) MOD SIZE
WHILE (tempCOUNT ≠ 0)
        DO
        tempCF := LSB(SRC);
        DEST := (DEST / 2) + (tempCF ∗ 2SIZE);
        tempCOUNT := tempCOUNT – 1;
        OD;
ELIHW;
IF (COUNT & COUNTMASK) ≠ 0
        THEN CF := MSB(DEST);
FI;
IF (COUNT & COUNTMASK) = 1
        THEN OF := MSB(DEST) XOR MSB − 1(DEST);
        ELSE OF is undefined;
FI

*/
void lifterClass::lift_ror() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];
  auto src = operands[1];
  auto Lvalue = GetOperandValue(dest, dest.size);
  auto Rvalue = GetOperandValue(src, dest.size);

  auto bitWidth = ConstantInt::get(Lvalue->getType(), dest.size);
  auto bitWidthplusone = ConstantInt::get(Lvalue->getType(), dest.size + 1);
  auto countmask =
      ConstantInt::get(Lvalue->getType(), dest.size == 64 ? 0x3f : 0x1f);

  auto one = ConstantInt::get(Lvalue->getType(), 1);
  auto zero = ConstantInt::get(Lvalue->getType(), 0);

  auto MSBpos = ConstantInt::get(Lvalue->getType(), dest.size - 1);
  auto secondMSBpos = ConstantInt::get(Lvalue->getType(), dest.size - 2);
  Rvalue = createURemFolder(createAndFolder(Rvalue, countmask, "maskRvalue"),
                            bitWidthplusone);

  Value* rightshifted = createLShrFolder(Lvalue, Rvalue);
  Value* leftshifted =
      createShlFolder(Lvalue, createSubFolder(bitWidth, Rvalue));
  Value* result =
      createOrFolder(rightshifted, leftshifted,
                     "ror-" + std::to_string(blockInfo.runtime_address) + "-");

  Value* msb = createLShrFolder(result, MSBpos);
  Value* cf = createZExtOrTruncFolder(msb, Type::getInt1Ty(context), "ror-cf");

  Value* secondMsb = createLShrFolder(result, secondMSBpos, "ror2ndmsb");

  auto ofDefined =
      createZExtOrTruncFolder(createXorFolder(msb, secondMsb), cf->getType());

  auto isOneBitRotation = createICMPFolder(CmpInst::ICMP_EQ, Rvalue, one);
  Value* ofCurrent = getFlag(FLAG_OF);
  Value* of =
      createSelectFolder(isOneBitRotation, ofDefined, ofCurrent, "ror-of");

  setFlag(FLAG_CF, cf);
  setFlag(FLAG_OF, of);

  auto isZeroBitRotation =
      createICMPFolder(CmpInst::ICMP_EQ, Rvalue, zero, "iszerobit");
  result = createSelectFolder(isZeroBitRotation, Lvalue, result, "ror-result");

  printvalue(Lvalue) printvalue(Rvalue) printvalue(result);

  SetOperandValue(dest, result);
}

void lifterClass::lift_inc() {
  auto operand = operands[0];

  Value* Lvalue = GetOperandValue(operand, operand.size);

  Value* one = ConstantInt::get(Lvalue->getType(), 1, true);
  Value* result = createAddFolder(
      Lvalue, one, "inc-" + std::to_string(blockInfo.runtime_address) + "-");
  Value* of = computeOverflowFlagAdd(Lvalue, one, result);
  // The CF flag is not affected. The OF, SF, ZF, AF, and PF flags are set
  // according to the result.
  // treat it as add r, 1 for flags

  printvalue(Lvalue) printvalue(result);

  Value* sf = computeSignFlag(result);
  Value* zf = computeZeroFlag(result);
  Value* pf = computeParityFlag(result);

  printvalue(sf);

  setFlag(FLAG_OF, of);
  setFlag(FLAG_SF, sf);
  setFlag(FLAG_ZF, zf);
  setFlag(FLAG_PF, pf);

  auto lowerNibbleMask = ConstantInt::get(Lvalue->getType(), 0xF);
  auto destLowerNibble = createAndFolder(Lvalue, lowerNibbleMask, "adcdst");
  auto srcLowerNibble = one;
  auto sumLowerNibble = createAddFolder(destLowerNibble, srcLowerNibble);
  auto af =
      createICMPFolder(CmpInst::ICMP_UGT, sumLowerNibble, lowerNibbleMask);

  setFlag(FLAG_AF, af);
  SetOperandValue(operand, result);
}

void lifterClass::lift_dec() {
  auto operand = operands[0];

  Value* Lvalue = GetOperandValue(operand, operand.size);

  Value* one = ConstantInt::get(Lvalue->getType(), 1, true);
  Value* result = createSubFolder(
      Lvalue, one, "dec-" + std::to_string(blockInfo.runtime_address) + "-");
  Value* of = computeOverflowFlagSub(Lvalue, one, result);

  // The CF flag is not affected. The OF, SF, ZF, AF, and PF flags are set
  // according to the result.
  // treat it as sub r, 1 for flags

  printvalue(Lvalue) printvalue(result);

  Value* sf = computeSignFlag(result);
  Value* zf = computeZeroFlag(result);
  Value* pf = computeParityFlag(result);

  printvalue(sf);

  setFlag(FLAG_OF, of);
  setFlag(FLAG_SF, sf);
  setFlag(FLAG_ZF, zf);
  setFlag(FLAG_PF, pf);

  auto lowerNibbleMask = ConstantInt::get(Lvalue->getType(), 0xF);
  auto RvalueLowerNibble =
      createAndFolder(Lvalue, lowerNibbleMask, "lvalLowerNibble");
  auto op2LowerNibble = one;
  auto af = createICMPFolder(CmpInst::ICMP_ULT, RvalueLowerNibble,
                             op2LowerNibble, "sub_af");
  setFlag(FLAG_AF, af);
  SetOperandValue(operand, result);
}

void lifterClass::lift_push() {
  LLVMContext& context = builder.getContext();
  auto src = operands[0]; // value that we are pushing
  auto dest = operands[2];
  auto rsp = operands[1];

  auto Rvalue = GetOperandValue(src, dest.size);
  auto RspValue = GetOperandValue(rsp, rsp.size); // ?
  auto val = ConstantInt::getSigned(
      Type::getInt64Ty(context),
      dest.size / 8); // jokes on me apparently this is not a fixed value
  auto result = createSubFolder(RspValue, val,
                                "pushing_newrsp-" +
                                    to_string(blockInfo.runtime_address) + "-");

  printvalue(RspValue) printvalue(result)
      SetOperandValue(rsp, result, to_string(blockInfo.runtime_address));
  ; // sub rsp 8 first,

  SetOperandValue(dest, Rvalue, to_string(blockInfo.runtime_address));
  ; // then mov rsp, val
}

void lifterClass::lift_pushfq() {
  LLVMContext& context = builder.getContext();
  auto src = operands[2];  // value that we are pushing rflags
  auto dest = operands[1]; // [rsp]
  auto rsp = operands[0];  // rsp

  auto Rvalue = GetOperandValue(src, dest.size);
  // auto Rvalue = GetRFLAGS(builder);
  auto RspValue = GetOperandValue(rsp, rsp.size);

  auto val = ConstantInt::get(Type::getInt64Ty(context), src.size / 8);
  auto result = createSubFolder(RspValue, val);

  SetOperandValue(rsp, result, to_string(blockInfo.runtime_address));
  ; // sub rsp 8 first,

  // pushFlags( dest, Rvalue,
  // to_string(blockInfo.runtime_address));;
  SetOperandValue(dest, Rvalue, to_string(blockInfo.runtime_address));
  ; // then mov rsp, val
}

void lifterClass::lift_pop() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0]; // value that we are pushing
  auto src = operands[2];
  auto rsp = operands[1];

  auto Rvalue =
      GetOperandValue(src, dest.size, to_string(blockInfo.runtime_address));
  ;
  auto RspValue =
      GetOperandValue(rsp, rsp.size, to_string(blockInfo.runtime_address));
  ;

  auto val = ConstantInt::getSigned(Type::getInt64Ty(context),
                                    dest.size / 8); // assuming its x64
  auto result = createAddFolder(RspValue, val,
                                "popping_new_rsp-" +
                                    to_string(blockInfo.runtime_address) + "-");

  printvalue(Rvalue) printvalue(RspValue) printvalue(result);

  SetOperandValue(rsp, result); // then add rsp 8

  SetOperandValue(dest, Rvalue, to_string(blockInfo.runtime_address));
  ; // mov val, rsp first
}

void lifterClass::lift_leave() {
  LLVMContext& context = builder.getContext();
  auto src2 = operands[0]; // [xsp]
  auto src1 = operands[1]; // xbp
  auto dest = operands[2]; // xsp
  // first xbp to xsp
  // then [xsp] to xbp

  auto xbp = GetOperandValue(src1, dest.size,
                             std::to_string(blockInfo.runtime_address));

  SetOperandValue(dest, xbp,
                  std::to_string(blockInfo.runtime_address)); // move xbp to xsp

  auto popstack = popStack(dest.size / 8);

  SetOperandValue(src1, popstack); // then add rsp 8

  // mov val, rsp first
}

void lifterClass::lift_popfq() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[2]; // value that we are pushing
  auto src = operands[1];  // [rsp]
  auto rsp = operands[0];  // rsp

  auto Rvalue =
      GetOperandValue(src, dest.size, to_string(blockInfo.runtime_address));

  auto RspValue =
      GetOperandValue(rsp, rsp.size, to_string(blockInfo.runtime_address));

  auto val = ConstantInt::getSigned(Type::getInt64Ty(context), dest.size / 8);
  auto result = createAddFolder(
      RspValue, val, "popfq-" + to_string(blockInfo.runtime_address) + "-");

  SetOperandValue(dest, Rvalue, to_string(blockInfo.runtime_address));
  // mov val, rsp first
  SetOperandValue(rsp, result, to_string(blockInfo.runtime_address));
  // then add rsp 8
}

void lifterClass::lift_leave() {
  LLVMContext& context = builder.getContext();

  auto stack = operands[0]; // value that we are pushing (stack memory)
  auto rbp = operands[1]; // rbp
  auto rsp = operands[2]; // rsp

  // mov rsp, rbp
  auto RbpValue =
      GetOperandValue(rbp, rbp.size, to_string(blockInfo.runtime_address));

  printvalue(RbpValue);

  // pop rbp
  auto Rvalue =
      GetOperandValue(stack, rbp.size, to_string(blockInfo.runtime_address));
  ;

  auto val = ConstantInt::getSigned(Type::getInt64Ty(context),
                                    rbp.size / 8); // assuming its x64
  auto result = createAddFolder(RbpValue, val,
                                "leaving_new_rsp-" +
                                    to_string(blockInfo.runtime_address) + "-");

  printvalue(Rvalue) printvalue(RbpValue) printvalue(result);

  SetOperandValue(rsp, result); // then add rsp 8

  SetOperandValue(rbp, Rvalue, to_string(blockInfo.runtime_address));
}

void lifterClass::lift_adc() {
  auto dest = operands[0];
  auto src = operands[1];

  Value* Lvalue = GetOperandValue(dest, dest.size);
  Value* Rvalue = GetOperandValue(src, dest.size);

  Value* cf = getFlag(FLAG_CF);
  cf = createZExtFolder(cf, Lvalue->getType());

  Value* tempResult = createAddFolder(
      Lvalue, Rvalue, "adc-temp-" + to_string(blockInfo.runtime_address) + "-");
  Value* result = createAddFolder(
      tempResult, cf,
      "adc-result-" + to_string(blockInfo.runtime_address) + "-");
  // The OF, SF, ZF, AF, CF, and PF flags are set according to the result.

  printvalue(Lvalue) printvalue(Rvalue) printvalue(tempResult)
      printvalue(result)

          auto cfAfterFirstAdd = createOrFolder(
              createICMPFolder(CmpInst::ICMP_ULT, tempResult, Lvalue),
              createICMPFolder(CmpInst::ICMP_ULT, tempResult, Rvalue));
  auto cfFinal = createOrFolder(
      cfAfterFirstAdd, createICMPFolder(CmpInst::ICMP_ULT, result, cf));

  auto af = computeAuxFlag(Lvalue, Rvalue, result);

  // auto of = computeOverflowFlagAdc(Lvalue, Rvalue, cf, result);

  Value* sf = computeSignFlag(result);
  Value* zf = computeZeroFlag(result);
  // Value* pf = computeParityFlag(result);

  setFlag(FLAG_OF, [this, Lvalue, Rvalue, cf, result]() {
    return computeOverflowFlagAdd(Lvalue, Rvalue, result);
  });
  setFlag(FLAG_AF, af);
  setFlag(FLAG_CF, cfFinal);
  setFlag(FLAG_SF, sf);
  setFlag(FLAG_ZF, zf);

  setFlag(FLAG_PF, [this, result]() { return computeParityFlag(result); });

  SetOperandValue(dest, result);
}

void lifterClass::lift_xadd() {
  auto dest = operands[0];
  auto src = operands[1];

  auto Lvalue = GetOperandValue(dest, dest.size);
  auto Rvalue = GetOperandValue(src, src.size);

  Value* sumValue = createAddFolder(
      Lvalue, Rvalue, "xadd_sum-" + to_string(blockInfo.runtime_address) + "-");

  SetOperandValue(dest, sumValue, to_string(blockInfo.runtime_address));
  ;

  SetOperandValue(src, Lvalue, to_string(blockInfo.runtime_address));
  ;
  /*
  TEMP := SRC + DEST;
  SRC := DEST;
  DEST := TEMP;
  */
  printvalue(Lvalue) printvalue(Rvalue) printvalue(sumValue)

      auto cf =
          createOrFolder(createICMPFolder(CmpInst::ICMP_ULT, sumValue, Lvalue),
                         createICMPFolder(CmpInst::ICMP_ULT, sumValue, Rvalue));

  auto lowerNibbleMask = ConstantInt::get(Lvalue->getType(), 0xF);
  auto destLowerNibble = createAndFolder(Lvalue, lowerNibbleMask, "xadddst");
  auto srcLowerNibble = createAndFolder(Rvalue, lowerNibbleMask, "xaddsrc");
  auto sumLowerNibble = createAddFolder(destLowerNibble, srcLowerNibble);
  auto af =
      createICMPFolder(CmpInst::ICMP_UGT, sumLowerNibble, lowerNibbleMask);

  auto resultSign = createICMPFolder(CmpInst::ICMP_SLT, sumValue,
                                     ConstantInt::get(Lvalue->getType(), 0));
  auto destSign = createICMPFolder(CmpInst::ICMP_SLT, Lvalue,
                                   ConstantInt::get(Lvalue->getType(), 0));
  auto srcSign = createICMPFolder(CmpInst::ICMP_SLT, Rvalue,
                                  ConstantInt::get(Rvalue->getType(), 0));
  auto inputSameSign = createICMPFolder(CmpInst::ICMP_EQ, destSign, srcSign);
  auto of = createAndFolder(
      inputSameSign, createICMPFolder(CmpInst::ICMP_NE, destSign, resultSign),
      "xaddof");

  Value* sf = computeSignFlag(sumValue);
  Value* zf = computeZeroFlag(sumValue);
  Value* pf = computeParityFlag(sumValue);

  setFlag(FLAG_OF, of);
  setFlag(FLAG_AF, af);
  setFlag(FLAG_CF, cf);
  setFlag(FLAG_SF, sf);
  setFlag(FLAG_ZF, zf);
  setFlag(FLAG_PF, pf);

  // The CF, PF, AF, SF, ZF, and OF flags are set according to the result
  // of the addition, which is stored in the destination operand.
}

void lifterClass::lift_test() {
  LLVMContext& context = builder.getContext();
  Value* Lvalue = GetOperandValue(operands[0], operands[0].size);
  Value* Rvalue = GetOperandValue(operands[1], operands[0].size);

  Value* testResult = createAndFolder(Lvalue, Rvalue, "testAnd");
  printvalue(Lvalue);
  printvalue(Rvalue);
  printvalue(testResult);

  Value* of = ConstantInt::get(Type::getInt64Ty(context), 0, "of");
  Value* cf = ConstantInt::get(Type::getInt64Ty(context), 0, "cf");

  Value* sf =
      createICMPFolder(CmpInst::ICMP_SLT, testResult,
                       ConstantInt::get(testResult->getType(), 0), "sf");
  Value* zf =
      createICMPFolder(CmpInst::ICMP_EQ, testResult,
                       ConstantInt::get(testResult->getType(), 0), "zf");
  Value* pf = computeParityFlag(testResult);

  setFlag(FLAG_OF, of);
  setFlag(FLAG_CF, cf);
  setFlag(FLAG_SF, sf);
  setFlag(FLAG_ZF, zf);
  setFlag(FLAG_PF, pf);
}

void lifterClass::lift_cmp() {

  Value* Lvalue = GetOperandValue(operands[0], operands[0].size);
  Value* Rvalue = GetOperandValue(operands[1], operands[0].size);

  Value* cmpResult = createSubFolder(Lvalue, Rvalue);

  Value* signL = createICMPFolder(CmpInst::ICMP_SLT, Lvalue,
                                  ConstantInt::get(Lvalue->getType(), 0));
  Value* signR = createICMPFolder(CmpInst::ICMP_SLT, Rvalue,
                                  ConstantInt::get(Rvalue->getType(), 0));
  Value* signResult = createICMPFolder(
      CmpInst::ICMP_SLT, cmpResult, ConstantInt::get(cmpResult->getType(), 0));

  Value* of = createOrFolder(
      createAndFolder(signL, createAndFolder(createNotFolder(signR),
                                             createNotFolder(signResult),
                                             "cmp-and1-")),
      createAndFolder(createNotFolder(signL),
                      createAndFolder(signR, signResult), "cmp-and2-"),
      "cmp-OF-or");

  Value* cf = createICMPFolder(CmpInst::ICMP_ULT, Lvalue, Rvalue);
  Value* zf = createICMPFolder(CmpInst::ICMP_EQ, cmpResult,
                               ConstantInt::get(cmpResult->getType(), 0));
  Value* sf = createICMPFolder(CmpInst::ICMP_SLT, cmpResult,
                               ConstantInt::get(cmpResult->getType(), 0));
  Value* pf = computeParityFlag(cmpResult);
  printvalue(Lvalue);
  printvalue(Rvalue);
  printvalue(cmpResult);
  setFlag(FLAG_OF, of);
  setFlag(FLAG_CF, cf);
  setFlag(FLAG_SF, sf);
  setFlag(FLAG_ZF, zf);
  setFlag(FLAG_PF, pf);

  setFlag(FLAG_AF, [this, cmpResult, Lvalue, Rvalue]() {
    auto lowerNibbleMask = ConstantInt::get(Lvalue->getType(), 0xF);
    auto RvalueLowerNibble =
        createAndFolder(Lvalue, lowerNibbleMask, "lvalLowerNibble");
    auto op2LowerNibble =
        createAndFolder(Rvalue, lowerNibbleMask, "rvalLowerNibble");
    return createICMPFolder(CmpInst::ICMP_ULT, RvalueLowerNibble,
                            op2LowerNibble, "sub_af");
  });
}

void lifterClass::lift_rdtsc() {
  // cout << blockInfo.runtime_address << "\n";
  LLVMContext& context = builder.getContext();
  auto rdtscCall = builder.CreateIntrinsic(Intrinsic::readcyclecounter, {}, {});
  auto edxPart = createLShrFolder(rdtscCall, 32, "to_edx");
  auto eaxPart =
      createZExtOrTruncFolder(rdtscCall, Type::getInt32Ty(context), "to_eax");
  SetRegisterValue(ZYDIS_REGISTER_EDX, edxPart);
  SetRegisterValue(ZYDIS_REGISTER_EAX, eaxPart);
}

void lifterClass::lift_cpuid() {
  LLVMContext& context = builder.getContext();
  // operands[0] = eax
  // operands[1] = ebx
  // operands[2] = ecx
  // operands[3] = edx
  /*

  c++
  #include <intrin.h>

  int getcpuid() {
          int cpuInfo[4];
          __cpuid(cpuInfo, 1);
          return cpuInfo[0] + cpuInfo[1];
  }

  ir
  define dso_local noundef i32 @getcpuid() #0 {
    %1 = alloca [4 x i32], align 16
    %2 = getelementptr inbounds [4 x i32], ptr %1, i64 0, i64 0
    %3 = call { i32, i32, i32, i32 } asm "xchgq %rbx,
  ${1:q}\0Acpuid\0Axchgq %rbx, ${1:q}", "={ax},=r,={cx},={dx},0,2"(i32 1,
  i32 0) %4 = getelementptr inbounds [4 x i32], ptr %1, i64 0, i64 0 %5 =
  extractvalue { i32, i32, i32, i32 } %3, 0 %6 = getelementptr inbounds
  i32, ptr %4, i32 0 store i32 %5, ptr %6, align 4 %7 = extractvalue {
  i32, i32, i32, i32 } %3, 1 %8 = getelementptr inbounds i32, ptr %4, i32
  1 store i32 %7, ptr %8, align 4 %9 = extractvalue { i32, i32, i32, i32 }
  %3, 2 %10 = getelementptr inbounds i32, ptr %4, i32 2 store i32 %9, ptr
  %10, align 4 %11 = extractvalue { i32, i32, i32, i32 } %3, 3 %12 =
  getelementptr inbounds i32, ptr %4, i32 3 store i32 %11, ptr %12, align
  4

    %13 = getelementptr inbounds [4 x i32], ptr %1, i64 0, i64 0
    %14 = load i32, ptr %13, align 16

    %15 = getelementptr inbounds [4 x i32], ptr %1, i64 0, i64 1
    %16 = load i32, ptr %15, align 4
    %17 = add nsw i32 %14, %16
    ret i32 %17
  }
  opt
  define dso_local noundef i32 @getcpuid() local_unnamed_addr {
    %1 = tail call { i32, i32, i32, i32 } asm "xchgq %rbx,
  ${1:q}\0Acpuid\0Axchgq %rbx, ${1:q}", "={ax},=r,={cx},={dx},0,2"(i32 1,
  i32 0) #0 %2 = extractvalue { i32, i32, i32, i32 } %1, 1 ret i32 %2
  }

  */
  // int cpuInfo[4];
  // ArrayType* CpuInfoTy = ArrayType::get(Type::getInt32Ty(context), 4);

  Value* eax = GetOperandValue(operands[0], operands[0].size);
  // one is eax, other is always 0?
  std::vector<Type*> AsmOutputs = {
      Type::getInt32Ty(context), Type::getInt32Ty(context),
      Type::getInt32Ty(context), Type::getInt32Ty(context)};
  StructType* AsmStructType = StructType::get(context, AsmOutputs);

  std::vector<Type*> ArgTypes = {Type::getInt32Ty(context),
                                 Type::getInt32Ty(context)};

  // this is probably incorrect
  InlineAsm* IA =
      InlineAsm::get(FunctionType::get(AsmStructType, ArgTypes, false),
                     "xchgq %rbx, ${1:q}\ncpuid\nxchgq %rbx, ${1:q}",
                     "={ax},=r,={cx},={dx},0,2", true);

  std::vector<Value*> Args{eax, ConstantInt::get(eax->getType(), 0)};

  Value* cpuidCall = builder.CreateCall(IA, Args);

  Value* eaxv = builder.CreateExtractValue(cpuidCall, 0, "eax");
  Value* ebx = builder.CreateExtractValue(cpuidCall, 1, "ebx");
  Value* ecx = builder.CreateExtractValue(cpuidCall, 2, "ecx");
  Value* edx = builder.CreateExtractValue(cpuidCall, 3, "edx");

  SetOperandValue(operands[0], eaxv);
  SetOperandValue(operands[1], ebx);
  SetOperandValue(operands[2], ecx);
  SetOperandValue(operands[3], edx);
}

uint64_t alternative_pext(uint64_t source, uint64_t mask) {
  uint64_t result = 0;
  int bit_position = 0;
  for (uint64_t i = 0; i < 64; ++i) {
    if (mask & (1ULL << i)) {
      if (source & (1ULL << i)) {
        result |= (1ULL << bit_position);
      }
      ++bit_position;
    }
  }
  return result;
}

void lifterClass::lift_pext() {
  const auto dest = operands[0];
  const auto src1 = operands[1];
  const auto src2 = operands[2];

  const auto src1v = GetOperandValue(operands[1], operands[1].size);
  const auto src2v = GetOperandValue(operands[2], operands[2].size);
  if (isa<ConstantInt>(src1v) && isa<ConstantInt>(src2v)) {
    const auto src1_c = cast<ConstantInt>(src1v);
    const auto src2_c = cast<ConstantInt>(src2v);
    const auto res =
        alternative_pext(src1_c->getZExtValue(), src2_c->getZExtValue());
    printvalue(src1_c);
    printvalue(src2_c);
    printvalue2(res);
    SetOperandValue(dest, ConstantInt::get(src1v->getType(), res));
  } else {
    Function* fakyu = cast<Function>(
        fnc->getParent()
            ->getOrInsertFunction("pext",
                                  Type::getIntNTy(fnc->getContext(), dest.size))
            .getCallee());
    auto rs = builder.CreateCall(fakyu, {src1v, src2v});
    SetOperandValue(
        dest,
        createAndFolder(
            rs, ConstantInt::get(rs->getType(),
                                 rs->getType()->getIntegerBitWidth() * 2 - 1)));
    // UNREACHABLE("lazy mf");
  }
}

void lifterClass::lift_setnz() {
  LLVMContext& context = builder.getContext();

  auto dest = operands[0];

  Value* zf = getFlag(FLAG_ZF);

  Value* result =
      createZExtFolder(createNotFolder(zf), Type::getInt8Ty(context));

  SetOperandValue(dest, result);
}
void lifterClass::lift_seto() {
  LLVMContext& context = builder.getContext();

  auto dest = operands[0];

  Value* of = getFlag(FLAG_OF);

  Value* result = createZExtFolder(of, Type::getInt8Ty(context));

  SetOperandValue(dest, result);
}
void lifterClass::lift_setno() {
  LLVMContext& context = builder.getContext();

  auto dest = operands[0];

  Value* of = getFlag(FLAG_OF);

  Value* notOf = createNotFolder(of, "notOF");

  Value* result = createZExtFolder(notOf, Type::getInt8Ty(context));

  SetOperandValue(dest, result);
}

void lifterClass::lift_setnb() {
  LLVMContext& context = builder.getContext();

  auto dest = operands[0];

  Value* cf = getFlag(FLAG_CF);

  Value* result = createICMPFolder(
      CmpInst::ICMP_EQ, cf, ConstantInt::get(Type::getInt1Ty(context), 0));

  Value* byteResult = createZExtFolder(result, Type::getInt8Ty(context));

  SetOperandValue(dest, byteResult);
}

void lifterClass::lift_setbe() {
  LLVMContext& context = builder.getContext();

  Value* cf = getFlag(FLAG_CF);
  Value* zf = getFlag(FLAG_ZF);

  Value* condition = createOrFolder(cf, zf, "setbe-or");

  Value* result = createZExtFolder(condition, Type::getInt8Ty(context));

  auto dest = operands[0];
  SetOperandValue(dest, result);
}

void lifterClass::lift_setnbe() {
  LLVMContext& context = builder.getContext();

  Value* cf = getFlag(FLAG_CF);
  Value* zf = getFlag(FLAG_ZF);

  Value* condition =
      createAndFolder(createNotFolder(cf), createNotFolder(zf), "setnbe-and");

  Value* result = createZExtFolder(condition, Type::getInt8Ty(context));

  auto dest = operands[0];
  SetOperandValue(dest, result);
}

void lifterClass::lift_setns() {
  LLVMContext& context = builder.getContext();

  auto dest = operands[0];

  Value* sf = getFlag(FLAG_SF);

  Value* result = createICMPFolder(
      CmpInst::ICMP_EQ, sf, ConstantInt::get(Type::getInt1Ty(context), 0));

  Value* byteResult = createZExtFolder(result, Type::getInt8Ty(context));

  SetOperandValue(dest, byteResult);
}

void lifterClass::lift_setp() {
  LLVMContext& context = builder.getContext();

  Value* pf = getFlag(FLAG_PF);

  Value* result = createZExtFolder(pf, Type::getInt8Ty(context));

  auto dest = operands[0];

  SetOperandValue(dest, result);
}

void lifterClass::lift_setnp() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];

  Value* pf = getFlag(FLAG_PF);

  Value* resultValue =
      createZExtFolder(createNotFolder(pf), Type::getInt8Ty(context));

  SetOperandValue(dest, resultValue, to_string(blockInfo.runtime_address));
}

void lifterClass::lift_setb() {
  LLVMContext& context = builder.getContext();

  auto dest = operands[0];

  Value* cf = getFlag(FLAG_CF);

  Value* result = createZExtFolder(cf, Type::getInt8Ty(context));

  SetOperandValue(dest, result);
}

void lifterClass::lift_sets() {
  LLVMContext& context = builder.getContext();
  Value* sf = getFlag(FLAG_SF);

  Value* result = createZExtFolder(sf, Type::getInt8Ty(context));

  auto dest = operands[0];
  SetOperandValue(dest, result);
}

void lifterClass::lift_stosx() {

  auto dest = operands[0]; // xdi
  Value* destValue = GetOperandValue(dest, dest.size);
  Value* DF = getFlag(FLAG_DF);
  // if df is 1, +
  // else -
  auto destbitwidth = dest.size;

  auto one = ConstantInt::get(DF->getType(), 1);
  Value* Direction =
      createSubFolder(createMulFolder(DF, createAddFolder(DF, one)), one);

  Value* result = createAddFolder(
      destValue, createMulFolder(
                     Direction, ConstantInt::get(DF->getType(), destbitwidth)));
  SetOperandValue(dest, result);
}

void lifterClass::lift_setz() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];

  Value* zf = getFlag(FLAG_ZF);
  printvalue(zf);
  Value* extendedZF =
      createZExtFolder(zf, Type::getInt8Ty(context), "setz_extend");

  SetOperandValue(dest, extendedZF);
}

void lifterClass::lift_setnle() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];

  Value* zf = getFlag(FLAG_ZF);
  Value* sf = getFlag(FLAG_SF);
  Value* of = getFlag(FLAG_OF);

  Value* zfNotSet = createICMPFolder(
      CmpInst::ICMP_EQ, zf, ConstantInt::get(Type::getInt1Ty(context), 0));

  Value* sfEqualsOf = createICMPFolder(CmpInst::ICMP_EQ, sf, of);

  printvalue(zf) printvalue(sf) printvalue(of)

      Value* combinedCondition =
          createAndFolder(zfNotSet, sfEqualsOf, "setnle-and");

  Value* byteResult =
      createZExtFolder(combinedCondition, Type::getInt8Ty(context));

  SetOperandValue(dest, byteResult);
}

void lifterClass::lift_setle() {
  LLVMContext& context = builder.getContext();
  Value* zf = getFlag(FLAG_ZF);
  Value* sf = getFlag(FLAG_SF);
  Value* of = getFlag(FLAG_OF);

  Value* sf_ne_of = createICMPFolder(CmpInst::ICMP_NE, sf, of);
  Value* condition = createOrFolder(zf, sf_ne_of, "setle-or");

  Value* result = createZExtFolder(condition, Type::getInt8Ty(context));

  auto dest = operands[0];
  SetOperandValue(dest, result);
}

void lifterClass::lift_setnl() {
  LLVMContext& context = builder.getContext();
  Value* sf = getFlag(FLAG_SF);
  Value* of = getFlag(FLAG_OF);

  Value* condition = createICMPFolder(CmpInst::ICMP_EQ, sf, of);

  Value* result = createZExtFolder(condition, Type::getInt8Ty(context));

  auto dest = operands[0];
  SetOperandValue(dest, result);
}

void lifterClass::lift_setl() {
  LLVMContext& context = builder.getContext();
  Value* sf = getFlag(FLAG_SF);
  Value* of = getFlag(FLAG_OF);

  Value* condition = createICMPFolder(CmpInst::ICMP_NE, sf, of);

  Value* result = createZExtFolder(condition, Type::getInt8Ty(context));

  auto dest = operands[0];
  SetOperandValue(dest, result);
}

void lifterClass::lift_bt() {

  auto dest = operands[0];
  auto bitIndex = operands[1];

  // If the bit base operand specifies a register, the instruction takes
  // the modulo 16, 32, or 64 of the bit offset operand (modulo size
  // depends on the mode and register size; 64-bit operands are available
  // only in 64-bit mode). If the bit base operand specifies a memory
  // location, the operand represents the address of the byte in memory
  // that contains the bit base (bit 0 of the specified byte) of the bit
  // string. The range of the bit position that can be referenced by the
  // offset operand depends on the operand size. CF := Bit(BitBase,
  // BitOffset);

  auto Lvalue = GetOperandValue(dest, dest.size);
  auto bitIndexValue = GetOperandValue(bitIndex, dest.size);

  unsigned LvalueBitW = cast<IntegerType>(Lvalue->getType())->getBitWidth();

  auto Rvalue =
      createAndFolder(bitIndexValue, ConstantInt::get(bitIndexValue->getType(),
                                                      LvalueBitW - 1));

  auto shl =
      createShlFolder(ConstantInt::get(bitIndexValue->getType(), 1), Rvalue);

  auto andd = createAndFolder(shl, Lvalue);

  auto cf = createICMPFolder(CmpInst::ICMP_NE, andd,
                             ConstantInt::get(andd->getType(), 0));

  setFlag(FLAG_CF, cf);
  printvalue(Rvalue);
  printvalue(Lvalue);
  printvalue(shl);
  printvalue(andd);
  printvalue(cf);
}

void lifterClass::lift_btr() {
  auto base = operands[0];
  auto offset = operands[1];

  unsigned baseBitWidth = base.size;

  Value* bitOffset = GetOperandValue(offset, base.size);

  Value* bitOffsetMasked = createAndFolder(
      bitOffset, ConstantInt::get(bitOffset->getType(), baseBitWidth - 1),
      "bitOffsetMasked");

  Value* baseVal = GetOperandValue(base, base.size);

  Value* bit = createLShrFolder(baseVal, bitOffsetMasked,
                                "btr-lshr-" +
                                    to_string(blockInfo.runtime_address) + "-");

  Value* one = ConstantInt::get(bit->getType(), 1);

  bit = createAndFolder(bit, one, "btr-and");

  setFlag(FLAG_CF, bit);

  Value* mask = createShlFolder(ConstantInt::get(baseVal->getType(), 1),
                                bitOffsetMasked, "btr-shl");

  mask = createNotFolder(mask); // invert mask
  baseVal = createAndFolder(
      baseVal, mask, "btr-and-" + to_string(blockInfo.runtime_address) + "-");

  SetOperandValue(base, baseVal);
  printvalue(bitOffset);
  printvalue(baseVal);
  printvalue(mask);
}

void lifterClass::lift_lzcnt() {
  // check
  auto dest = operands[0];
  auto src = operands[1];

  Value* Rvalue = GetOperandValue(src, src.size);
  Value* isZero = createICMPFolder(CmpInst::ICMP_EQ, Rvalue,
                                   ConstantInt::get(Rvalue->getType(), 0));
  Value* isOperandSize = createICMPFolder(
      CmpInst::ICMP_EQ, Rvalue, ConstantInt::get(Rvalue->getType(), dest.size));
  setFlag(FLAG_ZF, isZero);
  setFlag(FLAG_CF, isOperandSize);

  unsigned bitWidth = Rvalue->getType()->getIntegerBitWidth();

  Value* index = ConstantInt::get(Rvalue->getType(), bitWidth - 1);
  Value* zeroVal = ConstantInt::get(Rvalue->getType(), 0);
  Value* oneVal = ConstantInt::get(Rvalue->getType(), 1);

  Value* bitPosition = ConstantInt::get(Rvalue->getType(), -1);

  for (unsigned i = 0; i < bitWidth; ++i) {

    Value* mask = createShlFolder(oneVal, index);

    Value* test = createAndFolder(Rvalue, mask, "bsrtest");
    Value* isBitSet = createICMPFolder(CmpInst::ICMP_NE, test, zeroVal);

    Value* tmpPosition = createSelectFolder(isBitSet, index, bitPosition);

    Value* isPositionUnset = createICMPFolder(
        CmpInst::ICMP_EQ, bitPosition, ConstantInt::get(Rvalue->getType(), -1));
    bitPosition = createSelectFolder(isPositionUnset, tmpPosition, bitPosition);

    index = createSubFolder(index, oneVal);
  }

  SetOperandValue(dest, bitPosition);
}

void lifterClass::lift_bsr() {
  // check
  auto dest = operands[0];
  auto src = operands[1];

  Value* Rvalue = GetOperandValue(src, src.size);
  Value* isZero = createICMPFolder(CmpInst::ICMP_EQ, Rvalue,
                                   ConstantInt::get(Rvalue->getType(), 0));
  setFlag(FLAG_ZF, isZero);

  unsigned bitWidth = Rvalue->getType()->getIntegerBitWidth();

  Value* index = ConstantInt::get(Rvalue->getType(), bitWidth - 1);
  Value* zeroVal = ConstantInt::get(Rvalue->getType(), 0);
  Value* oneVal = ConstantInt::get(Rvalue->getType(), 1);

  Value* bitPosition = ConstantInt::get(Rvalue->getType(), -1);

  for (unsigned i = 0; i < bitWidth; ++i) {

    Value* mask = createShlFolder(oneVal, index);

    Value* test = createAndFolder(Rvalue, mask, "bsrtest");
    Value* isBitSet = createICMPFolder(CmpInst::ICMP_NE, test, zeroVal);

    Value* tmpPosition = createSelectFolder(isBitSet, index, bitPosition);

    Value* isPositionUnset = createICMPFolder(
        CmpInst::ICMP_EQ, bitPosition, ConstantInt::get(Rvalue->getType(), -1));
    bitPosition = createSelectFolder(isPositionUnset, tmpPosition, bitPosition);

    index = createSubFolder(index, oneVal);
  }

  SetOperandValue(dest, bitPosition);
}

void lifterClass::lift_blsr() {
  auto tmp = operands[0];
  auto src = operands[1];

  Value* source = GetOperandValue(src, src.size);
  auto one = ConstantInt::get(source->getType(), 1);
  auto temp = createAndFolder(createSubFolder(source, one), source);

  SetOperandValue(tmp, temp);
  setFlag(FLAG_ZF, computeZeroFlag(temp));
  setFlag(FLAG_SF, computeSignFlag(temp));
}

void lifterClass::lift_bzhi() {
  auto dst = operands[0];
  auto src = operands[1];
  auto src2 = operands[2];

  Value* source = GetOperandValue(src, src.size);
  Value* source2 = GetOperandValue(src, 8);
  auto one = ConstantInt::get(source2->getType(), 1);
  auto bitmask = createAShrFolder(createShlFolder(one, source2), source2);
  auto result = createAndFolder(source, bitmask);
  SetOperandValue(dst, result);
  setFlag(FLAG_ZF, computeZeroFlag(result));
  setFlag(FLAG_SF, computeSignFlag(result));
  setFlag(FLAG_OF, ConstantInt::get(source->getType(), 0));
  auto CF = createICMPFolder(CmpInst::ICMP_SGE, source2,
                             ConstantInt::get(source->getType(), dst.size - 1));
  setFlag(FLAG_CF, CF);
}

void lifterClass::lift_bsf() {
  // TODOs
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];
  auto src = operands[1];

  Value* Rvalue = GetOperandValue(src, src.size);

  Value* isZero = createICMPFolder(CmpInst::ICMP_EQ, Rvalue,
                                   ConstantInt::get(Rvalue->getType(), 0));
  setFlag(FLAG_ZF, isZero);

  Type* intType = Rvalue->getType();
  uint64_t intWidth = intType->getIntegerBitWidth();

  Value* result = ConstantInt::get(intType, intWidth);
  Value* one = ConstantInt::get(intType, 1);

  Value* continuecounting = ConstantInt::get(Type::getInt1Ty(context), 1);
  for (uint64_t i = 0; i < intWidth; ++i) {
    Value* bitMask =
        createShlFolder(one, ConstantInt::get(intType, i));        // a = v >> i
    Value* bitSet = createAndFolder(Rvalue, bitMask, "bsfbitset"); // b = a & 1
    Value* isBitZero = createICMPFolder(
        CmpInst::ICMP_EQ, bitSet, ConstantInt::get(intType, 0)); // c = b == 0
    // continue until isBitZero is 1
    // 0010
    // if continuecounting, select
    Value* possibleResult = ConstantInt::get(intType, i);
    Value* condition = createAndFolder(continuecounting, isBitZero,
                                       "bsfcondition"); // cond = cc, c, 0
    continuecounting = createNotFolder(isBitZero);      // cc = ~c
    result = createSelectFolder(
        condition, result, possibleResult,
        "updateResultOnFirstNonZeroBit"); // cond ift res(64) , i
  }

  SetOperandValue(dest, result);
}

void lifterClass::lift_tzcnt() {
  LLVMContext& context = builder.getContext();
  auto dest = operands[0];
  auto src = operands[1];

  Value* Rvalue = GetOperandValue(src, src.size);

  Value* isZero = createICMPFolder(CmpInst::ICMP_EQ, Rvalue,
                                   ConstantInt::get(Rvalue->getType(), 0));
  setFlag(FLAG_ZF, isZero);

  Value* isEq2OperandSize = createICMPFolder(
      CmpInst::ICMP_EQ, Rvalue, ConstantInt::get(Rvalue->getType(), src.size));

  setFlag(FLAG_CF, isEq2OperandSize);

  Type* intType = Rvalue->getType();
  uint64_t intWidth = intType->getIntegerBitWidth();

  Value* result = ConstantInt::get(intType, intWidth);
  Value* one = ConstantInt::get(intType, 1);

  Value* continuecounting = ConstantInt::get(Type::getInt1Ty(context), 1);
  for (uint64_t i = 0; i < intWidth; ++i) {
    Value* bitMask = createShlFolder(one, ConstantInt::get(intType, i));
    Value* bitSet = createAndFolder(Rvalue, bitMask, "bsfbitset");
    Value* isBitZero = createICMPFolder(CmpInst::ICMP_EQ, bitSet,
                                        ConstantInt::get(intType, 0));
    // continue until isBitZero is 1
    // 0010
    // if continuecounting, select
    Value* possibleResult = ConstantInt::get(intType, i);
    Value* condition =
        createAndFolder(continuecounting, isBitZero, "bsfcondition");
    continuecounting = createNotFolder(isBitZero);
    result = createSelectFolder(condition, result, possibleResult,
                                "updateResultOnFirstNonZeroBit");
  }

  SetOperandValue(dest, result);
}

void lifterClass::lift_btc() {
  auto base = operands[0];
  auto offset = operands[1];

  unsigned baseBitWidth = base.size;

  Value* bitOffset = GetOperandValue(offset, base.size);

  Value* bitOffsetMasked = createAndFolder(
      bitOffset, ConstantInt::get(bitOffset->getType(), baseBitWidth - 1),
      "bitOffsetMasked");

  Value* baseVal = GetOperandValue(base, base.size);

  Value* bit = createLShrFolder(baseVal, bitOffsetMasked,
                                "btc-lshr-" +
                                    to_string(blockInfo.runtime_address) + "-");

  Value* one = ConstantInt::get(bit->getType(), 1);

  bit = createAndFolder(bit, one, "btc-and");

  setFlag(FLAG_CF, bit);

  Value* mask = createShlFolder(ConstantInt::get(baseVal->getType(), 1),
                                bitOffsetMasked, "btc-shl");

  baseVal = createXorFolder(
      baseVal, mask, "btc-and-" + to_string(blockInfo.runtime_address) + "-");

  SetOperandValue(base, baseVal);
  printvalue(bitOffset);
  printvalue(baseVal);
  printvalue(mask);
}

void lifterClass::lift_lahf() {

  LLVMContext& context = builder.getContext();

  auto sf = getFlag(FLAG_SF);
  auto zf = getFlag(FLAG_ZF);
  auto af = getFlag(FLAG_AF);
  auto pf = getFlag(FLAG_PF);
  auto cf = getFlag(FLAG_CF);

  printvalue(sf) printvalue(zf) printvalue(af) printvalue(pf) printvalue(cf);

  cf = createZExtFolder(cf, Type::getInt8Ty(context));
  pf = createShlFolder(createZExtFolder(pf, Type::getInt8Ty(context)), FLAG_PF);
  af = createShlFolder(createZExtFolder(af, Type::getInt8Ty(context)), FLAG_AF);
  zf = createShlFolder(createZExtFolder(zf, Type::getInt8Ty(context)), FLAG_ZF);
  sf = createShlFolder(createZExtFolder(sf, Type::getInt8Ty(context)), FLAG_SF);
  Value* Rvalue = createAddFolder(
      createOrFolder(
          createOrFolder(createOrFolder(cf, pf), createOrFolder(af, sf)), zf),
      ConstantInt::get(cf->getType(), 2));

  printvalue(sf) printvalue(zf) printvalue(af) printvalue(pf) printvalue(cf);
  printvalue(Rvalue);
  SetRegisterValue(ZYDIS_REGISTER_AH, Rvalue);
}
void lifterClass::lift_sahf() {

  auto ah = GetRegisterValue(ZYDIS_REGISTER_AH);
  // RFLAGS(SF:ZF:0:AF:0:PF:1:CF) := AH;
  //
  printvalue(GetRegisterValue(ZYDIS_REGISTER_RAX));
  printvalue(ah);
  Value* one = ConstantInt::get(ah->getType(), 1);
  auto cf = createAndFolder(
      createLShrFolder(ah, ConstantInt::get(ah->getType(), FLAG_CF)), one);
  // + 2
  auto pf = createAndFolder(
      createLShrFolder(ah, ConstantInt::get(ah->getType(), FLAG_PF)), one);
  auto af = createAndFolder(
      createLShrFolder(ah, ConstantInt::get(ah->getType(), FLAG_AF)), one);
  auto zf = createAndFolder(
      createLShrFolder(ah, ConstantInt::get(ah->getType(), FLAG_ZF)), one);
  auto sf = createAndFolder(
      createLShrFolder(ah, ConstantInt::get(ah->getType(), FLAG_SF)), one);
  setFlag(FLAG_CF, cf);
  setFlag(FLAG_PF, pf);
  setFlag(FLAG_AF, af);
  setFlag(FLAG_ZF, zf);
  setFlag(FLAG_SF, sf);
}
void lifterClass::lift_std() {
  LLVMContext& context = builder.getContext();

  setFlag(FLAG_DF, ConstantInt::get(Type::getInt1Ty(context), 1));
}
void lifterClass::lift_stc() {
  LLVMContext& context = builder.getContext();

  setFlag(FLAG_CF, ConstantInt::get(Type::getInt1Ty(context), 1));
}

void lifterClass::lift_cmc() {

  Value* cf = getFlag(FLAG_CF);

  Value* one = ConstantInt::get(cf->getType(), 1);

  setFlag(FLAG_CF, createXorFolder(cf, one));
}

void lifterClass::lift_clc() {

  LLVMContext& context = builder.getContext();

  Value* clearedCF = ConstantInt::get(Type::getInt1Ty(context), 0);

  setFlag(FLAG_CF, clearedCF);
}

void lifterClass::lift_cld() {

  LLVMContext& context = builder.getContext();

  Value* clearedDF = ConstantInt::get(Type::getInt1Ty(context), 0);

  setFlag(FLAG_DF, clearedDF);
}

void lifterClass::lift_cli() {

  LLVMContext& context = builder.getContext();

  Value* resetIF = ConstantInt::get(Type::getInt1Ty(context), 0);

  setFlag(FLAG_IF, resetIF);
}

void lifterClass::lift_bts() {
  auto base = operands[0];
  auto offset = operands[1];

  unsigned baseBitWidth = base.size;

  Value* bitOffset = GetOperandValue(offset, base.size);

  Value* bitOffsetMasked = createAndFolder(
      bitOffset, ConstantInt::get(bitOffset->getType(), baseBitWidth - 1),
      "bitOffsetMasked");

  Value* baseVal = GetOperandValue(base, base.size);

  Value* bit = createLShrFolder(baseVal, bitOffsetMasked,
                                "bts-lshr-" +
                                    to_string(blockInfo.runtime_address) + "-");

  Value* one = ConstantInt::get(bit->getType(), 1);

  bit = createAndFolder(bit, one, "bts-and");

  setFlag(FLAG_CF, bit);

  Value* mask = createShlFolder(ConstantInt::get(baseVal->getType(), 1),
                                bitOffsetMasked, "bts-shl");

  baseVal = createOrFolder(
      baseVal, mask, "bts-or-" + to_string(blockInfo.runtime_address) + "-");

  SetOperandValue(base, baseVal);
  printvalue(bitOffset);
  printvalue(baseVal);
  printvalue(mask);
}

void lifterClass::lift_cwd() {
  LLVMContext& context = builder.getContext();

  Value* ax =
      createZExtOrTruncFolder(GetOperandValue(operands[1], operands[1].size),
                              Type::getInt16Ty(context));

  Value* signBit = computeSignFlag(ax);

  Value* dx = createSelectFolder(
      createICMPFolder(CmpInst::ICMP_EQ, signBit,
                       ConstantInt::get(signBit->getType(), 0)),
      ConstantInt::get(Type::getInt16Ty(context), 0),
      ConstantInt::get(Type::getInt16Ty(context), 0xFFFF), "setDX");

  SetOperandValue(operands[0], dx);
}

void lifterClass::lift_cdq() {
  LLVMContext& context = builder.getContext();
  // if eax is -, then edx is filled with ones FFFF_FFFF
  Value* eax =
      createZExtOrTruncFolder(GetOperandValue(operands[1], operands[1].size),
                              Type::getInt32Ty(context));

  Value* signBit = computeSignFlag(eax);

  Value* edx = createSelectFolder(
      createICMPFolder(CmpInst::ICMP_EQ, signBit,
                       ConstantInt::get(signBit->getType(), 0)),
      ConstantInt::get(Type::getInt32Ty(context), 0),
      ConstantInt::get(Type::getInt32Ty(context), 0xFFFFFFFF), "setEDX");

  SetOperandValue(operands[0], edx);
}

void lifterClass::lift_cqo() {

  LLVMContext& context = builder.getContext();
  // if rax is -, then rdx is filled with ones FFFF_FFFF_FFFF_FFFF
  Value* rax =
      createZExtOrTruncFolder(GetOperandValue(operands[1], operands[1].size),
                              Type::getInt64Ty(context));

  Value* signBit = computeSignFlag(rax);

  Value* rdx = createSelectFolder(
      createICMPFolder(CmpInst::ICMP_EQ, signBit,
                       ConstantInt::get(signBit->getType(), 0)),
      ConstantInt::get(Type::getInt64Ty(context), 0),
      ConstantInt::get(Type::getInt64Ty(context), 0xFFFFFFFFFFFFFFFF),
      "setRDX");
  printvalue(rax) printvalue(signBit) printvalue(rdx)
      SetOperandValue(operands[0], rdx);
}

void lifterClass::lift_cbw() {
  LLVMContext& context = builder.getContext();
  Value* al = createZExtOrTruncFolder(
      GetOperandValue(operands[1], operands[1].size), Type::getInt8Ty(context));

  Value* ax = createSExtFolder(al, Type::getInt16Ty(context), "cbw");

  SetOperandValue(operands[0], ax);
}

void lifterClass::lift_cwde() {
  LLVMContext& context = builder.getContext();
  Value* ax =
      createZExtOrTruncFolder(GetOperandValue(operands[1], operands[1].size),
                              Type::getInt16Ty(context));
  printvalue(ax);
  Value* eax = createSExtFolder(ax, Type::getInt32Ty(context), "cwde");
  printvalue(eax);
  SetOperandValue(operands[0], eax);
}

void lifterClass::lift_cdqe() {
  LLVMContext& context = builder.getContext();

  Value* eax =
      createZExtOrTruncFolder(GetOperandValue(operands[1], operands[1].size),
                              Type::getInt32Ty(context), "cdqe-trunc");

  Value* rax = createSExtFolder(eax, Type::getInt64Ty(context), "cdqe");

  SetOperandValue(operands[0], rax);
}

void lifterClass::liftInstructionSemantics() {

  switch (instruction.mnemonic) {
  // movs
  // case ZYDIS_MNEMONIC_MOVAPS:
  // case ZYDIS_MNEMONIC_MOVUPS:
  case ZYDIS_MNEMONIC_MOVZX:
  case ZYDIS_MNEMONIC_MOVSX:
  case ZYDIS_MNEMONIC_MOVSXD:
  case ZYDIS_MNEMONIC_MOV: {
    lift_mov();
    break;
  }
  case ZYDIS_MNEMONIC_MOVSB:
  case ZYDIS_MNEMONIC_MOVSW:
  case ZYDIS_MNEMONIC_MOVSD:
  case ZYDIS_MNEMONIC_MOVSQ: {
    lift_movs_X();
    break;
  }
  case ZYDIS_MNEMONIC_BEXTR: {
    lift_bextr();
    break;
  }
    // cmov
  case ZYDIS_MNEMONIC_CMOVZ: {
    lift_cmovz();
    break;
  }
  case ZYDIS_MNEMONIC_CMOVNZ: {
    lift_cmovnz();
    break;
  }
  case ZYDIS_MNEMONIC_CMOVL: {
    lift_cmovl();
    break;
  }
  case ZYDIS_MNEMONIC_CMOVB: {
    lift_cmovb();
    break;
  }
  case ZYDIS_MNEMONIC_CMOVNB: {
    lift_cmovnb();
    break;
  }
  case ZYDIS_MNEMONIC_CMOVNS: {
    lift_cmovns();
    break;
  }

  case ZYDIS_MNEMONIC_CMOVBE: {
    lift_cmovbz();
    break;
  }
  case ZYDIS_MNEMONIC_CMOVNBE: {
    lift_cmovnbz();
    break;
  }
  case ZYDIS_MNEMONIC_CMOVNL: {
    lift_cmovnl();
    break;
  }
  case ZYDIS_MNEMONIC_CMOVS: {
    lift_cmovs();
    break;
  }
  case ZYDIS_MNEMONIC_CMOVNLE: {
    lift_cmovnle();
    break;
  }
  case ZYDIS_MNEMONIC_CMOVLE: {
    lift_cmovle();
    break;
  }

  case ZYDIS_MNEMONIC_CMOVO: {
    lift_cmovo();
    break;
  }
  case ZYDIS_MNEMONIC_CMOVNO: {
    lift_cmovno();
    break;
  }
  case ZYDIS_MNEMONIC_CMOVP: {
    lift_cmovp();
    break;
  }
  case ZYDIS_MNEMONIC_CMOVNP: {
    lift_cmovnp();
    break;
  }
    // branches

  case ZYDIS_MNEMONIC_RET: {
    lift_ret();
    break;
  }

  case ZYDIS_MNEMONIC_JMP: {
    lift_jmp();
    break;
  }

  case ZYDIS_MNEMONIC_JNZ: {
    lift_jnz();
    break;
  }
  case ZYDIS_MNEMONIC_JZ: {
    lift_jz();
    break;
  }
  case ZYDIS_MNEMONIC_JS: {
    lift_js();
    break;
  }
  case ZYDIS_MNEMONIC_JNS: {
    lift_jns();
    break;
  }
  case ZYDIS_MNEMONIC_JNBE: {

    lift_jnbe();
    break;
  }
  case ZYDIS_MNEMONIC_JNB: {
    lift_jnb();
    break;
  }
  case ZYDIS_MNEMONIC_JB: {
    lift_jb();
    break;
  }
  case ZYDIS_MNEMONIC_JBE: {

    lift_jbe();
    break;
  }
  case ZYDIS_MNEMONIC_JNLE: {
    lift_jnle();
    break;
  }
  case ZYDIS_MNEMONIC_JLE: {

    lift_jle();
    break;
  }
  case ZYDIS_MNEMONIC_JNL: {

    lift_jnl();
    break;
  }
  case ZYDIS_MNEMONIC_JL: {

    lift_jl();
    break;
  }
  case ZYDIS_MNEMONIC_JO: {

    lift_jo();
    break;
  }
  case ZYDIS_MNEMONIC_JNO: {

    lift_jno();
    break;
  }
  case ZYDIS_MNEMONIC_JP: {

    lift_jp();
    break;
  }
  case ZYDIS_MNEMONIC_JNP: {

    lift_jnp();
    break;
  }
    // arithmetics and logical operations

  case ZYDIS_MNEMONIC_XCHG: {
    lift_xchg();
    break;
  }
  case ZYDIS_MNEMONIC_CMPXCHG: {
    lift_cmpxchg();
    break;
  }
  case ZYDIS_MNEMONIC_NOT: {
    lift_not();
    break;
  }

  case ZYDIS_MNEMONIC_BSWAP: {
    lift_bswap();
    break;
  }
  case ZYDIS_MNEMONIC_NEG: {
    lift_neg();
    break;
  }
  case ZYDIS_MNEMONIC_SARX:
  case ZYDIS_MNEMONIC_SAR: {
    lift_sar();
    break;
  }
  case ZYDIS_MNEMONIC_SHLX:
  case ZYDIS_MNEMONIC_SHL: {
    lift_shl();
    break;
  }
  case ZYDIS_MNEMONIC_POPCNT: {
    lift_popcnt();
    break;
  }
  case ZYDIS_MNEMONIC_SHLD: {
    lift_shld();
    break;
  }
  case ZYDIS_MNEMONIC_SHRD: {
    lift_shrd();
    break;
  }
  case ZYDIS_MNEMONIC_SHRX:
  case ZYDIS_MNEMONIC_SHR: {
    lift_shr();
    break;
  }

  case ZYDIS_MNEMONIC_RCR: {
    lift_rcr();
    break;
  }
  case ZYDIS_MNEMONIC_RCL: {
    lift_rcl();
    break;
  }
  case ZYDIS_MNEMONIC_SBB: {
    lift_sbb();
    break;
  }
  case ZYDIS_MNEMONIC_ADC: {
    lift_adc();
    break;
  }
  case ZYDIS_MNEMONIC_XADD: {
    lift_xadd();
    break;
  }

  case ZYDIS_MNEMONIC_LEA: {
    lift_lea();
    break;
  }
  case ZYDIS_MNEMONIC_INC: {
    lift_inc();
    break;
  }

  case ZYDIS_MNEMONIC_DEC: {
    lift_dec();
    break;
  }

  case ZYDIS_MNEMONIC_MUL: {
    lift_mul();
    break;
  }
  case ZYDIS_MNEMONIC_IMUL: {
    lift_imul();
    break;
  }
  case ZYDIS_MNEMONIC_DIV: {
    lift_div();
    break;
  }
  case ZYDIS_MNEMONIC_IDIV: {
    lift_idiv();
    break;
  }
  case ZYDIS_MNEMONIC_SUB:
  case ZYDIS_MNEMONIC_ADD: {
    lift_add_sub();

    break;
  }

  case ZYDIS_MNEMONIC_XOR: {
    lift_xor();
    break;
  }
  case ZYDIS_MNEMONIC_OR: {
    lift_or();
    break;
  }
  case ZYDIS_MNEMONIC_AND: {
    lift_and();
    break;
  }
  case ZYDIS_MNEMONIC_ANDN: {
    lift_andn();
    break;
  }
  case ZYDIS_MNEMONIC_ROR: {
    lift_ror();

    break;
  }
  case ZYDIS_MNEMONIC_ROL: {
    lift_rol();

    break;
  }

  case ZYDIS_MNEMONIC_PUSH: {
    lift_push();
    break;
  }
  case ZYDIS_MNEMONIC_PUSHF:
  case ZYDIS_MNEMONIC_PUSHFQ: {
    lift_pushfq();
    break;
  }
  case ZYDIS_MNEMONIC_POP: {
    lift_pop();
    break;
  }
  case ZYDIS_MNEMONIC_POPF:
  case ZYDIS_MNEMONIC_POPFQ: {
    lift_popfq();
    break;
  }
  case ZYDIS_MNEMONIC_LEAVE: {
    lift_leave();
    break;
  }
  case ZYDIS_MNEMONIC_TEST: {
    lift_test();
    break;
  }
  case ZYDIS_MNEMONIC_CMP: {
    lift_cmp();
    break;
  }
  case ZYDIS_MNEMONIC_RDTSC: {
    lift_rdtsc();
    break;
  }
  case ZYDIS_MNEMONIC_CPUID: {
    lift_cpuid();
    break;
  }
  case ZYDIS_MNEMONIC_PEXT: {
    lift_pext();
    break;
  }

  case ZYDIS_MNEMONIC_CALL: {
    lift_call();
    break;
  }

  // set and flags
  case ZYDIS_MNEMONIC_STOSB:
  case ZYDIS_MNEMONIC_STOSW:
  case ZYDIS_MNEMONIC_STOSD:
  case ZYDIS_MNEMONIC_STOSQ: {
    lift_stosx();
    break;
  }
  case ZYDIS_MNEMONIC_SETZ: {
    lift_setz();
    break;
  }
  case ZYDIS_MNEMONIC_SETNZ: {
    lift_setnz();
    break;
  }
  case ZYDIS_MNEMONIC_SETO: {
    lift_seto();
    break;
  }
  case ZYDIS_MNEMONIC_SETNO: {
    lift_setno();
    break;
  }
  case ZYDIS_MNEMONIC_SETNB: {
    lift_setnb();
    break;
  }
  case ZYDIS_MNEMONIC_SETNBE: {
    lift_setnbe();
    break;
  }
  case ZYDIS_MNEMONIC_SETBE: {
    lift_setbe();
    break;
  }
  case ZYDIS_MNEMONIC_SETNS: {
    lift_setns();
    break;
  }
  case ZYDIS_MNEMONIC_SETP: {
    lift_setp();
    break;
  }
  case ZYDIS_MNEMONIC_SETNP: {
    lift_setnp();
    break;
  }
  case ZYDIS_MNEMONIC_SETB: {
    lift_setb();
    break;
  }
  case ZYDIS_MNEMONIC_SETS: {
    lift_sets();
    break;
  }
  case ZYDIS_MNEMONIC_SETNLE: {
    lift_setnle();
    break;
  }
  case ZYDIS_MNEMONIC_SETLE: {
    lift_setle();
    break;
  }
  case ZYDIS_MNEMONIC_SETNL: {
    lift_setnl();
    break;
  }
  case ZYDIS_MNEMONIC_SETL: {
    lift_setl();
    break;
  }

  case ZYDIS_MNEMONIC_BTR: {
    lift_btr();
    break;
  }
  case ZYDIS_MNEMONIC_LZCNT: {
    lift_lzcnt();
    break;
  }
  case ZYDIS_MNEMONIC_BSR: {
    lift_bsr();
    break;
  }
  case ZYDIS_MNEMONIC_BSF: {
    lift_bsf();
    break;
  }
  case ZYDIS_MNEMONIC_BLSR: {
    lift_blsr();
    break;
  }
  case ZYDIS_MNEMONIC_BZHI: {
    lift_bzhi();
    break;
  }
  case ZYDIS_MNEMONIC_TZCNT: {
    lift_tzcnt();
    break;
  }
  case ZYDIS_MNEMONIC_BTC: {
    lift_btc();
    break;
  }
  case ZYDIS_MNEMONIC_LAHF: {
    lift_lahf();
    break;
  }
  case ZYDIS_MNEMONIC_SAHF: {
    lift_sahf();
    break;
  }
  case ZYDIS_MNEMONIC_STD: {
    lift_std();
    break;
  }
  case ZYDIS_MNEMONIC_CLD: {
    lift_cld();
    break;
  }
  case ZYDIS_MNEMONIC_STC: {
    lift_stc();
    break;
  }
  case ZYDIS_MNEMONIC_CMC: {
    lift_cmc();
    break;
  }
  case ZYDIS_MNEMONIC_CLC: {
    lift_clc();
    break;
  }

  case ZYDIS_MNEMONIC_CLI: {
    lift_cli();
    break;
  }
  case ZYDIS_MNEMONIC_BTS: {
    lift_bts();
    break;
  }
  case ZYDIS_MNEMONIC_BT: {
    lift_bt();
    break;
  }

  case ZYDIS_MNEMONIC_CDQ: { // these are not related to flags at all
    lift_cdq();
    break;
  }
  case ZYDIS_MNEMONIC_CWDE: {
    lift_cwde();
    break;
  }
  case ZYDIS_MNEMONIC_CWD: {
    lift_cwd();
    break;
  }
  case ZYDIS_MNEMONIC_CQO: {
    lift_cqo();
    break;
  }
  case ZYDIS_MNEMONIC_CDQE: {
    lift_cdqe();
    break;
  }
  case ZYDIS_MNEMONIC_CBW: {
    lift_cbw();
    break;
  }
  case ZYDIS_MNEMONIC_PAUSE:
  case ZYDIS_MNEMONIC_NOP: {
    break;
  }
  case ZYDIS_MNEMONIC_UD2: {
    Function* externFunc = cast<Function>(
        fnc->getParent()
            ->getOrInsertFunction("exception", fnc->getReturnType())
            .getCallee()); // Just call exception and return
    builder.CreateRet(builder.CreateCall(externFunc));
    run = 0; // prettify this probably
    finished = 1;
    return;
  }
  default: {

    ZydisFormatter formatter;

    ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL);
    char buffer[256];
    ZydisFormatterFormatInstruction(
        &formatter, &(instruction), operands, instruction.operand_count_visible,
        &buffer[0], sizeof(buffer), blockInfo.runtime_address, ZYAN_NULL);

    std::cout << "not implemented: " << instruction.mnemonic
              << " runtime: " << std::hex << blockInfo.runtime_address << " "
              << buffer << std::endl;

    debugging::doIfDebug([&]() {
      std::string Filename = "output_notimplemented.ll";
      std::error_code EC;
      raw_fd_ostream OS(Filename, EC);
      builder.GetInsertBlock()->getParent()->getParent()->print(OS, nullptr);
    });
    UNREACHABLE("Instruction not implemented");
  }
  }
}

void lifterClass::liftInstruction() {
  LLVMContext& context = builder.getContext();
  // RIP gets updated before execution of the instruction->
  /*
  auto val = ConstantInt::getSigned(Type::getInt64Ty(context),
                                    blockInfo.runtime_address);
  SetRegisterValue(ZYDIS_REGISTER_RIP, val);
  */
  // auto rsp = GetRegisterValue(ZYDIS_REGISTER_RSP);
  // printvalue(rsp);
  printvalue2(blockInfo.runtime_address);
  auto funcInfo = funcsignatures::getFunctionInfo(blockInfo.runtime_address);

  if (funcInfo) {
    callFunctionIR(funcInfo->name.c_str(), funcInfo);
    outs() << "calling: " << funcInfo->name.c_str() << "\n";
    outs().flush();
    auto next_jump = popStack(BinaryOperations::getBitness() / 8);

    // get [rsp], jump there
    if (!isa<ConstantInt>(next_jump)) {
      UNREACHABLE("next_jump is not a ConstantInt.");
      return;
    }
    auto RIP_value = cast<ConstantInt>(next_jump);
    auto jump_address = RIP_value->getZExtValue();

    auto bb = BasicBlock::Create(context, "returnToOrgCF",
                                 builder.GetInsertBlock()->getParent());
    builder.CreateBr(bb);

    blockInfo = BBInfo(jump_address, bb);
    run = 0;
    return;
  }

  // if really an import, jump_address + imagebase should return a string (?)
  uint64_t jump_address = blockInfo.runtime_address;
  APInt temp;
  bool isReadable = BinaryOperations::readMemory(jump_address, 1, temp);
  bool isImport = BinaryOperations::isImport(jump_address);
  if (!isReadable && isImport &&
      cast<ConstantInt>(GetRegisterValue(ZYDIS_REGISTER_RSP))->getValue() !=
          STACKP_VALUE) {
    printvalueforce2(jump_address);
    auto bb = BasicBlock::Create(context, "returnToOrgCF",
                                 builder.GetInsertBlock()->getParent());
    // actually call the function first

    auto functionName = BinaryOperations::getName(jump_address);
    outs() << "calling : " << functionName
           << " addr: " << (uint64_t)jump_address;
    outs().flush();

    callFunctionIR(functionName, nullptr);

    auto next_jump = popStack(BinaryOperations::getBitness() / 8);

    // get [rsp], jump there
    auto RIP_value = cast<ConstantInt>(next_jump);
    jump_address = RIP_value->getZExtValue();

    builder.CreateBr(bb);

    blockInfo = BBInfo(jump_address, bb);
    run = 0;
    return;
  }
  if (!isReadable && !isImport) {
    printvalueforce2(jump_address);
    printvalueforce2(isReadable);
    printvalueforce2(isImport);
    // done something wrong;
    debugging::doIfDebug([&]() {
      std::string Filename = "output_external.ll";
      std::error_code EC;
      raw_fd_ostream OS(Filename, EC);
      builder.GetInsertBlock()->getParent()->getParent()->print(OS, nullptr);
    });
    outs().flush();
    UNREACHABLE("Trying to execute invalid external function");
  }

  // do something for prefixes like rep here
  liftInstructionSemantics();
}