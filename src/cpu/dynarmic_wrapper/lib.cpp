/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <cstdint>
#include <cstdio>
#include <array>
#include <optional>
#include <memory>

#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/interface/A32/config.h"
#include "dynarmic/interface/A32/coprocessor.h"
#include "dynarmic/interface/A64/a64.h"
#include "dynarmic/interface/A64/config.h"
#include "dynarmic/interface/exclusive_monitor.h"

namespace touchHLE::cpu {

// ======================== A32 (original) ========================

using VAddr32 = std::uint32_t;

// Types and functions defined in Rust (A32)
extern "C" {
struct touchHLE_Mem;
std::uint8_t touchHLE_cpu_read_u8(touchHLE_Mem *mem, VAddr32 addr, bool *error);
std::uint16_t touchHLE_cpu_read_u16(touchHLE_Mem *mem, VAddr32 addr, bool *error);
std::uint32_t touchHLE_cpu_read_u32(touchHLE_Mem *mem, VAddr32 addr, bool *error);
std::uint64_t touchHLE_cpu_read_u64(touchHLE_Mem *mem, VAddr32 addr, bool *error);
bool touchHLE_cpu_write_u8(touchHLE_Mem *mem, VAddr32 addr, std::uint8_t value);
bool touchHLE_cpu_write_u16(touchHLE_Mem *mem, VAddr32 addr, std::uint16_t value);
bool touchHLE_cpu_write_u32(touchHLE_Mem *mem, VAddr32 addr, std::uint32_t value);
bool touchHLE_cpu_write_u64(touchHLE_Mem *mem, VAddr32 addr, std::uint64_t value);

struct touchHLE_DynarmicContext {
  std::array<std::uint32_t, 16> regs;
  std::array<std::uint32_t, 64> extregs;
  std::uint32_t cpsr;
  std::uint32_t fpscr;
};
}

const auto HaltReasonSvc = Dynarmic::HaltReason::UserDefined1;
const auto HaltReasonUndefinedInstruction = Dynarmic::HaltReason::UserDefined2;
const auto HaltReasonBreakpoint = Dynarmic::HaltReason::UserDefined3;

class Environment final : public Dynarmic::A32::UserCallbacks {
public:
  Dynarmic::A32::Jit *cpu = nullptr;
  touchHLE_Mem *mem = nullptr;
  std::uint64_t ticks_remaining;
  uint32_t halting_svc;

private:
  std::uint8_t MemoryRead8(VAddr32 vaddr) override {
    bool error;
    auto value = touchHLE_cpu_read_u8(mem, vaddr, &error);
    if (error) {
      cpu->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    }
    return value;
  }
  std::uint16_t MemoryRead16(VAddr32 vaddr) override {
    bool error;
    auto value = touchHLE_cpu_read_u16(mem, vaddr, &error);
    if (error) {
      cpu->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    }
    return value;
  }
  std::uint32_t MemoryRead32(VAddr32 vaddr) override {
    bool error;
    auto value = touchHLE_cpu_read_u32(mem, vaddr, &error);
    if (error) {
      cpu->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    }
    return value;
  }
  std::uint64_t MemoryRead64(VAddr32 vaddr) override {
    bool error;
    auto value = touchHLE_cpu_read_u64(mem, vaddr, &error);
    if (error) {
      cpu->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    }
    return value;
  }

  std::optional<std::uint32_t> MemoryReadCode(VAddr32 vaddr) override {
    bool error;
    auto value = touchHLE_cpu_read_u32(mem, vaddr, &error);
    if (error) {
      return std::nullopt;
    } else {
      return value;
    }
  }

