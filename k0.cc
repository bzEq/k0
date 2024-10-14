#include <assert.h>
#include <iostream>
#include <map>
#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <string_view>
#include <vector>

namespace k0::core {

// Instructions:
// alloca <reg> <num>
// imm <reg> <num>
// add <reg> <reg> <reg>
// cmp <reg> <num> <reg> <reg>
// br <reg> <num> <num>
// call <addr>
// ret
// copy <reg> <reg>
// load <reg> <reg>
// store <reg> <reg>
// debug <reg>

enum OP {
  ALLOCA,
  IMM,
  ADD,
  CMP,
  BR,
  CALL,
  RET,
  COPY,
  LOAD,
  STORE,
  DEBUG,
};

enum COND {
  LT = -1,
  EQ = 0,
  GT = 1,
};

struct Instruction {
  uint8_t op;
  int64_t get(size_t i) const { return operand[i]; }
  std::vector<int64_t> operand;
};

struct BasicBlock {
  std::vector<Instruction> body_;
};

struct Function {
  std::string name_;
  std::string_view name() const { return name_; }
  int64_t entry() const { return entry_; }
  int64_t entry_ = 0;
  std::map<int64_t, BasicBlock> basic_blocks_;
};

struct PC {
  Function *f;
  int64_t ib;
  size_t ii;
};

struct Alloca {
  void *base = nullptr;
  size_t size;
  ~Alloca() {
    if (base)
      free(base);
  }
};

struct FunctionContext {
  std::map<int64_t, std::unique_ptr<Alloca>> allocas;
  std::map<int64_t, int64_t> values;
  void Assign(int64_t reg, int64_t val) { values[reg] = val; }
  int64_t GetVal(int64_t reg) { return values[reg]; }
  void Allocate(int64_t reg, size_t sz) {
    auto alloca =
        std::unique_ptr<Alloca>(new Alloca{.base = malloc(sz), .size = sz});
    Assign(reg, (int64_t)alloca->base);
    allocas.insert({reg, std::move(alloca)});
  }
  PC pc;
};

class ExecutionEngine {
public:
  void ExecuteEntry(Function &f) {
    stack_.emplace_back();
    FunctionContext &target = stack_.back();
    target.pc.f = &f;
    target.pc.ib = f.entry();
    target.pc.ii = 0;
    Execute();
  }

private:
  bool stopping_;
  PC pc_;
  std::vector<FunctionContext> stack_;

  void Execute() {
    while (!stack_.empty())
      ExecuteInstruction();
  }

  void ExecuteInstruction() {
    FunctionContext &ctx = stack_.back();
    PC &pc = ctx.pc;
    Instruction &i = pc.f->basic_blocks_[pc.ib].body_[pc.ii++];
    switch (i.op) {
    case ALLOCA: {
      int64_t reg = i.get(0);
      size_t size = i.get(1);
      ctx.Allocate(reg, size);
      break;
    }
    case IMM: {
      int64_t reg = i.get(0);
      int64_t val = i.get(1);
      ctx.Assign(reg, val);
      break;
    }
    case ADD: {
      int64_t dst_reg = i.get(0);
      int64_t op0_val = ctx.GetVal(i.get(1));
      int64_t op1_val = ctx.GetVal(i.get(2));
      ctx.Assign(dst_reg, op0_val + op1_val);
      break;
    }
    case CMP: {
      int64_t dst_reg = i.get(0);
      int64_t cond = i.get(1);
      int64_t op0_val = ctx.GetVal(i.get(2));
      int64_t op1_val = ctx.GetVal(i.get(3));
      int64_t flag = op0_val == op1_val ? EQ : (op0_val < op1_val ? LT : GT);
      ctx.Assign(dst_reg, flag == cond);
      break;
    }
    case COPY: {
      int64_t src_val = ctx.GetVal(i.get(1));
      ctx.Assign(i.get(0), src_val);
      break;
    }
    // FIXME: Unsafe memory ld/st.
    case LOAD: {
      int64_t dst_reg = i.get(0);
      int64_t *src = reinterpret_cast<int64_t *>(ctx.GetVal(i.get(1)));
      ctx.Assign(dst_reg, *src);
      break;
    }
    case STORE: {
      int64_t *dst = reinterpret_cast<int64_t *>(ctx.GetVal(i.get(1)));
      int64_t src_val = ctx.GetVal(i.get(0));
      *dst = src_val;
      break;
    }
    case BR: {
      int64_t flag_val = ctx.GetVal(i.get(0));
      pc.ii = 0;
      if (flag_val) {
        pc.ib = i.get(1);
      } else {
        pc.ib = i.get(2);
      }
      break;
    }
    case CALL: {
      Function *f = reinterpret_cast<Function *>(i.get(0));
      assert(f && "Unable to resolve function");
      stack_.emplace_back();
      FunctionContext &target = stack_.back();
      target.pc.f = f;
      target.pc.ib = f->entry();
      target.pc.ii = 0;
      break;
    }
    case RET: {
      stack_.pop_back();
      break;
    }
    case DEBUG: {
      std::cerr << ctx.GetVal(i.get(0)) << std::endl;
      break;
    }
    default:
      assert(false && "Unsupported opcode");
    }
  }
};

} // namespace k0::core

int main() {
  using namespace k0::core;
  Function F;
  BasicBlock &entry = F.basic_blocks_[0];
  Function Callee;
  BasicBlock &callee_entry = Callee.basic_blocks_[0];
  entry.body_.emplace_back(Instruction{IMM, {1, 4096}});
  entry.body_.emplace_back(Instruction{DEBUG, {1, 4096}});
  callee_entry.body_.emplace_back(Instruction{RET});
  entry.body_.emplace_back(Instruction{IMM, {1, 1024}});
  entry.body_.emplace_back(
      Instruction{CALL, {*(reinterpret_cast<int64_t *>(&Callee))}});
  entry.body_.emplace_back(Instruction{IMM, {2, -1024}});
  entry.body_.emplace_back(Instruction{DEBUG, {3}});
  entry.body_.emplace_back(Instruction{RET});
  ExecutionEngine EE;
  EE.ExecuteEntry(F);
  return 0;
}
