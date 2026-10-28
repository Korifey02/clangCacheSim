#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
// С LLVM 23 заголовок плагинов переехал в отдельную библиотеку Plugins
#if __has_include("llvm/Plugins/PassPlugin.h")
#include "llvm/Plugins/PassPlugin.h"
#else
#include "llvm/Passes/PassPlugin.h"
#endif
#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

static cl::opt<std::string> Stage(
    "ccs-stage", cl::init("early"),
    cl::desc("Когда вставлять трассировку: early — обращения как в исходнике "
             "(после SROA), late — после всех оптимизаций"));

namespace {

class Instrumenter {
public:
  explicit Instrumenter(Module &M);
  bool run();

private:
  Module &M;
  LLVMContext &Ctx;
  const DataLayout &DL;
  PointerType *PtrTy;
  StructType *VarTy;
  FunctionCallee Hook;
  FunctionCallee RegisterStack;
  FunctionCallee RegisterGlobal;
  DenseMap<const Value *, Constant *> Vars;
  // Место для регистраций при входе в функцию, запомненное до любых вставок
  DenseMap<Function *, Instruction *> EntryPos;

  void instrument(Instruction *Before, Value *Ptr, Value *Size, bool IsWrite);
  void registerAlloca(AllocaInst *AI);
  void registerAllocaAt(AllocaInst *AI, Instruction *Before);
  void registerGlobals();
  Constant *varFor(Value *Ptr);
  Constant *makeVar(StringRef Name);
};

// Адрес уходит за пределы функции: в вызов, в память, в возвращаемое значение
bool escapes(const AllocaInst *AI) {
#if LLVM_VERSION_MAJOR >= 21
  return PointerMayBeCaptured(AI, /*ReturnCaptures=*/true);
#else
  return PointerMayBeCaptured(AI, /*ReturnCaptures=*/true, /*StoreCaptures=*/true);
#endif
}

std::string globalName(const GlobalVariable *GV) {
  if (GV->getName().starts_with(".str"))
    return "<string literal>";
  return demangle(GV->getName());
}

std::string localName(const DILocalVariable *V) {
  const DISubprogram *SP = V->getScope()->getSubprogram();
  if (!SP || SP->getName().empty())
    return V->getName().str();
  return (SP->getName() + "::" + V->getName()).str();
}

std::string allocaName(AllocaInst *AI) {
  for (DbgVariableRecord *R : findDVRDeclares(AI))
    return localName(R->getVariable());
  // С оптимизациями и -g clang вместо dbg_declare ставит dbg_assign
  for (DbgVariableRecord *R : at::getDVRAssignmentMarkers(AI))
    return localName(R->getVariable());
  std::string Fn = demangle(AI->getFunction()->getName());
  if (AI->hasName())
    return Fn + "::" + AI->getName().str();
  // Рантайм сводит переменные по имени, поэтому безымянным нужен номер
  unsigned N = 0;
  for (Instruction &I : instructions(AI->getFunction())) {
    if (&I == AI)
      break;
    N += isa<AllocaInst>(I);
  }
  return Fn + "::<stack#" + std::to_string(N) + ">";
}

Instrumenter::Instrumenter(Module &M)
    : M(M), Ctx(M.getContext()), DL(M.getDataLayout()) {
  PtrTy = PointerType::getUnqual(Ctx);
  Type *I32 = Type::getInt32Ty(Ctx);
  // Совпадает с struct ccs_var в runtime/var.h
  VarTy = StructType::get(Ctx, {PtrTy, I32});

  // Рантайм не трогает память программы, поэтому вызов не мешает
  // оптимизатору двигать сами load/store вокруг него.
  AttrBuilder AB(Ctx);
  AB.addAttribute(Attribute::NoUnwind);
  AB.addAttribute(Attribute::WillReturn);
  AB.addMemoryAttr(MemoryEffects::inaccessibleMemOnly());
  Hook = M.getOrInsertFunction(
      "__ccs_access", AttributeList::get(Ctx, AttributeList::FunctionIndex, AB),
      Type::getVoidTy(Ctx), PtrTy, Type::getInt64Ty(Ctx), I32, PtrTy);

  AttrBuilder NoUnwind(Ctx);
  NoUnwind.addAttribute(Attribute::NoUnwind);
  AttributeList RegAttrs =
      AttributeList::get(Ctx, AttributeList::FunctionIndex, NoUnwind);
  RegisterStack = M.getOrInsertFunction("__ccs_register_stack", RegAttrs,
                                        Type::getVoidTy(Ctx), PtrTy,
                                        Type::getInt64Ty(Ctx), PtrTy);
  RegisterGlobal = M.getOrInsertFunction("__ccs_register_global", RegAttrs,
                                         Type::getVoidTy(Ctx), PtrTy,
                                         Type::getInt64Ty(Ctx), PtrTy);
}

bool Instrumenter::run() {
  SmallVector<Instruction *, 128> Work;
  // Анализ убегания — до вставки вызовов, иначе они сами будут считаться захватом
  SmallVector<AllocaInst *, 16> Escaping;
  for (Function &F : M) {
    if (F.isDeclaration() || F.getName().starts_with("__ccs_"))
      continue;
    EntryPos[&F] = &*F.getEntryBlock().getFirstNonPHIOrDbgOrAlloca();
    for (Instruction &I : instructions(F)) {
      if (isa<LoadInst, StoreInst, MemTransferInst, MemSetInst>(&I))
        Work.push_back(&I);
      else if (auto *AI = dyn_cast<AllocaInst>(&I); AI && escapes(AI))
        Escaping.push_back(AI);
    }
  }

  for (Instruction *I : Work) {
    if (auto *LI = dyn_cast<LoadInst>(I)) {
      TypeSize TS = DL.getTypeStoreSize(LI->getType());
      if (!TS.isScalable())
        instrument(I, LI->getPointerOperand(),
                   ConstantInt::get(Type::getInt64Ty(Ctx), TS), false);
    } else if (auto *SI = dyn_cast<StoreInst>(I)) {
      TypeSize TS = DL.getTypeStoreSize(SI->getValueOperand()->getType());
      if (!TS.isScalable())
        instrument(I, SI->getPointerOperand(),
                   ConstantInt::get(Type::getInt64Ty(Ctx), TS), true);
    } else if (auto *MT = dyn_cast<MemTransferInst>(I)) {
      instrument(I, MT->getRawSource(), MT->getLength(), false);
      instrument(I, MT->getRawDest(), MT->getLength(), true);
    } else if (auto *MS = dyn_cast<MemSetInst>(I)) {
      instrument(I, MS->getRawDest(), MS->getLength(), true);
    }
  }

  for (AllocaInst *AI : Escaping)
    registerAlloca(AI);
  registerGlobals();
  return true;
}

// К объекту на стеке могут обратиться через указатель, где переменную уже
// не видно статически: сообщаем рантайму его адрес и размер, когда объект
// начинает жить. Объекты с непересекающимся временем жизни компилятор кладёт
// на одно место стека, поэтому регистрация при входе в функцию не годится:
// выиграл бы тот, кого зарегистрировали последним
void Instrumenter::registerAlloca(AllocaInst *AI) {
  SmallVector<Instruction *, 4> Starts;
  for (User *U : AI->users())
    if (auto *II = dyn_cast<IntrinsicInst>(U);
        II && II->getIntrinsicID() == Intrinsic::lifetime_start)
      Starts.push_back(II);

  if (!Starts.empty()) {
    for (Instruction *S : Starts)
      registerAllocaAt(AI, S->getNextNode());
  } else if (AI->isStaticAlloca()) {
    registerAllocaAt(AI, EntryPos.lookup(AI->getFunction()));
  } else {
    registerAllocaAt(AI, AI->getNextNode());
  }
}

void Instrumenter::registerAllocaAt(AllocaInst *AI, Instruction *Before) {
  IRBuilder<> B(Before);

  Value *Size;
  std::optional<TypeSize> TS = AI->getAllocationSize(DL);
  if (TS && !TS->isScalable()) {
    Size = B.getInt64(TS->getFixedValue());
  } else {
    // Массив переменной длины: число элементов известно только при выполнении
    Value *Count = B.CreateZExtOrTrunc(AI->getArraySize(), B.getInt64Ty());
    Size = B.CreateMul(
        Count, B.getInt64(DL.getTypeAllocSize(AI->getAllocatedType())));
  }
  B.CreateCall(RegisterStack, {AI, Size, varFor(AI)});
}

void Instrumenter::registerGlobals() {
  SmallVector<GlobalVariable *, 32> Globals;
  for (GlobalVariable &GV : M.globals()) {
    if (GV.isDeclaration() || GV.isThreadLocal() ||
        GV.getAddressSpace() != 0 || GV.hasAvailableExternallyLinkage() ||
        GV.getName().starts_with("llvm.") || GV.getName().starts_with("__ccs_"))
      continue;
    if (GV.getValueType()->isSized() &&
        DL.getTypeAllocSize(GV.getValueType()) > 0)
      Globals.push_back(&GV);
  }
  if (Globals.empty())
    return;

  Function *Ctor = Function::Create(
      FunctionType::get(Type::getVoidTy(Ctx), false),
      GlobalValue::InternalLinkage, "__ccs_register_globals", M);
  IRBuilder<> B(BasicBlock::Create(Ctx, "", Ctor));
  for (GlobalVariable *GV : Globals)
    B.CreateCall(RegisterGlobal,
                 {GV, B.getInt64(DL.getTypeAllocSize(GV->getValueType())),
                  varFor(GV)});
  B.CreateRetVoid();
  // Раньше пользовательских конструкторов: те уже могут обращаться к глобальным
  appendToGlobalCtors(M, Ctor, 1);
}

void Instrumenter::instrument(Instruction *Before, Value *Ptr, Value *Size,
                              bool IsWrite) {
  if (Ptr->getType()->getPointerAddressSpace() != 0)
    return;
  IRBuilder<> B(Before);
  Constant *Var = varFor(Ptr);
  B.CreateCall(Hook, {Ptr, B.CreateZExtOrTrunc(Size, B.getInt64Ty()),
                      B.getInt32(IsWrite),
                      Var ? Var : ConstantPointerNull::get(PtrTy)});
}

// Переменная, к которой относится адрес, если её видно статически.
// Указатели неизвестного происхождения (куча, аргументы) — nullptr.
Constant *Instrumenter::varFor(Value *Ptr) {
  const Value *Obj = getUnderlyingObject(Ptr, 0);
  auto It = Vars.find(Obj);
  if (It != Vars.end())
    return It->second;

  Constant *Var = nullptr;
  if (auto *GV = dyn_cast<GlobalVariable>(Obj)) {
    if (!GV->getName().starts_with("__ccs_"))
      Var = makeVar(globalName(GV));
  } else if (auto *AI = dyn_cast<AllocaInst>(Obj)) {
    Var = makeVar(allocaName(const_cast<AllocaInst *>(AI)));
  }
  Vars[Obj] = Var;
  return Var;
}

Constant *Instrumenter::makeVar(StringRef Name) {
  Constant *Str = ConstantDataArray::getString(Ctx, Name);
  auto *NameGV = new GlobalVariable(M, Str->getType(), true,
                                    GlobalValue::PrivateLinkage, Str,
                                    "__ccs_name");
  NameGV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  Constant *Init = ConstantStruct::get(
      VarTy, {NameGV, ConstantInt::get(Type::getInt32Ty(Ctx), 0)});
  return new GlobalVariable(M, VarTy, false, GlobalValue::PrivateLinkage, Init,
                            "__ccs_var");
}

struct CacheSimPass : PassInfoMixin<CacheSimPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    return Instrumenter(M).run() ? PreservedAnalyses::none()
                                 : PreservedAnalyses::all();
  }
  static bool isRequired() { return true; }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "CacheSimPass", "0.1", [](PassBuilder &PB) {
            PB.registerPipelineStartEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel Level) {
                  if (Stage != "early")
                    return;
                  // Без SROA скаляры вроде счётчиков циклов ещё лежат в памяти
                  if (Level != OptimizationLevel::O0)
                    MPM.addPass(createModuleToFunctionPassAdaptor(
                        SROAPass(SROAOptions::ModifyCFG)));
                  MPM.addPass(CacheSimPass());
                });
            PB.registerOptimizerLastEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel,
                   ThinOrFullLTOPhase) {
                  if (Stage == "late")
                    MPM.addPass(CacheSimPass());
                });
          }};
}