  void MemoryWrite8(VAddr32 vaddr, std::uint8_t value) override {
    if (touchHLE_cpu_write_u8(mem, vaddr, value)) {
      cpu->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    }
  }
  void MemoryWrite16(VAddr32 vaddr, std::uint16_t value) override {
    if (touchHLE_cpu_write_u16(mem, vaddr, value)) {
      cpu->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    }
  }
  void MemoryWrite32(VAddr32 vaddr, std::uint32_t value) override {
    if (touchHLE_cpu_write_u32(mem, vaddr, value)) {
      cpu->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    }
  }
  void MemoryWrite64(VAddr32 vaddr, std::uint64_t value) override {
    if (touchHLE_cpu_write_u64(mem, vaddr, value)) {
      cpu->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    }
  }

  bool MemoryWriteExclusive8(VAddr32 addr, std::uint8_t value, std::uint8_t expected) override {
    if (MemoryRead8(addr) != expected) return false;
    MemoryWrite8(addr, value);
    return true;
  }
  bool MemoryWriteExclusive16(VAddr32 addr, std::uint16_t value, std::uint16_t expected) override {
    if (MemoryRead16(addr) != expected) return false;
    MemoryWrite16(addr, value);
    return true;
  }
  bool MemoryWriteExclusive32(VAddr32 addr, std::uint32_t value, std::uint32_t expected) override {
    if (MemoryRead32(addr) != expected) return false;
    MemoryWrite32(addr, value);
    return true;
  }
  bool MemoryWriteExclusive64(VAddr32 addr, std::uint64_t value, std::uint64_t expected) override {
    if (MemoryRead64(addr) != expected) return false;
    MemoryWrite64(addr, value);
    return true;
  }

  void InterpreterFallback(std::uint32_t, size_t) override {
    abort(); // TODO
  }
  void CallSVC(std::uint32_t svc) override {
    halting_svc = svc;
    cpu->HaltExecution(HaltReasonSvc);
  }
  void ExceptionRaised(VAddr32 pc, Dynarmic::A32::Exception exception) override {
    if (exception == Dynarmic::A32::Exception::NoExecuteFault) {
      cpu->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    } else if (exception == Dynarmic::A32::Exception::UndefinedInstruction) {
      cpu->HaltExecution(HaltReasonUndefinedInstruction);
    } else if (exception == Dynarmic::A32::Exception::Breakpoint) {
      cpu->HaltExecution(HaltReasonBreakpoint);
    } else {
      std::fprintf(stderr, "ExceptionRaised (A32): unexpected exception %u at %x\n",
                   unsigned(exception), pc);
      abort();
    }
  }
  void AddTicks(std::uint64_t ticks) override {
    if (ticks > ticks_remaining) {
      ticks_remaining = 0;
      return;
    }
    ticks_remaining -= ticks;
  }
  std::uint64_t GetTicksRemaining() override { return ticks_remaining; }
};

class ArmDynarmicCP15 : public Dynarmic::A32::Coprocessor {
  static std::uint64_t Ignore(void *, std::uint32_t, std::uint32_t) {
    return 0;
  }

  static Callback CallbackForIgnoredOperation() {
    return Callback{&Ignore, std::nullopt};
  }

public:
  using CoprocReg = Dynarmic::A32::CoprocReg;

  CallbackOrAccessOneWord CompileSendOneWord(bool, unsigned, CoprocReg, CoprocReg, unsigned) override {
    return CallbackForIgnoredOperation();
  }
  std::optional<Callback> CompileInternalOperation(bool, unsigned, CoprocReg, CoprocReg, CoprocReg, unsigned) override {
    return CallbackForIgnoredOperation();
  }
  CallbackOrAccessTwoWords CompileSendTwoWords(bool, unsigned, CoprocReg) override {
    return CallbackForIgnoredOperation();
  }
  CallbackOrAccessOneWord CompileGetOneWord(bool, unsigned, CoprocReg, CoprocReg, unsigned) override {
    return CallbackForIgnoredOperation();
  }
  CallbackOrAccessTwoWords CompileGetTwoWords(bool, unsigned, CoprocReg) override {
    return CallbackForIgnoredOperation();
  }
  std::optional<Callback> CompileLoadWords(bool, bool, CoprocReg, std::optional<std::uint8_t>) override {
    return CallbackForIgnoredOperation();
  }
  std::optional<Callback> CompileStoreWords(bool, bool, CoprocReg, std::optional<std::uint8_t>) override {
    return CallbackForIgnoredOperation();
  }
};

class DynarmicWrapper {
  Environment env;
  std::unique_ptr<Dynarmic::A32::Jit> cpu;
  std::unique_ptr<Dynarmic::ExclusiveMonitor> mon;
  std::array<std::uint8_t *, Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES> page_table;

public:
  DynarmicWrapper(void *direct_memory_access_ptr, size_t null_page_count) {
    Dynarmic::A32::UserConfig user_config;
    user_config.callbacks = &env;
    user_config.coprocessors[15] = std::make_shared<ArmDynarmicCP15>();
    mon = std::make_unique<Dynarmic::ExclusiveMonitor>(1);
    user_config.global_monitor = mon.get();
#ifndef NDEBUG
    user_config.check_halt_on_memory_access = true;
#endif
    if (direct_memory_access_ptr) {
      page_table.fill((std::uint8_t *)direct_memory_access_ptr);
      static_assert(1 << Dynarmic::A32::UserConfig::PAGE_BITS == 0x1000);

      if (null_page_count > page_table.size()) {
        printf("Too many null pages, %zu requested but maximum is %zu.",
               null_page_count, page_table.size());
        abort();
      }
      for (size_t i = 0; i < null_page_count; i++) {
        page_table[i] = nullptr;
      }
      user_config.page_table = &page_table;
      user_config.absolute_offset_page_table = true;
    }
    cpu = std::make_unique<Dynarmic::A32::Jit>(user_config);
    env.cpu = cpu.get();
  }

  const std::uint32_t *regs() const { return &cpu->Regs().front(); }
  std::uint32_t *regs() { return &cpu->Regs().front(); }

  std::uint32_t cpsr() const { return cpu->Cpsr(); }
  void set_cpsr(std::uint32_t cpsr) { cpu->SetCpsr(cpsr); }

  void invalidate_cache_range(VAddr32 start, std::uint32_t size) {
    cpu->InvalidateCacheRange(start, size);
  }

  void swap_context(touchHLE_DynarmicContext *context) {
    touchHLE_DynarmicContext tmp = {cpu->Regs(), cpu->ExtRegs(), cpu->Cpsr(), cpu->Fpscr()};
    cpu->Regs() = context->regs;
    cpu->ExtRegs() = context->extregs;
    cpu->SetCpsr(context->cpsr);
    cpu->SetFpscr(context->fpscr);
    *context = tmp;
  }

  std::int32_t run_or_step(touchHLE_Mem *mem, std::uint64_t *ticks) {
    env.mem = mem;
    Dynarmic::HaltReason hr;
    if (ticks) {
      env.ticks_remaining = *ticks;
      hr = cpu->Run();
    } else {
      hr = cpu->Step();
    }
    std::int32_t res;
    if ((!hr && ticks) || (hr == Dynarmic::HaltReason::Step && !ticks)) {
      res = -1;
    } else if (Dynarmic::Has(hr, Dynarmic::HaltReason::MemoryAbort)) {
      res = -2;
    } else if (Dynarmic::Has(hr, HaltReasonUndefinedInstruction)) {
      res = -3;
    } else if (Dynarmic::Has(hr, HaltReasonBreakpoint)) {
      res = -4;
    } else if (Dynarmic::Has(hr, HaltReasonSvc)) {
      res = std::int32_t(env.halting_svc);
    } else {
      printf("unhandled halt reason (A32) %u\n", unsigned(hr));
      abort();
    }
    env.mem = nullptr;
    if (ticks) {
      *ticks = env.ticks_remaining;
    }
    return res;
  }
};

// ======================== A64 (nuevo) ========================

using VAddr64 = std::uint64_t;
using Vector = Dynarmic::A64::Vector;

class Environment64 final : public Dynarmic::A64::UserCallbacks {
public:
  Dynarmic::A64::Jit *cpu = nullptr;
  touchHLE_Mem *mem = nullptr;
  std::uint64_t ticks_remaining = 0;
  uint32_t halting_svc = 0;

private:
  // Nota: por ahora casteamos a VAddr32 porque las funciones de Rust todavía son de 32-bit.
  // Más adelante hay que actualizar el lado de Rust para aceptar u64.
  std::uint8_t MemoryRead8(VAddr64 vaddr) override {
    bool error = false;
    auto value = touchHLE_cpu_read_u8(mem, static_cast<VAddr32>(vaddr), &error);
    if (error) cpu->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    return value;
  }
  std::uint16_t MemoryRead16(VAddr64 vaddr) override {
    bool error = false;
    auto value = touchHLE_cpu_read_u16(mem, static_cast<VAddr32>(vaddr), &error);
    if (error) cpu->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    return value;
  }
  std::uint32_t MemoryRead32(VAddr64 vaddr) override {
    bool error = false;
    auto value = touchHLE_cpu_read_u32(mem, static_cast<VAddr32>(vaddr), &error);
    if (error) cpu->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    return value;
  }
  std::uint64_t MemoryRead64(VAddr64 vaddr) override {
    bool error = false;
    auto value = touchHLE_cpu_read_u64(mem, static_cast<VAddr32>(vaddr), &error);
    if (error) cpu->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    return value;
  }
  Vector MemoryRead128(VAddr64 vaddr) override {
    return {MemoryRead64(vaddr), MemoryRead64(vaddr + 8)};
  }

  std::optional<std::uint32_t> MemoryReadCode(VAddr64 vaddr) override {
    bool error = false;
    auto value = touchHLE_cpu_read_u32(mem, static_cast<VAddr32>(vaddr), &error);
    if (error) return std::nullopt;
    return value;
  }

  void MemoryWrite8(VAddr64 vaddr, std::uint8_t value) override {
    if (touchHLE_cpu_write_u8(mem, static_cast<VAddr32>(vaddr), value))
      cpu->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
  }
  void MemoryWrite16(VAddr64 vaddr, std::uint16_t value) override {
    if (touchHLE_cpu_write_u16(mem, static_cast<VAddr32>(vaddr), value))
      cpu->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
  }
  void MemoryWrite32(VAddr64 vaddr, std::uint32_t value) override {
    if (touchHLE_cpu_write_u32(mem, static_cast<VAddr32>(vaddr), value))
      cpu->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
  }
  void MemoryWrite64(VAddr64 vaddr, std::uint64_t value) override {
    if (touchHLE_cpu_write_u64(mem, static_cast<VAddr32>(vaddr), value))
      cpu->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
  }
  void MemoryWrite128(VAddr64 vaddr, Vector value) override {
    MemoryWrite64(vaddr, value[0]);
    MemoryWrite64(vaddr + 8, value[1]);
  }

  bool MemoryWriteExclusive8(VAddr64 addr, std::uint8_t value, std::uint8_t expected) override {
    if (MemoryRead8(addr) != expected) return false;
    MemoryWrite8(addr, value);
    return true;
  }
  bool MemoryWriteExclusive16(VAddr64 addr, std::uint16_t value, std::uint16_t expected) override {
    if (MemoryRead16(addr) != expected) return false;
    MemoryWrite16(addr, value);
    return true;
  }
  bool MemoryWriteExclusive32(VAddr64 addr, std::uint32_t value, std::uint32_t expected) override {
    if (MemoryRead32(addr) != expected) return false;
    MemoryWrite32(addr, value);
    return true;
  }
  bool MemoryWriteExclusive64(VAddr64 addr, std::uint64_t value, std::uint64_t expected) override {
    if (MemoryRead64(addr) != expected) return false;
    MemoryWrite64(addr, value);
    return true;
  }
  bool MemoryWriteExclusive128(VAddr64 addr, Vector value, Vector expected) override {
    if (MemoryRead128(addr) != expected) return false;
    MemoryWrite128(addr, value);
    return true;
  }

  void InterpreterFallback(VAddr64, size_t) override {
    abort(); // TODO
  }

  void CallSVC(std::uint32_t svc) override {
    halting_svc = svc;
    cpu->HaltExecution(HaltReasonSvc);
  }

  void ExceptionRaised(VAddr64 pc, Dynarmic::A64::Exception exception) override {
    if (exception == Dynarmic::A64::Exception::NoExecuteFault) {
      cpu->HaltExecution(Dynarmic::HaltReason::MemoryAbort);
    } else if (exception == Dynarmic::A64::Exception::UnallocatedEncoding ||
               exception == Dynarmic::A64::Exception::ReservedValue) {
      cpu->HaltExecution(HaltReasonUndefinedInstruction);
    } else if (exception == Dynarmic::A64::Exception::Breakpoint) {
      cpu->HaltExecution(HaltReasonBreakpoint);
    } else {
      std::fprintf(stderr, "ExceptionRaised (A64): unexpected exception %u at %llx\n",
                   unsigned(exception), (unsigned long long)pc);
      abort();
    }
  }

  void AddTicks(std::uint64_t ticks) override {
    if (ticks > ticks_remaining) {
      ticks_remaining = 0;
      return;
    }
    ticks_remaining -= ticks;
  }

  std::uint64_t GetTicksRemaining() override { return ticks_remaining; }

  std::uint64_t GetCNTPCT() override {
    return 0; // TODO
  }
};

class DynarmicWrapper64 {
  Environment64 env;
  std::unique_ptr<Dynarmic::A64::Jit> cpu;
  std::unique_ptr<Dynarmic::ExclusiveMonitor> mon;

public:
  DynarmicWrapper64(void * /*direct_memory_access_ptr*/, size_t /*null_page_count*/) {
    Dynarmic::A64::UserConfig user_config;
    user_config.callbacks = &env;
    mon = std::make_unique<Dynarmic::ExclusiveMonitor>(1);
    user_config.global_monitor = mon.get();

#ifndef NDEBUG
    user_config.check_halt_on_memory_access = true;
#endif

    // TODO: page_table / fastmem para A64
    cpu = std::make_unique<Dynarmic::A64::Jit>(user_config);
    env.cpu = cpu.get();
  }

  std::uint64_t GetReg(size_t index) const { return cpu->GetRegister(index); }
  void SetReg(size_t index, std::uint64_t value) { cpu->SetRegister(index, value); }

  std::uint64_t GetPC() const { return cpu->GetPC(); }
  void SetPC(std::uint64_t pc) { cpu->SetPC(pc); }

  std::uint64_t GetSP() const { return cpu->GetSP(); }
  void SetSP(std::uint64_t sp) { cpu->SetSP(sp); }

  void invalidate_cache_range(VAddr64 start, std::uint64_t size) {
    cpu->InvalidateCacheRange(start, size);
  }

  std::int32_t run_or_step(touchHLE_Mem *mem, std::uint64_t *ticks) {
    env.mem = mem;
    Dynarmic::HaltReason hr;
    if (ticks) {
      env.ticks_remaining = *ticks;
      hr = cpu->Run();
    } else {
      hr = cpu->Step();
    }

    std::int32_t res;
    if ((!hr && ticks) || (hr == Dynarmic::HaltReason::Step && !ticks)) {
      res = -1;
    } else if (Dynarmic::Has(hr, Dynarmic::HaltReason::MemoryAbort)) {
      res = -2;
    } else if (Dynarmic::Has(hr, HaltReasonUndefinedInstruction)) {
      res = -3;
    } else if (Dynarmic::Has(hr, HaltReasonBreakpoint)) {
      res = -4;
    } else if (Dynarmic::Has(hr, HaltReasonSvc)) {
      res = std::int32_t(env.halting_svc);
    } else {
      printf("unhandled halt reason (A64) %u\n", unsigned(hr));
      abort();
    }

    env.mem = nullptr;
    if (ticks) *ticks = env.ticks_remaining;
    return res;
  }
};

// ======================== Exports C ========================

extern "C" {

// ----- A32 (los originales, no tocar nombres) -----
DynarmicWrapper *touchHLE_DynarmicWrapper_new(void *direct_memory_access_ptr, size_t null_page_count) {
  return new DynarmicWrapper(direct_memory_access_ptr, null_page_count);
}
void touchHLE_DynarmicWrapper_delete(DynarmicWrapper *cpu) { delete cpu; }

const std::uint32_t *touchHLE_DynarmicWrapper_regs_const(const DynarmicWrapper *cpu) {
  return cpu->regs();
}
std::uint32_t *touchHLE_DynarmicWrapper_regs_mut(DynarmicWrapper *cpu) {
  return cpu->regs();
}

std::uint32_t touchHLE_DynarmicWrapper_cpsr(const DynarmicWrapper *cpu) {
  return cpu->cpsr();
}
void touchHLE_DynarmicWrapper_set_cpsr(DynarmicWrapper *cpu, std::uint32_t cpsr) {
  cpu->set_cpsr(cpsr);
}

void touchHLE_DynarmicWrapper_swap_context(DynarmicWrapper *cpu, touchHLE_DynarmicContext *context) {
  cpu->swap_context(context);
}

void touchHLE_DynarmicWrapper_invalidate_cache_range(DynarmicWrapper *cpu, VAddr32 start, std::uint32_t size) {
  cpu->invalidate_cache_range(start, size);
}

std::int32_t touchHLE_DynarmicWrapper_run_or_step(DynarmicWrapper *cpu, touchHLE_Mem *mem, std::uint64_t *ticks) {
  return cpu->run_or_step(mem, ticks);
}

// ----- A64 (nuevos nombres) -----
DynarmicWrapper64 *touchHLE_DynarmicWrapper64_new(void *direct_memory_access_ptr, size_t null_page_count) {
  return new DynarmicWrapper64(direct_memory_access_ptr, null_page_count);
}
void touchHLE_DynarmicWrapper64_delete(DynarmicWrapper64 *cpu) { delete cpu; }

std::uint64_t touchHLE_DynarmicWrapper64_get_reg(
    const DynarmicWrapper64 *cpu,
    std::size_t index
) {
    return cpu->GetReg(index);
}

void touchHLE_DynarmicWrapper64_set_reg(
    DynarmicWrapper64 *cpu,
    std::size_t index,
    std::uint64_t value
) {
    cpu->SetReg(index, value);
}

std::uint64_t touchHLE_DynarmicWrapper64_get_pc(
    const DynarmicWrapper64 *cpu
) {
    return cpu->GetPC();
}

void touchHLE_DynarmicWrapper64_set_pc(
    DynarmicWrapper64 *cpu,
    std::uint64_t pc
) {
    cpu->SetPC(pc);
}

std::uint64_t touchHLE_DynarmicWrapper64_get_sp(
    const DynarmicWrapper64 *cpu
) {
    return cpu->GetSP();
}

void touchHLE_DynarmicWrapper64_set_sp(
    DynarmicWrapper64 *cpu,
    std::uint64_t sp
) {
    cpu->SetSP(sp);
}

std::int32_t touchHLE_DynarmicWrapper64_run_or_step(DynarmicWrapper64 *cpu, touchHLE_Mem *mem, std::uint64_t *ticks) {
  return cpu->run_or_step(mem, ticks);
}

void touchHLE_DynarmicWrapper64_invalidate_cache_range(DynarmicWrapper64 *cpu, VAddr64 start, std::uint64_t size) {
  cpu->invalidate_cache_range(start, size);
}

} // extern "C"

} // namespace touchHLE::cpu
