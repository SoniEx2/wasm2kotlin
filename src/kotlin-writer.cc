/*
 * Copyright 2020-2021 Soni L.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Based on wasm2c, under the following license notice:
 *
 * Copyright 2017 WebAssembly Community Group participants
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/kotlin-writer.h"

#include <cctype>
#include <cinttypes>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "src/cast.h"
#include "src/common.h"
#include "src/ir.h"
#include "src/literal.h"
#include "src/set-util.h"
#include "src/stream.h"
#include "src/string-util.h"

#define INDENT_SIZE 2

#define UNIMPLEMENTED(x) printf("unimplemented: %s\n", (x)), abort()

#define WASM_RT_PKG "wasm_rt_impl"

extern const char* s_source_includes;
extern const char* s_source_inner;

namespace wabt {

namespace {

struct Label {
  Label(LabelType label_type,
        const std::string& name,
        const TypeVector& sig,
        size_t type_stack_size,
        size_t try_catch_stack_size,
        bool used = false)
      : label_type(label_type),
        name(name),
        sig(sig),
        type_stack_size(type_stack_size),
        try_catch_stack_size(try_catch_stack_size),
        used(used) {}

  bool HasValue() const { return !sig.empty(); }

  LabelType label_type;
  const std::string& name;
  const TypeVector& sig;
  size_t type_stack_size;
  size_t try_catch_stack_size;
  bool used = false;
};

template <int>
struct Name {
  explicit Name(const std::string& name) : name(name) {}
  const std::string& name;
};

typedef Name<0> LocalName;
typedef Name<1> GlobalName;
typedef Name<2> ExternalPtr;

struct GotoLabel {
  explicit GotoLabel(const Var& var) : var(var) {}
  const Var& var;
};

struct LabelDecl {
  explicit LabelDecl(const std::string& name) : name(name) {}
  std::string name;
};

struct GlobalVar {
  explicit GlobalVar(const Var& var) : var(var) {}
  const Var& var;
};

struct StackVar {
  explicit StackVar(Index index, Type type = Type::Any)
      : index(index), type(type) {}
  Index index;
  Type type;
};

struct StackVarValue {
  explicit StackVarValue(Index index, Type type = Type::Any)
      : index(index), type(type) {}
  Index index;
  Type type;
};

struct TypeEnum {
  explicit TypeEnum(Type type) : type(type) {}
  Type type;
};

struct ResultType {
  explicit ResultType(const TypeVector& types) : types(types) {}
  const TypeVector& types;
};

struct TryCatchLabel {
  TryCatchLabel(const std::string& name, size_t try_catch_stack_size)
      : name(name), try_catch_stack_size(try_catch_stack_size) {}
  std::string name;
  size_t try_catch_stack_size;
};

struct Newline {};
struct OpenBrace {};
struct CloseBrace {};

struct SideEffects {
  std::set<std::string> updates_locals;
  std::set<std::string> updates_globals;
  bool updates_memory = false;
  bool can_trap = false;

  bool empty() const {
    return !can_trap && !updates_memory && updates_locals.empty() &&
           updates_globals.empty();
  }

  void clear() {
    updates_locals.clear();
    updates_globals.clear();
    updates_memory = false;
    can_trap = false;
  }

  SideEffects& operator|=(const SideEffects& rhs) {
    updates_locals.insert(rhs.updates_locals.begin(), rhs.updates_locals.end());
    updates_globals.insert(rhs.updates_globals.begin(),
                           rhs.updates_globals.end());
    updates_memory = updates_memory || rhs.updates_memory;
    can_trap = can_trap || rhs.can_trap;
    return *this;
  }

  friend SideEffects operator|(SideEffects lhs, const SideEffects& rhs) {
    lhs |= rhs;
    return lhs;
  }
};

struct DependsOn {
  std::set<std::string> depends_locals;
  std::set<std::string> depends_globals;
  bool depends_memory = false;

  bool empty() const {
    return !depends_memory && depends_locals.empty() && depends_globals.empty();
  }

  void clear() {
    depends_locals.clear();
    depends_globals.clear();
    depends_memory = false;
  }

  DependsOn& operator|=(const DependsOn& rhs) {
    depends_locals.insert(rhs.depends_locals.begin(), rhs.depends_locals.end());
    depends_globals.insert(rhs.depends_globals.begin(),
                           rhs.depends_globals.end());
    depends_memory = depends_memory || rhs.depends_memory;
    return *this;
  }

  friend DependsOn operator|(DependsOn lhs, const DependsOn& rhs) {
    lhs |= rhs;
    return lhs;
  }
};

struct StackValue {
  std::string value;
  uint8_t precedence = 0;
  DependsOn depends_on;
  SideEffects side_effects;

  bool InvalidatedBy(const SideEffects& effects) const {
    return (effects.can_trap && !side_effects.empty()) ||
           (effects.updates_memory && depends_on.depends_memory) ||
           SetsOverlap(effects.updates_locals, depends_on.depends_locals) ||
           SetsOverlap(effects.updates_globals, depends_on.depends_globals);
  }

  bool RequiredFor(const DependsOn& requirements,
                   const SideEffects& effects) const {
    return (side_effects.can_trap && !effects.empty()) ||
           (side_effects.updates_memory && requirements.depends_memory) ||
           SetsOverlap(side_effects.updates_locals,
                       requirements.depends_locals) ||
           SetsOverlap(side_effects.updates_globals,
                       requirements.depends_globals);
  }
};

class KotlinWriter {
 public:
  KotlinWriter(Stream* kotlin_stream,
               const char* class_name,
               const char* package_name,
               const WriteKotlinOptions& options)
      : options_(options),
        kotlin_stream_(kotlin_stream),
        class_name_(class_name),
        package_name_(package_name) {}

  Result WriteModule(const Module&);

 private:
  typedef std::set<std::string> SymbolSet;
  typedef std::map<std::string, std::string> SymbolMap;
  typedef std::pair<Index, Type> StackTypePair;
  typedef std::map<StackTypePair, std::string> StackVarSymbolMap;
  typedef std::map<Index, FuncDeclaration> CallIndirectDeclMap;

  void UseStream(Stream*);

  void WriteKotlinSource();

  size_t MarkTypeStack() const;
  void ResetTypeStack(size_t mark);
  Type StackType(Index) const;
  void PushType(Type);
  void PushTypes(const TypeVector&);
  void DropTypes(size_t count);
  void PushValue(StackValue);
  const StackValue& GetValue(Index index = 0) const;
  void PushVar();
  StackValue PopValue();
  std::vector<StackValue> PopValues(size_t count);
  void PushValues(std::vector<StackValue> values);
  void DropValue();
  void SpillValues();

  void PushLabel(LabelType,
                 const std::string& name,
                 const FuncSignature&,
                 bool used = false);
  const Label* FindLabel(const Var& var, bool mark_used = true);
  void PopLabel();

  static std::string AddressOf(const std::string&, const std::string&);

  static char MangleType(Type);
  static std::string MangleName(std::string_view);
  static std::string LegalizeName(std::string_view);
  static std::string ExportName(std::string_view mangled_name);
  std::string DefineName(SymbolSet*, std::string_view);
  std::string DefineImportName(const std::string& name,
                               std::string_view module_name,
                               std::string_view mangled_field_name);
  std::string DefineGlobalScopeName(const std::string&);
  std::string DefineLocalScopeName(const std::string&);
  std::string DefineStackVarName(Index, Type, std::string_view);
  void DefineCallIndirect(Index, const FuncDeclaration&);

  void Indent(int size = INDENT_SIZE);
  void Dedent(int size = INDENT_SIZE);
  void WriteIndent();
  void WriteData(const void* src, size_t size);
  void Writef(const char* format, ...);

  void WriteValueData(const void* src, size_t size);
  void WriteValuef(const char* format, ...);

  template <typename T, typename U, typename... Args>
  void WriteValue(T&& t, U&& u, Args&&... args) {
    WriteValue(std::forward<T>(t));
    WriteValue(std::forward<U>(u));
    WriteValue(std::forward<Args>(args)...);
  }

  void WriteValue() {}
  void WriteValue(std::string_view);
  void WriteValue(const Const&);
  void WriteValue(const GlobalVar&);
  void WriteValue(const Var&);
  void WriteValue(const LocalName&);
  void WriteValue(const GlobalName&);
  void WriteValue(const StackVar&);
  void WriteValue(Type);

  template <typename T, typename U, typename... Args>
  void Write(T&& t, U&& u, Args&&... args) {
    Write(std::forward<T>(t));
    Write(std::forward<U>(u));
    Write(std::forward<Args>(args)...);
  }

  std::string GetGlobalName(const std::string&) const;
  std::string GetModuleName(const std::string&) const;

  void Write() {}
  void Write(Newline);
  void Write(OpenBrace);
  void Write(CloseBrace);
  void Write(Index);
  void Write(std::string_view);
  void Write(const LocalName&);
  void Write(const GlobalName&);
  void Write(const ExternalPtr&);
  void Write(Type);
  void Write(TypeEnum);
  void Write(const Var&);
  void Write(const GotoLabel&);
  void Write(const LabelDecl&);
  void Write(const GlobalVar&);
  void Write(const StackVar&);
  void Write(const ResultType&);
  void Write(const Const&);
  void WriteInitExpr(const ExprList&);
  void WriteSourceTop();
  void WriteSourceBottom();
  void WriteTagTypes();
  void WriteFuncTypes();
  void WriteTags();
  void WriteTag(const Tag*, const std::string&);
  void WriteImport(const char*, const std::string&, const std::string&, bool);
  void WriteImports();
  void WriteFuncType(const FuncDeclaration&);
  void AllocateFuncs();
  void WriteGlobals();
  void WriteGlobal(const Global&, const std::string&);
  void WriteMemories();
  void WriteMemory(const std::string&);
  void WriteTables();
  void WriteTable(const std::string&);
  void WriteDataSegmentData(const DataSegment* data_segment);
  void WriteDataInitializers();
  void WriteElemSegmentExprs(const ElemSegment* elem_segment);
  void WriteElemInitializers();
  void WriteExports();
  void WriteInit();
  void WriteFuncs();
  void Write(const Func&);
  void WriteParams(const std::vector<std::string>&, std::vector<std::string>&);
  void WriteLocals(const std::vector<std::string>&,
                   const std::vector<std::string>&);
  void WriteStackVarDeclarations();
  void WriteCallIndirectDefinitions();
  void Write(const ExprList&);

  void WriteSimpleUnaryExpr(Type, const char* op, bool can_trap = false);
  void WritePostfixUnaryExpr(Type, const char* op);
  void WriteInfixBinaryExpr(Opcode,
                            const char* op,
                            uint8_t precedence,
                            bool debooleanize = false);
  void WritePrefixBinaryExpr(Opcode, const char* op, bool can_trap = false);
  void WriteUnsignedCompareExpr(Opcode, const char* op);
  void Write(const BinaryExpr&);
  void Write(const CompareExpr&);
  void Write(const ConvertExpr&);
  void Write(const LoadExpr&);
  void Write(const StoreExpr&);
  void Write(const UnaryExpr&);
  void Write(const TernaryExpr&);
  void Write(const SimdLaneOpExpr&);
  void Write(const SimdLoadLaneExpr&);
  void Write(const SimdStoreLaneExpr&);
  void Write(const SimdShuffleOpExpr&);
  void Write(const LoadSplatExpr&);
  void Write(const LoadZeroExpr&);
  void Write(const Block&);

  size_t BeginTry(const TryExpr& tryexpr);
  void WriteTryCatch(const TryExpr& tryexpr);
  void WriteTryDelegate(const TryExpr& tryexpr);
  void Write(const Catch& c);

  void PushTryCatch(const std::string& name);
  void PopTryCatch();

  void PushFuncSection(const std::string_view include_condition = "");

  const WriteKotlinOptions& options_;
  const Module* module_ = nullptr;
  const Func* func_ = nullptr;
  Stream* stream_ = nullptr;
  Stream* kotlin_stream_ = nullptr;
  std::string class_name_;
  std::string package_name_;
  Result result_ = Result::Ok;
  int indent_ = 0;
  bool should_write_indent_next_ = false;
  bool unreachable_ = false;

  SymbolMap global_sym_map_;
  SymbolMap module_import_sym_map_;
  SymbolMap local_sym_map_;
  StackVarSymbolMap stack_var_sym_map_;
  SymbolSet global_syms_;
  SymbolSet local_syms_;
  SymbolSet import_syms_;
  SymbolSet module_import_syms_;
  TypeVector type_stack_;
  std::vector<Label> label_stack_;
  std::vector<TryCatchLabel> try_catch_stack_;
  std::vector<StackValue> value_stack_;
  CallIndirectDeclMap call_indirect_decl_map_;

  std::vector<std::pair<std::string, MemoryStream>> func_sections_;
  SymbolSet func_includes_;
};

static const char kImplicitFuncLabel[] = "$Bfunc";

size_t KotlinWriter::MarkTypeStack() const {
  return type_stack_.size();
}

void KotlinWriter::ResetTypeStack(size_t mark) {
  assert(mark <= type_stack_.size());
  type_stack_.erase(type_stack_.begin() + mark, type_stack_.end());
  assert(value_stack_.size() == type_stack_.size());
}

Type KotlinWriter::StackType(Index index) const {
  assert(index < type_stack_.size());
  return *(type_stack_.rbegin() + index);
}

void KotlinWriter::PushType(Type type) {
  type_stack_.push_back(type);
}

void KotlinWriter::PushTypes(const TypeVector& types) {
  type_stack_.insert(type_stack_.end(), types.begin(), types.end());
}

void KotlinWriter::DropTypes(size_t count) {
  assert(count <= type_stack_.size());
  type_stack_.erase(type_stack_.end() - count, type_stack_.end());
  assert(value_stack_.size() == type_stack_.size());
}

void KotlinWriter::PushValue(StackValue value) {
  value_stack_.push_back(value);
}

const StackValue& KotlinWriter::GetValue(Index index) const {
  assert(type_stack_.size() >= value_stack_.size());
  assert(index < value_stack_.size());
  return *(value_stack_.rbegin() + index);
}

void KotlinWriter::PushVar() {
  assert(type_stack_.size() > value_stack_.size());
  StackValue value;
  Type type = type_stack_[value_stack_.size()];
  StackTypePair stp = {value_stack_.size(), type};
  auto iter = stack_var_sym_map_.find(stp);
  if (iter == stack_var_sym_map_.end()) {
    std::string name = MangleType(type) + std::to_string(value_stack_.size());
    value.value = DefineStackVarName(value_stack_.size(), type, name);
  } else {
    value.value = iter->second;
  }
  value.precedence = 0;
  value_stack_.push_back(value);
}

StackValue KotlinWriter::PopValue() {
  assert(!value_stack_.empty());
  StackValue ret = std::move(value_stack_.back());
  value_stack_.pop_back();
  return ret;
}

std::vector<StackValue> KotlinWriter::PopValues(size_t count) {
  assert(value_stack_.size() >= count);
  std::vector<StackValue> values(count);
  std::move(value_stack_.end() - count, value_stack_.end(), values.begin());
  value_stack_.erase(value_stack_.end() - count, value_stack_.end());
  return values;
}

void KotlinWriter::PushValues(std::vector<StackValue> values) {
  value_stack_.resize(value_stack_.size() + values.size());
  std::move(values.begin(), values.end(), value_stack_.end() - values.size());
}

void KotlinWriter::SpillValues() {
  // Writes out values to the function body.
  assert(value_stack_.size() <= type_stack_.size());
  Index max = value_stack_.size();
  for (Index i = 0; i < max; ++i) {
    StackValue& value = value_stack_[i];
    if (value.precedence == 0) {  // simple var
      continue;
    }
    std::string thing;
    Type type = type_stack_[i];
    StackTypePair stp = {i, type};
    auto iter = stack_var_sym_map_.find(stp);
    if (iter == stack_var_sym_map_.end()) {
      std::string name = MangleType(type) + std::to_string(i);
      thing = DefineStackVarName(i, type, name);
    } else {
      thing = iter->second;
    }
    std::swap(value.value, thing);
    value.depends_on.clear();
    value.side_effects.clear();
    value.precedence = 0;
    Write(value.value, " = ", thing, ";", Newline());
  }
}

void KotlinWriter::PushLabel(LabelType label_type,
                             const std::string& name,
                             const FuncSignature& sig,
                             bool used) {
  if (label_type == LabelType::Loop)
    label_stack_.emplace_back(label_type, name, sig.param_types,
                              type_stack_.size(), try_catch_stack_.size(),
                              used);
  else
    label_stack_.emplace_back(label_type, name, sig.result_types,
                              type_stack_.size(), try_catch_stack_.size(),
                              used);
}

const Label* KotlinWriter::FindLabel(const Var& var, bool mark_used) {
  Label* label = nullptr;

  if (var.is_index()) {
    // We've generated names for all labels, so we should only be using an
    // index when branching to the implicit function label, which can't be
    // named.
    assert(var.index() + 1 == label_stack_.size());
    label = &label_stack_[0];
  } else {
    assert(var.is_name());
    for (Index i = label_stack_.size(); i > 0; --i) {
      label = &label_stack_[i - 1];
      if (label->name == var.name())
        break;
    }
  }

  assert(label);
  if (mark_used) {
    label->used = true;
    if (var.is_name()) {
      assert(local_sym_map_.count(var.name()) == 1);
      func_includes_.insert(local_sym_map_[var.name()]);
    }
  }
  return label;
}

void KotlinWriter::PopLabel() {
  label_stack_.pop_back();
}

// static
std::string KotlinWriter::AddressOf(const std::string& s,
                                    const std::string& class_name) {
  return "this@" + class_name + "::" + s + "";
}

// static
char KotlinWriter::MangleType(Type type) {
  switch (type) {
    case Type::I32:
      return 'i';
    case Type::I64:
      return 'j';
    case Type::F32:
      return 'f';
    case Type::F64:
      return 'd';
    default:
      WABT_UNREACHABLE;
  }
}

// static
std::string KotlinWriter::MangleName(std::string_view name) {
  const char kPrefix = 'Z';
  std::string result = "Z_";

  if (!name.empty()) {
    for (unsigned char c : name) {
      if ((isalnum(c) && c != kPrefix) || c == '_') {
        result += c;
      } else {
        result += kPrefix;
        result += StringPrintf("%02X", static_cast<uint8_t>(c));
      }
    }
  }

  return result;
}

// static
std::string KotlinWriter::ExportName(std::string_view mangled_name) {
  return std::string(mangled_name);
}

// static
std::string KotlinWriter::LegalizeName(std::string_view name) {
  if (name.empty())
    return "w2k_";

  std::string result;
  result = isalpha(static_cast<unsigned char>(name[0])) ? name[0] : '_';
  for (size_t i = 1; i < name.size(); ++i)
    result += isalnum(static_cast<unsigned char>(name[i])) ? name[i] : '_';

  // In addition to containing valid characters for C, we must also avoid
  // colliding with things C cares about, such as reserved words (e.g. "void")
  // or a function name like main() (which a compiler will  complain about if we
  // define it with another type). To avoid such problems, prefix.
  result = "w2k_" + result;

  return result;
}

std::string KotlinWriter::DefineName(SymbolSet* set, std::string_view name) {
  std::string legal = LegalizeName(name);
  if (set->find(legal) != set->end()) {
    std::string base = legal + "_";
    size_t count = 0;
    do {
      legal = base + std::to_string(count++);
    } while (set->find(legal) != set->end());
  }
  set->insert(legal);
  return legal;
}

std::string_view StripLeadingDollar(std::string_view name) {
  if (!name.empty() && name[0] == '$') {
    name.remove_prefix(1);
  }
  return name;
}

std::string KotlinWriter::DefineImportName(
    const std::string& name,
    std::string_view module,
    std::string_view mangled_field_name) {
  std::string mangled(mangled_field_name);
  import_syms_.insert(name);
  std::string unique = DefineName(&global_syms_, mangled);
  global_sym_map_.insert(SymbolMap::value_type(name, unique));
  return unique;
}

std::string KotlinWriter::DefineGlobalScopeName(const std::string& name) {
  std::string unique = DefineName(&global_syms_, StripLeadingDollar(name));
  global_sym_map_.insert(SymbolMap::value_type(name, unique));
  return unique;
}

std::string KotlinWriter::DefineLocalScopeName(const std::string& name) {
  std::string unique = DefineName(&local_syms_, StripLeadingDollar(name));
  local_sym_map_.insert(SymbolMap::value_type(name, unique));
  return unique;
}

std::string KotlinWriter::DefineStackVarName(Index index,
                                             Type type,
                                             std::string_view name) {
  std::string unique = DefineName(&local_syms_, name);
  StackTypePair stp = {index, type};
  stack_var_sym_map_.insert(StackVarSymbolMap::value_type(stp, unique));
  return unique;
}

void KotlinWriter::DefineCallIndirect(Index index,
                                      const FuncDeclaration& decl) {
  if (call_indirect_decl_map_.find(index) != call_indirect_decl_map_.end()) {
    return;
  }
  call_indirect_decl_map_.insert(CallIndirectDeclMap::value_type(index, decl));
}

void KotlinWriter::Indent(int size) {
  indent_ += size;
}

void KotlinWriter::Dedent(int size) {
  indent_ -= size;
  assert(indent_ >= 0);
}

void KotlinWriter::WriteIndent() {
  static char s_indent[] =
      "                                                                       "
      "                                                                       ";
  static size_t s_indent_len = sizeof(s_indent) - 1;
  size_t to_write = indent_;
  while (to_write >= s_indent_len) {
    stream_->WriteData(s_indent, s_indent_len);
    to_write -= s_indent_len;
  }
  if (to_write > 0) {
    stream_->WriteData(s_indent, to_write);
  }
}

void KotlinWriter::WriteValueData(const void* src, size_t size) {
  assert(!value_stack_.empty());
  value_stack_.back().value.append(static_cast<const char*>(src), size);
}

void KotlinWriter::WriteData(const void* src, size_t size) {
  if (should_write_indent_next_) {
    WriteIndent();
    should_write_indent_next_ = false;
  }
  stream_->WriteData(src, size);
}

void WABT_PRINTF_FORMAT(2, 3) KotlinWriter::Writef(const char* format, ...) {
  WABT_SNPRINTF_ALLOCA(buffer, length, format);
  WriteData(buffer, length);
}

void WABT_PRINTF_FORMAT(2, 3) KotlinWriter::WriteValuef(const char* format,
                                                        ...) {
  WABT_SNPRINTF_ALLOCA(buffer, length, format);
  WriteValueData(buffer, length);
}

void KotlinWriter::Write(Newline) {
  Write("\n");
  should_write_indent_next_ = true;
}

void KotlinWriter::Write(OpenBrace) {
  Write("{");
  Indent();
  Write(Newline());
}

void KotlinWriter::Write(CloseBrace) {
  Dedent();
  Write("}");
}

void KotlinWriter::Write(Index index) {
  Writef("%" PRIindex, index);
}

void KotlinWriter::Write(std::string_view s) {
  WriteData(s.data(), s.size());
}

void KotlinWriter::WriteValue(std::string_view s) {
  WriteValueData(s.data(), s.size());
}

void KotlinWriter::Write(const LocalName& name) {
  assert(local_sym_map_.count(name.name) == 1);
  Write(local_sym_map_[name.name]);
}

void KotlinWriter::WriteValue(const LocalName& name) {
  assert(local_sym_map_.count(name.name) == 1);
  WriteValue(local_sym_map_[name.name]);
}

std::string KotlinWriter::GetGlobalName(const std::string& name) const {
  assert(global_sym_map_.count(name) == 1);
  auto iter = global_sym_map_.find(name);
  assert(iter != global_sym_map_.end());
  return iter->second;
}

void KotlinWriter::Write(const GlobalName& name) {
  Write(GetGlobalName(name.name));
}

void KotlinWriter::WriteValue(const GlobalName& name) {
  WriteValue(GetGlobalName(name.name));
}

void KotlinWriter::Write(const ExternalPtr& name) {
  Write(AddressOf(GetGlobalName(name.name), class_name_));
}

void KotlinWriter::Write(const Var& var) {
  assert(var.is_name());
  Write(LocalName(var.name()));
}

void KotlinWriter::WriteValue(const Var& var) {
  assert(var.is_name());
  WriteValue(LocalName(var.name()));
}

void KotlinWriter::Write(const GotoLabel& goto_label) {
  const Label* label = FindLabel(goto_label.var);
  if (label->HasValue()) {
    size_t amount = label->sig.size();
    assert(type_stack_.size() >= label->type_stack_size);
    assert(type_stack_.size() >= amount);
    assert(type_stack_.size() - amount >= label->type_stack_size);
    Index offset = type_stack_.size() - label->type_stack_size - amount;
    for (Index i = 0; i < amount; ++i) {
      const StackValue& sv = GetValue(amount - i - 1);
      if (sv.precedence != 0 || offset != 0) {
        Write(StackVar(amount - i - 1 + offset, label->sig[i]), " = ", sv.value,
              "; ");
      }
    }
  }

  assert(try_catch_stack_.size() >= label->try_catch_stack_size);

  if (goto_label.var.is_name()) {
    switch (label->label_type) {
      case LabelType::Block:
      case LabelType::If:
        Write("break@", goto_label.var, ";");
        break;
      case LabelType::Loop:
        Write("continue@", goto_label.var, ";");
        break;
      default:
        assert(false);
    }
  } else {
    // We've generated names for all labels, so we should only be using an
    // index when branching to the implicit function label, which can't be
    // named.
    Write("break@", Var(kImplicitFuncLabel, {}), ";");
  }
}

void KotlinWriter::Write(const LabelDecl& label) {
  Write(label.name, "@ ");
}

void KotlinWriter::Write(const GlobalVar& var) {
  assert(var.var.is_name());
  Write(GetGlobalName(var.var.name()));
}

void KotlinWriter::WriteValue(const GlobalVar& var) {
  assert(var.var.is_name());
  WriteValue(GetGlobalName(var.var.name()));
}

void KotlinWriter::WriteValue(const StackVar& sv) {
  Index index = type_stack_.size() - 1 - sv.index;
  Type type = sv.type;
  if (type == Type::Any) {
    assert(index < type_stack_.size());
    type = type_stack_[index];
  }

  StackTypePair stp = {index, type};
  auto iter = stack_var_sym_map_.find(stp);
  if (iter == stack_var_sym_map_.end()) {
    std::string name = MangleType(type) + std::to_string(index);
    WriteValue(DefineStackVarName(index, type, name));
  } else {
    WriteValue(iter->second);
  }
}

void KotlinWriter::Write(const StackVar& sv) {
  Index index = type_stack_.size() - 1 - sv.index;
  Type type = sv.type;
  if (type == Type::Any) {
    assert(index < type_stack_.size());
    type = type_stack_[index];
  }

  StackTypePair stp = {index, type};
  auto iter = stack_var_sym_map_.find(stp);
  if (iter == stack_var_sym_map_.end()) {
    std::string name = MangleType(type) + std::to_string(index);
    Write(DefineStackVarName(index, type, name));
  } else {
    Write(iter->second);
  }
}

void KotlinWriter::Write(Type type) {
  switch (type) {
    case Type::I32:
      Write("Int");
      break;
    case Type::I64:
      Write("Long");
      break;
    case Type::F32:
      Write("Float");
      break;
    case Type::F64:
      Write("Double");
      break;
    default:
      WABT_UNREACHABLE;
  }
}

void KotlinWriter::WriteValue(Type type) {
  switch (type) {
    case Type::I32:
      WriteValue("Int");
      break;
    case Type::I64:
      WriteValue("Long");
      break;
    case Type::F32:
      WriteValue("Float");
      break;
    case Type::F64:
      WriteValue("Double");
      break;
    default:
      WABT_UNREACHABLE;
  }
}

void KotlinWriter::Write(TypeEnum type) {
  switch (type.type) {
    case Type::I32:
      Write("Int::class");
      break;
    case Type::I64:
      Write("Long::class");
      break;
    case Type::F32:
      Write("Float::class");
      break;
    case Type::F64:
      Write("Double::class");
      break;
    default:
      WABT_UNREACHABLE;
  }
}

void KotlinWriter::Write(const ResultType& rt) {
  if (rt.types.empty()) {
    Write("Unit");
  } else if (rt.types.size() == 1) {
    Write(rt.types[0]);
  } else {
    Write("(((");
    bool first = true;
    bool second = true;
    for (auto type : rt.types) {
      if (!first) {
        if (!second) {
          Write(", ");
        }
        Write(type);
        second = false;
      }
      first = false;
    }
    Write(") -> Unit) -> ", rt.types[0], ")");
  }
}

void KotlinWriter::WriteValue(const Const& const_) {
  switch (const_.type()) {
    case Type::I32: {
      int32_t i32_bits = static_cast<int32_t>(const_.u32());
      if (i32_bits < 0) {
        WriteValuef("(%d)", i32_bits);
      } else {
        WriteValuef("%d", i32_bits);
      }
      break;
    }

    case Type::I64: {
      int64_t i64_bits = static_cast<int64_t>(const_.u64());
      if (i64_bits == std::numeric_limits<int64_t>::min()) {
        WriteValue("(-0x7FFFFFFFFFFFFFFFL - 1L)");
      } else {
        WriteValuef("%" PRId64 "L", i64_bits);
      }
      break;
    }

    case Type::F32: {
      uint32_t f32_bits = const_.f32_bits();
      if ((f32_bits & 0x7f800000u) == 0x7f800000u) {
        const char* sign = (f32_bits & 0x80000000) ? "-" : "";
        uint32_t significand = f32_bits & 0x7fffffu;
        if (significand == 0) {
          // Infinity.
          WriteValuef("(%sFloat.POSITIVE_INFINITY)", sign);
        } else {
          WriteValue("Float.fromBits(");
          // Nan.
          WriteValuef("%d", f32_bits);
          WriteValuef(") /* %snan:0x%06x */", sign, significand);
        }
      } else if (f32_bits == 0x80000000) {
        // Negative zero. Special-cased so it isn't written as -0 below.
        WriteValue("(-0.0f)");
      } else {
        WriteValuef("(%.9g", Bitcast<float>(f32_bits));
        WriteValue("f)");
      }
      break;
    }

    case Type::F64: {
      uint64_t f64_bits = const_.f64_bits();
      if ((f64_bits & 0x7ff0000000000000ull) == 0x7ff0000000000000ull) {
        const char* sign = (f64_bits & 0x8000000000000000ull) ? "-" : "";
        uint64_t significand = f64_bits & 0xfffffffffffffull;
        if (significand == 0) {
          // Infinity.
          WriteValuef("(%sDouble.POSITIVE_INFINITY)", sign);
        } else {
          // Nan.
          WriteValue("Double.fromBits(");
          if (f64_bits ==
              Bitcast<uint64_t>(std::numeric_limits<int64_t>::min())) {
            WriteValue("-0x7FFFFFFFFFFFFFFFL - 1L");
          } else {
            WriteValuef("%" PRId64 "L", f64_bits);
          }
          WriteValuef(") /* %snan:0x%013" PRIx64 " */", sign, significand);
        }
      } else if (f64_bits == 0x8000000000000000ull) {
        // Negative zero. Special-cased so it isn't written as -0 below.
        WriteValuef("(-0.0)");
      } else {
        std::string printed = StringPrintf("%#.17g", Bitcast<double>(f64_bits));
        WriteValue(printed);
        if (printed.back() == '.') {
          WriteValue("0");
        }
      }
      break;
    }

    default:
      WABT_UNREACHABLE;
  }
}

void KotlinWriter::Write(const Const& const_) {
  switch (const_.type()) {
    case Type::I32: {
      int32_t i32_bits = static_cast<int32_t>(const_.u32());
      if (i32_bits < 0) {
        Writef("(%d)", i32_bits);
      } else {
        Writef("%d", i32_bits);
      }
      break;
    }

    case Type::I64: {
      int64_t i64_bits = static_cast<int64_t>(const_.u64());
      if (i64_bits == std::numeric_limits<int64_t>::min()) {
        Write("(-0x7FFFFFFFFFFFFFFFL - 1L)");
      } else {
        Writef("%" PRId64 "L", i64_bits);
      }
      break;
    }

    case Type::F32: {
      uint32_t f32_bits = const_.f32_bits();
      // TODO(binji): Share with similar float info in interp.cc and literal.cc
      if ((f32_bits & 0x7f800000u) == 0x7f800000u) {
        const char* sign = (f32_bits & 0x80000000) ? "-" : "";
        uint32_t significand = f32_bits & 0x7fffffu;
        if (significand == 0) {
          // Infinity.
          Writef("%sFloat.POSITIVE_INFINITY", sign);
        } else {
          Write("Float.fromBits(");
          // Nan.
          Writef("%d", f32_bits);
          Writef(") /* %snan:0x%06x */", sign, significand);
        }
      } else if (f32_bits == 0x80000000) {
        // Negative zero. Special-cased so it isn't written as -0 below.
        Writef("-0.0f");
      } else {
        Writef("%.9g", Bitcast<float>(f32_bits));
        Write("f");
      }
      break;
    }

    case Type::F64: {
      uint64_t f64_bits = const_.f64_bits();
      // TODO(binji): Share with similar float info in interp.cc and literal.cc
      if ((f64_bits & 0x7ff0000000000000ull) == 0x7ff0000000000000ull) {
        const char* sign = (f64_bits & 0x8000000000000000ull) ? "-" : "";
        uint64_t significand = f64_bits & 0xfffffffffffffull;
        if (significand == 0) {
          // Infinity.
          Writef("%sDouble.POSITIVE_INFINITY", sign);
        } else {
          // Nan.
          Write("Double.fromBits(");
          if (f64_bits ==
              Bitcast<uint64_t>(std::numeric_limits<int64_t>::min())) {
            Write("-0x7FFFFFFFFFFFFFFFL - 1L");
          } else {
            Writef("%" PRId64 "L", f64_bits);
          }
          Writef(") /* %snan:0x%013" PRIx64 " */", sign, significand);
        }
      } else if (f64_bits == 0x8000000000000000ull) {
        // Negative zero. Special-cased so it isn't written as -0 below.
        Writef("-0.0");
      } else {
        std::string printed = StringPrintf("%#.17g", Bitcast<double>(f64_bits));
        Write(printed);
        if (printed.back() == '.') {
          Write("0");
        }
      }
      break;
    }

    default:
      WABT_UNREACHABLE;
  }
}

void KotlinWriter::WriteInitExpr(const ExprList& expr_list) {
  if (expr_list.empty())
    return;

  assert(expr_list.size() == 1);
  const Expr* expr = &expr_list.front();
  switch (expr_list.front().type()) {
    case ExprType::Const:
      Write(cast<ConstExpr>(expr)->const_);
      break;

    case ExprType::GlobalGet:
      Write(GlobalVar(cast<GlobalGetExpr>(expr)->var));
      break;

    default:
      WABT_UNREACHABLE;
  }
}

void KotlinWriter::WriteSourceTop() {
  if (!package_name_.empty()) {
    Write("package ", package_name_, Newline());
  }
  Write(s_source_includes);
  Write("@Suppress(\"NAME_SHADOWING\", \"UNUSED_VALUE\", \"UNUSED_VARIABLE\", ",
        "\"UNUSED_PARAMETER\", \"UNREACHABLE_CODE\", \"UNUSED_EXPRESSION\", ",
        "\"VARIABLE_WITH_REDUNDANT_INITIALIZER\", ",
        "\"ASSIGNED_BUT_NEVER_ACCESSED_VARIABLE\")", Newline());
  Write("class ", class_name_,
        " (moduleRegistry: " WASM_RT_PKG ".ModuleRegistry, name: String)",
        OpenBrace());
  Write(s_source_inner);
}

void KotlinWriter::WriteImport(const char* type,
                               const std::string& module,
                               const std::string& mangled,
                               bool delegate) {
  if (delegate) {
    Write(" by ");
  } else {
    Write(" = ");
  }
  Write("moduleRegistry.import", type, "(\"");
  Write(MangleName(module), "\", \"", mangled, "\");");
}

void KotlinWriter::WriteSourceBottom() {
  Dedent();
  Write("}");
}

void KotlinWriter::WriteFuncTypes() {
  if (!module_->types.size()) {
    return;
  }
  Write(Newline());
  Writef("private val func_types: IntArray = IntArray(%" PRIzd ")",
         module_->types.size());
  Write(Newline(), Newline());
  Write("init /* func_types */", OpenBrace());
  Index func_type_index = 0;
  for (TypeEntry* type : module_->types) {
    FuncType* func_type = cast<FuncType>(type);
    Index num_params = func_type->GetNumParams();
    Index num_results = func_type->GetNumResults();
    Write("func_types[", func_type_index,
          "] = " WASM_RT_PKG ".register_func_type(", num_params, ", ",
          num_results);
    for (Index i = 0; i < num_params; ++i) {
      Write(", ", TypeEnum(func_type->GetParamType(i)));
    }

    for (Index i = 0; i < num_results; ++i) {
      Write(", ", TypeEnum(func_type->GetResultType(i)));
    }

    Write(");", Newline());
    ++func_type_index;
  }
  Write(CloseBrace(), Newline());
}

void KotlinWriter::WriteTags() {
  if (module_->tags.size() == module_->num_tag_imports) {
    return;
  }

  Write(Newline());

  for (auto it = module_->tags.cbegin() + module_->num_tag_imports;
       it != module_->tags.cend(); ++it) {
    const Tag* tag = *it;

    Write("private var ");
    WriteTag(tag, DefineGlobalScopeName(tag->name));
    Write(" = " WASM_RT_PKG ".Tag()", Newline());
  }
}

void KotlinWriter::WriteTag(const Tag* tag, const std::string& name) {
  Write(name, ": " WASM_RT_PKG ".Tag<(");
  const FuncDeclaration& tag_type = tag->decl;
  Index num_params = tag_type.GetNumParams();
  assert(tag_type.GetNumResults() == 0);
  for (Index i = 0; i < num_params; ++i) {
    if (i != 0) {
      Write(",");
    }
    Write(tag_type.GetParamType(i));
  }
  Write(") -> Unit>");
}

void KotlinWriter::WriteImports() {
  if (module_->imports.empty())
    return;

  Write(Newline());

  // TODO(binji): Write imports ordered by type.
  for (const Import* import : module_->imports) {
    Write("/* import: '", import->module_name, "' '", import->field_name,
          "' */", Newline());
    Write("private ");
    std::string mangled;
    const char* type;
    bool delegate = false;
    switch (import->kind()) {
      case ExternalKind::Func: {
        Write("val ");
        const Func& func = cast<FuncImport>(import)->func;
        mangled = MangleName(import->field_name);
        std::string name =
            DefineImportName(func.name, import->module_name, mangled);
        Write(name, ": ");
        WriteFuncType(func.decl);
        type = "Func";
        break;
      }

      case ExternalKind::Global: {
        const Global& global = cast<GlobalImport>(import)->global;
        if (global.mutable_) {
          Write("var ");
          type = "Global";
          delegate = true;
        } else {
          Write("val ");
          type = "Constant";
        }
        mangled = MangleName(import->field_name);
        WriteGlobal(global, DefineImportName(global.name, import->module_name,
                                             mangled));
        break;
      }

      case ExternalKind::Memory: {
        Write("val ");
        const Memory& memory = cast<MemoryImport>(import)->memory;
        mangled = MangleName(import->field_name);
        WriteMemory(
            DefineImportName(memory.name, import->module_name, mangled));
        type = "Memory";
        break;
      }

      case ExternalKind::Table: {
        Write("val ");
        const Table& table = cast<TableImport>(import)->table;
        mangled = MangleName(import->field_name);
        WriteTable(DefineImportName(table.name, import->module_name, mangled));
        type = "Table";
        break;
      }

      case ExternalKind::Tag: {
        Write("val ");
        const Tag& tag = cast<TagImport>(import)->tag;
        mangled = MangleName(import->field_name);
        WriteTag(&tag,
                 DefineImportName(tag.name, import->module_name, mangled));
        type = "Tag";
        break;
      }

      default:
        WABT_UNREACHABLE;
    }
    WriteImport(type, import->module_name, mangled, delegate);

    Write(Newline());
  }
}

void KotlinWriter::WriteFuncType(const FuncDeclaration& decl) {
  Write("(");
  for (Index i = 0; i < decl.GetNumParams(); ++i) {
    if (i != 0) {
      Write(", ");
    }
    Write(decl.GetParamType(i));
  }
  Write(") -> ", ResultType(decl.sig.result_types));
}

void KotlinWriter::AllocateFuncs() {
  if (module_->funcs.size() == module_->num_func_imports)
    return;

  Index func_index = 0;
  for (const Func* func : module_->funcs) {
    bool is_import = func_index < module_->num_func_imports;
    if (!is_import) {
      DefineGlobalScopeName(func->name);
    }
    ++func_index;
  }
}

void KotlinWriter::WriteGlobals() {
  Index global_index = 0;
  if (module_->globals.size() != module_->num_global_imports) {
    Write(Newline());

    for (const Global* global : module_->globals) {
      bool is_import = global_index < module_->num_global_imports;
      if (!is_import) {
        Write("private ");
        if (global->mutable_) {
          Write("var ");
        } else {
          Write("val ");
        }
        WriteGlobal(*global, DefineGlobalScopeName(global->name));
        Write(";", Newline());
      }
      ++global_index;
    }
  }

  Write(Newline(), "init /* globals */ ", OpenBrace());
  global_index = 0;
  for (const Global* global : module_->globals) {
    bool is_import = global_index < module_->num_global_imports;
    if (!is_import) {
      assert(!global->init_expr.empty());
      Write(GlobalName(global->name), " = ");
      WriteInitExpr(global->init_expr);
      Write(";", Newline());
    }
    ++global_index;
  }
  Write(CloseBrace(), Newline());
}

void KotlinWriter::WriteGlobal(const Global& global, const std::string& name) {
  Write(name, ": ", global.type);
}

void KotlinWriter::WriteMemories() {
  if (module_->memories.size() == module_->num_memory_imports)
    return;

  Write(Newline());

  Index memory_index = 0;
  for (const Memory* memory : module_->memories) {
    bool is_import = memory_index < module_->num_memory_imports;
    if (!is_import) {
      Write("private var ");
      WriteMemory(DefineGlobalScopeName(memory->name));
      uint32_t max =
          memory->page_limits.has_max ? memory->page_limits.max : 65536;
      Write(" = " WASM_RT_PKG ".Memory(", memory->page_limits.initial, ", ");
      Writef("%d", static_cast<int32_t>(max));
      Write(");", Newline());
    }
    ++memory_index;
  }
}

void KotlinWriter::WriteMemory(const std::string& name) {
  Write(name, ": " WASM_RT_PKG ".Memory");
}

void KotlinWriter::WriteTables() {
  if (module_->tables.size() == module_->num_table_imports) {
    return;
  }

  Write(Newline());

  assert(module_->tables.size() <= 1);
  Index table_index = 0;
  for (const Table* table : module_->tables) {
    bool is_import = table_index < module_->num_table_imports;
    if (!is_import) {
      Write("private var ");
      WriteTable(DefineGlobalScopeName(table->name));
      uint32_t max =
          table->elem_limits.has_max ? table->elem_limits.max : UINT32_MAX;
      Write(" = " WASM_RT_PKG ".Table(", table->elem_limits.initial, ", ");
      Writef("%d", static_cast<int32_t>(max));
      Write(");", Newline());
    }
    ++table_index;
  }
}

void KotlinWriter::WriteTable(const std::string& name) {
  Write(name, ": " WASM_RT_PKG ".Table");
}

static inline bool is_droppable(const DataSegment* data_segment) {
  return (data_segment->kind == SegmentKind::Passive) &&
         (!data_segment->data.empty());
}

// base64 is better, inspired by:
// https://thephd.dev/implementing-embed-c-and-c++
const char base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void KotlinWriter::WriteDataSegmentData(const DataSegment* data_segment) {
  size_t i = 0;
  uint32_t data = 0;
  for (uint8_t x : data_segment->data) {
    data = (data << 8) | x;
    if ((i++ % 3) == 2) {
      Writef("%c", base64_alphabet[(data >> 18) & 0x3F]);
      Writef("%c", base64_alphabet[(data >> 12) & 0x3F]);
      Writef("%c", base64_alphabet[(data >> 6) & 0x3F]);
      Writef("%c", base64_alphabet[(data)&0x3F]);
    }
  }
  if ((i % 3) == 1) {
    data = data << 4;
    Writef("%c", base64_alphabet[(data >> 6) & 0x3F]);
    Writef("%c", base64_alphabet[(data)&0x3F]);
  } else if ((i % 3) == 2) {
    data = data << 2;
    Writef("%c", base64_alphabet[(data >> 12) & 0x3F]);
    Writef("%c", base64_alphabet[(data >> 6) & 0x3F]);
    Writef("%c", base64_alphabet[(data)&0x3F]);
  }
}

void KotlinWriter::WriteDataInitializers() {
  for (const DataSegment* data_segment : module_->data_segments) {
    DefineGlobalScopeName(data_segment->name);
    if (data_segment->data.size()) {
      Write(Newline(), "private ", is_droppable(data_segment) ? "var" : "val",
            " data_segment_data_", GlobalName(data_segment->name),
            ": ByteArray = " WASM_RT_PKG ".loadb64(\"");
      WriteDataSegmentData(data_segment);
      Write("\");", Newline());
    }
  }

  Write(Newline(), "init /* memory */ ", OpenBrace());
  for (const DataSegment* data_segment : module_->data_segments) {
    if (data_segment->kind != SegmentKind::Active) {
      continue;
    }
    const Memory* memory =
        module_->memories[module_->GetMemoryIndex(data_segment->memory_var)];
    Write(GlobalName(memory->name), ".put(");
    WriteInitExpr(data_segment->offset);
    if (data_segment->data.empty()) {
      Write(", byteArrayOf());", Newline());
    } else {
      Write(", " WASM_RT_PKG ".loadb64(\"");
      WriteDataSegmentData(data_segment);
      Write("\"));", Newline());
    }
  }

  Write(CloseBrace(), Newline());
}

static inline bool is_droppable(const ElemSegment* elem_segment) {
  return (elem_segment->kind == SegmentKind::Passive) &&
         (!elem_segment->elem_exprs.empty());
}

void KotlinWriter::WriteElemSegmentExprs(const ElemSegment* elem_segment) {
  for (const ExprList& elem_expr : elem_segment->elem_exprs) {
    assert(elem_expr.size() == 1);
    const Expr& expr = elem_expr.front();
    switch (expr.type()) {
      case ExprType::RefFunc: {
        const Func* func = module_->GetFunc(cast<RefFuncExpr>(&expr)->var);
        const Index func_type_index =
            module_->GetFuncTypeIndex(func->decl.type_var);
        bool is_import = import_syms_.count(func->name) != 0;
        Write(WASM_RT_PKG ".Func(", func_type_index, ", ");
        if (!is_import) {
          Write(ExternalPtr(func->name));
        } else {
          Write(GlobalName(func->name));
        }
        Write("), ", Newline());
      } break;
      case ExprType::RefNull:
        Write("null, ", Newline());
        break;
      default:
        WABT_UNREACHABLE;
    }
  }
}

void KotlinWriter::WriteElemInitializers() {
  for (const ElemSegment* elem_segment : module_->elem_segments) {
    if (!is_droppable(elem_segment)) {
      continue;
    }

    DefineGlobalScopeName(elem_segment->name);
    Write(Newline(), "private var elem_segment_exprs_",
          GlobalName(elem_segment->name),
          ": Array<" WASM_RT_PKG ".ElemSegExpr?> = arrayOf(");
    WriteElemSegmentExprs(elem_segment);
    Write(");", Newline());
  }

  const Table* table = module_->tables.empty() ? nullptr : module_->tables[0];

  Write(Newline(), "init /* table */ ", OpenBrace());
  for (const ElemSegment* elem_segment : module_->elem_segments) {
    if (elem_segment->kind != SegmentKind::Active) {
      continue;
    }

    Write(GlobalName(table->name), ".table_init(");
    WriteInitExpr(elem_segment->offset);
    if (elem_segment->elem_exprs.empty()) {
      // It's mandatory to handle the case of a zero-length elem segment
      // (even in a module with no types). This must trap if the offset
      // is out of bounds.
      Write(", arrayOf(), 0, 0, intArrayOf());", Newline());
    } else {
      Write(", arrayOf(");
      WriteElemSegmentExprs(elem_segment);
      Write("), 0, ", elem_segment->elem_exprs.size(), ", func_types);",
            Newline());
    }
  }

  Write(CloseBrace(), Newline());
}

void KotlinWriter::WriteExports() {
  if (module_->exports.empty())
    return;

  Write(Newline());

  Write("init /* exports */ ", OpenBrace());

  for (const Export* export_ : module_->exports) {
    Write("/* export: '", export_->name, "' */", Newline());

    std::string mangled_name;
    std::string internal_name;
    const char* type;
    bool external_ptr = false;

    switch (export_->kind) {
      case ExternalKind::Func: {
        const Func* func = module_->GetFunc(export_->var);
        mangled_name = ExportName(MangleName(export_->name));
        internal_name = func->name;
        external_ptr = import_syms_.count(func->name) == 0;
        type = "Func";
        break;
      }

      case ExternalKind::Global: {
        const Global* global = module_->GetGlobal(export_->var);
        mangled_name = ExportName(MangleName(export_->name));
        internal_name = global->name;
        if (global->mutable_) {
          external_ptr = true;
          type = "Global";
        } else {
          type = "Constant";
        }
        break;
      }

      case ExternalKind::Memory: {
        const Memory* memory = module_->GetMemory(export_->var);
        mangled_name = ExportName(MangleName(export_->name));
        internal_name = memory->name;
        type = "Memory";
        break;
      }

      case ExternalKind::Table: {
        const Table* table = module_->GetTable(export_->var);
        mangled_name = ExportName(MangleName(export_->name));
        internal_name = table->name;
        type = "Table";
        break;
      }

      case ExternalKind::Tag: {
        const Tag* tag = module_->GetTag(export_->var);
        mangled_name = ExportName(MangleName(export_->name));
        internal_name = tag->name;
        type = "Tag";
        break;
      }

      default:
        WABT_UNREACHABLE;
    }
    Write("moduleRegistry.export", type, "(name, \"", mangled_name, "\", ");
    if (external_ptr) {
      Write(ExternalPtr(internal_name));
    } else {
      Write(GlobalName(internal_name));
    }
    Write(");");

    Write(Newline());
  }

  Write(CloseBrace());
}

void KotlinWriter::WriteInit() {
  Write(Newline(), "init ", OpenBrace());
  for (Var* var : module_->starts) {
    Write(GetGlobalName(module_->GetFunc(*var)->name), "();", Newline());
  }
  Write(CloseBrace(), Newline());
}

void KotlinWriter::WriteFuncs() {
  Write(Newline());
  Index func_index = 0;
  for (const Func* func : module_->funcs) {
    bool is_import = func_index < module_->num_func_imports;
    if (!is_import)
      Write(Newline(), *func, Newline());
    ++func_index;
  }
}

void KotlinWriter::PushFuncSection(const std::string_view include_condition) {
  func_sections_.emplace_back(include_condition, MemoryStream{});
  stream_ = &func_sections_.back().second;
}

void KotlinWriter::Write(const Func& func) {
  func_ = &func;
  // Copy symbols from global symbol table so we don't shadow them.
  local_syms_ = global_syms_;
  local_sym_map_.clear();
  stack_var_sym_map_.clear();
  func_sections_.clear();
  func_includes_.clear();

  std::vector<std::string> index_to_name;
  std::vector<std::string> to_shadow;
  MakeTypeBindingReverseMapping(func_->GetNumParamsAndLocals(), func_->bindings,
                                &index_to_name);

  Write("private fun ", GlobalName(func.name), "(");
  WriteParams(index_to_name, to_shadow);
  Write(": ", ResultType(func.decl.sig.result_types), OpenBrace());
  WriteLocals(index_to_name, to_shadow);
  Write("try ", OpenBrace());

  PushFuncSection();

  std::string label = DefineLocalScopeName(kImplicitFuncLabel);
  value_stack_.clear();
  ResetTypeStack(0);
  std::string empty;  // Must not be temporary, since address is taken by Label.
  PushLabel(LabelType::Func, empty, func.decl.sig);
  Write(LabelDecl(label), "do ", OpenBrace());
  Write(func.exprs);
  std::vector<StackValue> return_values;
  if (!unreachable_) {
    SpillValues();
    PopValues(func.GetNumResults());
  }
  unreachable_ = false;
  PopLabel();
  ResetTypeStack(0);
  PushTypes(func.decl.sig.result_types);
  while (value_stack_.size() < type_stack_.size()) {
    PushVar();
  }
  Write(CloseBrace(), " while (false);", Newline());

  // Return the top of the stack implicitly.
  Index num_results = func.GetNumResults();
  if (num_results == 1) {
    Write("return ", StackVar(0), ";", Newline());
  } else if (num_results >= 2) {
    Write("return ", OpenBrace());
    Write("it(");
    for (Index i = 1; i < num_results; ++i) {
      if (i != 1) {
        Write(", ");
      }
      Write(StackVar(num_results - i - 1));
    }
    Write(");", Newline(), StackVar(num_results - 1), Newline());
    Write(CloseBrace(), Newline());
  }

  stream_ = kotlin_stream_;

  WriteStackVarDeclarations();

  for (auto& [condition, stream] : func_sections_) {
    std::unique_ptr<OutputBuffer> buf = stream.ReleaseOutputBuffer();
    if (condition.empty() || func_includes_.count(condition)) {
      stream_->WriteData(buf->data.data(), buf->data.size());
    }
  }

  Write(CloseBrace(), " catch(e: StackOverflowError) ", OpenBrace(),
        "throw " WASM_RT_PKG ".ExhaustionException(null, e)", Newline());
  Write(CloseBrace(), " catch (d: Delegate) ", OpenBrace(), "throw d.ex",
        Newline());
  Write(CloseBrace(), Newline());

  Write(CloseBrace());

  func_ = nullptr;
}

void KotlinWriter::WriteParams(const std::vector<std::string>& index_to_name,
                               std::vector<std::string>& to_shadow) {
  if (func_->GetNumParams() != 0) {
    Indent(4);
    for (Index i = 0; i < func_->GetNumParams(); ++i) {
      if (i != 0) {
        Write(", ");
        if ((i % 8) == 0)
          Write(Newline());
      }
      std::string name = DefineLocalScopeName(index_to_name[i]);
      to_shadow.push_back(name);
      Write(name, ": ", func_->GetParamType(i));
    }
    Dedent(4);
  }
  Write(")");
}

void KotlinWriter::WriteLocals(const std::vector<std::string>& index_to_name,
                               const std::vector<std::string>& to_shadow) {
  if (!to_shadow.empty()) {
    for (const auto& param : to_shadow) {
      Write("var ", param, " = ", param, ";", Newline());
    }
  }
  Index num_params = func_->GetNumParams();
  for (Type type : {Type::I32, Type::I64, Type::F32, Type::F64}) {
    Index local_index = 0;
    for (Type local_type : func_->local_types) {
      if (local_type == type) {
        Write("var ",
              DefineLocalScopeName(index_to_name[num_params + local_index]),
              ": ", local_type, " = 0");
        if (type == Type::F32) {
          Write(".0f");
        } else if (type == Type::F64) {
          Write(".0");
        }
        Write(Newline());
      }
      ++local_index;
    }
  }
}

void KotlinWriter::WriteCallIndirectDefinitions() {
  // Creates CALL_INDIRECT functions, used to adapt between JVM and WASM calling
  // conventions.
  for (const auto& pair : call_indirect_decl_map_) {
    Index index = pair.first;
    const FuncDeclaration& decl = pair.second;
    Writef("private fun CALL_INDIRECT_%u(w2k_table: " WASM_RT_PKG ".Table, ",
           index);
    if (decl.GetNumParams() != 0) {
      Indent(4);
      for (Index i = 0; i < decl.GetNumParams(); ++i) {
        if (i != 0) {
          Write(", ");
          if ((i % 8) == 0)
            Write(Newline());
        }
        Writef("w2k_p%u", i);
        Write(": ", decl.GetParamType(i));
      }
      Write(", w2k_index: Int");
      Dedent(4);
    } else {
      Write("w2k_index: Int");
    }
    Write("): ", ResultType(decl.sig.result_types), OpenBrace());
    Write("return " WASM_RT_PKG ".CALL_INDIRECT<");
    WriteFuncType(decl);
    Write(">(w2k_table, func_types[");
    Write(index, "], w2k_index)(");
    for (Index i = 0; i < decl.GetNumParams(); ++i) {
      Writef("w2k_p%u, ", i);
    }
    Write(")", Newline());
    Write(CloseBrace(), Newline());
  }
}

void KotlinWriter::WriteStackVarDeclarations() {
  for (Type type : {Type::I32, Type::I64, Type::F32, Type::F64}) {
    size_t count = 0;
    for (const auto& [pair, name] : stack_var_sym_map_) {
      Type stp_type = pair.second;

      if (stp_type == type) {
        if (count == 0) {
          Indent(4);
        }
        Write("var ", name, ": ", type, " = 0");
        if (type == Type::F32) {
          Write(".0f");
        } else if (type == Type::F64) {
          Write(".0");
        }
        Write(Newline());
        ++count;
      }
    }
    if (count != 0) {
      Dedent(4);
    }
  }
}

void KotlinWriter::DropValue() {
  StackValue sv = PopValue();
  if (!sv.side_effects.empty()) {
    SpillValues();
    Write(sv.value, ";", Newline());
  }
}

void KotlinWriter::Write(const Block& block) {
  const std::string label = DefineLocalScopeName(block.label);
  std::vector<StackValue> input_values = PopValues(block.decl.GetNumParams());
  DropTypes(block.decl.GetNumParams());
  SideEffects updating;
  DependsOn depends_on;
  SpillValues();
  size_t mark = MarkTypeStack();
  PushLabel(LabelType::Block, block.label, block.decl.sig);
  PushTypes(block.decl.sig.param_types);
  PushValues(std::move(input_values));
  PushFuncSection(label);
  Write(LabelDecl(label), "do ", OpenBrace());
  PushFuncSection();
  Write(block.exprs);
  if (!unreachable_) {
    SpillValues();
    PopValues(block.decl.GetNumResults());
  }
  unreachable_ = false;
  ResetTypeStack(mark);
  PopLabel();
  PushTypes(block.decl.sig.result_types);
  while (value_stack_.size() < type_stack_.size()) {
    PushVar();
  }
  PushFuncSection(label);
  Write(CloseBrace(), " while (false);", Newline());
  PushFuncSection();
}

size_t KotlinWriter::BeginTry(const TryExpr& tryexpr) {
  const std::string tlabel = DefineLocalScopeName(tryexpr.block.label);
  std::vector<StackValue> input_values =
      PopValues(tryexpr.block.decl.GetNumParams());
  DropTypes(tryexpr.block.decl.GetNumParams());
  SideEffects updating;
  DependsOn depends_on;
  SpillValues();
  size_t mark = MarkTypeStack();
  PushTryCatch(tlabel);
  PushLabel(LabelType::Try, tryexpr.block.label, tryexpr.block.decl.sig);
  PushTypes(tryexpr.block.decl.sig.param_types);
  PushValues(std::move(input_values));
  PushFuncSection(tlabel);
  Write(LabelDecl(tlabel), "do ", OpenBrace());
  PushFuncSection();
  Write("try ", OpenBrace());
  Write(tryexpr.block.exprs);
  if (!unreachable_) {
    SpillValues();
    PopValues(tryexpr.block.decl.GetNumResults());
  }
  unreachable_ = false;
  ResetTypeStack(mark);
  // never catch WasmTrapException
  Write(CloseBrace(), " catch (e: " WASM_RT_PKG ".WasmTrapException) ",
        OpenBrace());
  Write("throw e", Newline());
  // handle delegates
  // TODO maybe only emit these if there are delegates?
  // maybe the complexity wouldn't be worth it tho
  Write(CloseBrace(), " catch (d: Delegate) ", OpenBrace());
  Write("if (--d.level == 0) ", OpenBrace());
  Write("throw d.ex", Newline());
  Write(CloseBrace(), Newline());
  Write("throw d", Newline());
  Write(CloseBrace());  // no newline
  assert(label_stack_.back().name == tryexpr.block.label);
  assert(label_stack_.back().label_type == LabelType::Try);
  label_stack_.back().label_type = LabelType::Catch;
  return mark;
}

void KotlinWriter::WriteTryCatch(const TryExpr& tryexpr) {
  const size_t mark = BeginTry(tryexpr);

  /* exception has been thrown -- do we catch it? */

  assert(local_sym_map_.count(tryexpr.block.label) == 1);
  const std::string& tlabel = local_sym_map_[tryexpr.block.label];

  PopTryCatch();

  Write(" catch (ex_", tlabel, ": Exception) ", OpenBrace());
  Write("val ex = ex_", tlabel, ";", Newline());

  assert(!tryexpr.catches.empty());
  bool has_catch_all{};
  for (auto it = tryexpr.catches.cbegin(); it != tryexpr.catches.cend(); ++it) {
    if (it != tryexpr.catches.cbegin()) {
      Write(" else ");
    }
    Write(*it);
    if (!unreachable_) {
      SpillValues();
      PopValues(tryexpr.block.decl.GetNumResults());
    }
    unreachable_ = false;
    Write(CloseBrace());
    ResetTypeStack(mark);
    if (it->IsCatchAll()) {
      has_catch_all = true;
      break;
    }
  }
  if (!has_catch_all) {
    /* if not caught, rethrow */
    Write(" else ", OpenBrace());
    Write("throw ex_", tlabel, Newline());
    Write(CloseBrace(), Newline());
  } else {
    Write(Newline());
  }
  Write(CloseBrace(), Newline()); /* end of catch blocks */

  PushFuncSection(tlabel);
  Write(CloseBrace(), " while (false);", Newline());
  PushFuncSection();

  PopLabel();
  PushTypes(tryexpr.block.decl.sig.result_types);
  while (value_stack_.size() < type_stack_.size()) {
    PushVar();
  }
}

void KotlinWriter::Write(const Catch& c) {
  if (c.IsCatchAll()) {
    Write("if (true) ", OpenBrace());
    Write(c.exprs);
    return;
  }

  const Tag* tag = module_->GetTag(c.var);
  Write("if (", GlobalName(tag->name), ".check (ex) {");
  const FuncDeclaration& tag_type = tag->decl;
  const Index num_params = tag_type.GetNumParams();
  PushTypes(tag_type.sig.param_types);
  for (Index i = 0; i < num_params; ++i) {
    if (i != 0) {
      Write(",");
    }
    Writef("v%d", i);
  }
  Write("->");
  for (Index i = 0; i < num_params; ++i) {
    Write(StackVar(num_params - i - 1));
    Writef("=v%d;", i);
  }
  Write("}) ", OpenBrace());
  while (value_stack_.size() < type_stack_.size()) {
    PushVar();
  }

  Write(c.exprs);
}

void KotlinWriter::PushTryCatch(const std::string& name) {
  try_catch_stack_.emplace_back(name, try_catch_stack_.size());
}

void KotlinWriter::PopTryCatch() {
  assert(!try_catch_stack_.empty());
  try_catch_stack_.pop_back();
}

void KotlinWriter::WriteTryDelegate(const TryExpr& tryexpr) {
  const size_t mark = BeginTry(tryexpr);

  /* exception has been thrown -- where do we delegate it? */

  assert(local_sym_map_.count(tryexpr.block.label) == 1);
  const std::string& tlabel = local_sym_map_[tryexpr.block.label];

  if (tryexpr.delegate_target.is_index()) {
    /* must be the implicit function label */
    Write(" catch (ex: Exception) ", OpenBrace());
    Write("throw Delegate(", try_catch_stack_.size(), ", ex)", Newline());
    Write(CloseBrace(), Newline());
  } else {
    const Label* label = FindLabel(tryexpr.delegate_target, false);

    assert(try_catch_stack_.size() >= label->try_catch_stack_size);
    size_t depth = try_catch_stack_.size() - label->try_catch_stack_size;

    if (depth != 0 && --depth != 0) {
      Write(" catch (ex: Exception) ", OpenBrace());
      Write("throw Delegate(");
      Write(depth);
      Write(", ex)", Newline());
      Write(CloseBrace());
    }
    Write(Newline());
  }

  PopTryCatch();

  PushFuncSection(tlabel);
  Write(CloseBrace(), " while (false);", Newline());
  PushFuncSection();

  ResetTypeStack(mark);
  PopLabel();
  PushTypes(tryexpr.block.decl.sig.result_types);
  while (value_stack_.size() < type_stack_.size()) {
    PushVar();
  }
}

void KotlinWriter::Write(const ExprList& exprs) {
  for (const Expr& expr : exprs) {
    switch (expr.type()) {
      case ExprType::Binary:
        Write(*cast<BinaryExpr>(&expr));
        break;

      case ExprType::Block:
        Write(cast<BlockExpr>(&expr)->block);
        break;

      case ExprType::Br: {
        unreachable_ = true;
        const Var& var = cast<BrExpr>(&expr)->var;
        const Label* label = FindLabel(var);
        std::vector<StackValue> values = PopValues(label->sig.size());
        SpillValues();
        PushValues(std::move(values));
        Write(GotoLabel(var), Newline());
        assert(!label_stack_.empty());
        size_t mark = label_stack_.back().type_stack_size;
        while (value_stack_.size() > mark) {
          DropValue();
        }
        // Stop processing this ExprList, since the following are unreachable.
        return;
      }

      case ExprType::BrIf: {
        StackValue sv = PopValue();
        DropTypes(1);
        SpillValues();
        Write("if ((", sv.value, ").inz())", OpenBrace());
        Write(GotoLabel(cast<BrIfExpr>(&expr)->var), CloseBrace(), Newline());
        break;
      }

      case ExprType::BrTable: {
        const auto* bt_expr = cast<BrTableExpr>(&expr);
        StackValue sv = PopValue();
        DropTypes(1);
        SpillValues();
        unreachable_ = true;
        std::unordered_multimap<const Label*, Index> targets;
        Index i = 0;
        for (const Var& var : bt_expr->targets) {
          const Label* label = FindLabel(var);
          targets.insert(std::pair<const Label*, Index>(label, i++));
        }
        Write("when (", sv.value, ") ", OpenBrace());
        for (const Var& var : bt_expr->targets) {
          const Label* label = FindLabel(var);
          auto range = targets.equal_range(label);
          bool written = false;
          for (auto it = range.first; it != range.second; ++it) {
            if (it == range.first) {
              Write(it->second);
              written = true;
            } else {
              Write(", ", it->second);
            }
          }
          targets.erase(label);

          if (written) {
            Write(" -> ", OpenBrace(), GotoLabel(var), CloseBrace(), Newline());
          }
        }
        Write("else -> ", OpenBrace());
        Write(GotoLabel(bt_expr->default_target), CloseBrace(), Newline(),
              CloseBrace(), Newline());
        assert(!label_stack_.empty());
        size_t mark = label_stack_.back().type_stack_size;
        while (value_stack_.size() > mark) {
          DropValue();
        }
        // Stop processing this ExprList, since the following are unreachable.
        return;
      }

      case ExprType::Call: {
        const Var& var = cast<CallExpr>(&expr)->var;
        const Func& func = *module_->GetFunc(var);
        Index num_params = func.GetNumParams();
        Index num_results = func.GetNumResults();
        assert(type_stack_.size() >= num_params);
        std::vector<StackValue> args = PopValues(num_params);
        DropTypes(num_params);
        SpillValues();
        StackValue sv;
        sv.precedence = 2;
        for (const StackValue& arg : args) {
          sv.depends_on |= arg.depends_on;
          sv.side_effects |= arg.side_effects;
        }
        sv.side_effects.updates_globals = global_syms_;
        sv.side_effects.updates_memory = true;
        sv.side_effects.can_trap = true;
        PushValue(sv);

        if (num_results > 1) {
          WriteValue("(");
        }
        WriteValue(GlobalName(var.name()), "(");
        for (Index i = 0; i < num_params; ++i) {
          WriteValue(args[i].value, ", ");
        }
        WriteValue(")");
        PushTypes(func.decl.sig.result_types);
        if (num_results > 1) {
          WriteValue("){");
          for (Index i = 1; i < num_results; ++i) {
            if (i != 1) {
              WriteValue(",");
            }
            WriteValuef("v%d", i);
          }
          WriteValue("->");
          for (Index i = 1; i < num_results; ++i) {
            WriteValue(StackVar(num_results - i - 1));
            WriteValuef("=v%d;", i);
          }
          WriteValue("}");
        }
        while (value_stack_.size() < type_stack_.size()) {
          PushVar();
          // FIXME these should have depends_on set to the call StackValue
        }
        if (num_results == 0) {
          DropValue();
        }
        break;
      }

      case ExprType::CallIndirect: {
        const FuncDeclaration& decl = cast<CallIndirectExpr>(&expr)->decl;
        Index num_params = decl.GetNumParams();
        Index num_results = decl.GetNumResults();
        assert(type_stack_.size() > num_params);
        StackValue tabkey = PopValue();
        DropTypes(1);
        std::vector<StackValue> args = PopValues(num_params);
        DropTypes(num_params);
        SpillValues();
        StackValue sv;
        sv.precedence = 2;
        sv.depends_on |= tabkey.depends_on;
        sv.side_effects |= tabkey.side_effects;
        for (const StackValue& arg : args) {
          sv.depends_on |= arg.depends_on;
          sv.side_effects |= arg.side_effects;
        }
        sv.side_effects.updates_globals = global_syms_;
        sv.side_effects.updates_memory = true;
        sv.side_effects.can_trap = true;
        PushValue(sv);

        assert(module_->tables.size() == 1);
        const Table* table = module_->tables[0];

        assert(decl.has_func_type);
        Index func_type_index = module_->GetFuncTypeIndex(decl.type_var);

        DefineCallIndirect(func_type_index, decl);
        if (num_results > 1) {
          WriteValue("(");
        }
        WriteValue("CALL_INDIRECT_");
        WriteValuef("%u", func_type_index);
        WriteValue("(", GetGlobalName(table->name));
        WriteValue(", ");
        for (Index i = 0; i < num_params; ++i) {
          WriteValue(args[i].value, ", ");
        }
        WriteValue(tabkey.value, ")");
        PushTypes(decl.sig.result_types);
        if (num_results > 1) {
          WriteValue("){");
          for (Index i = 1; i < num_results; ++i) {
            if (i != 1) {
              WriteValue(",");
            }
            WriteValuef("v%d", i);
          }
          WriteValue("->");
          for (Index i = 1; i < num_results; ++i) {
            WriteValue(StackVar(num_results - i - 1));
            WriteValuef("=v%d;", i);
          }
          WriteValue("}");
        }
        while (value_stack_.size() < type_stack_.size()) {
          PushVar();
          // FIXME these should have depends_on set to the call StackValue
        }
        if (num_results == 0) {
          DropValue();
        }
        break;
      }

      case ExprType::CodeMetadata:
        Write(*cast<CompareExpr>(&expr));
        break;

      case ExprType::Compare:
        Write(*cast<CompareExpr>(&expr));
        break;

      case ExprType::Const: {
        const Const& const_ = cast<ConstExpr>(&expr)->const_;
        PushType(const_.type());
        StackValue sv;
        sv.precedence = 1;
        PushValue(sv);
        WriteValue(const_);
        break;
      }

      case ExprType::Convert:
        Write(*cast<ConvertExpr>(&expr));
        break;

      case ExprType::Drop:
        DropValue();
        DropTypes(1);
        break;

      case ExprType::GlobalGet: {
        const Var& var = cast<GlobalGetExpr>(&expr)->var;
        PushType(module_->GetGlobal(var)->type);
        StackValue sv;
        sv.precedence = 1;
        sv.depends_on.depends_globals.insert(var.name());
        PushValue(sv);
        WriteValue(GlobalVar(var));
        break;
      }

      case ExprType::GlobalSet: {
        const Var& var = cast<GlobalSetExpr>(&expr)->var;
        assert(var.is_name());
        StackValue sv = PopValue();
        DropTypes(1);
        SpillValues();
        Write(GlobalVar(var), " = ", sv.value, ";", Newline());
        break;
      }

      case ExprType::If: {
        const IfExpr& if_ = *cast<IfExpr>(&expr);
        std::string label = DefineLocalScopeName(if_.true_.label);
        StackValue cond = PopValue();
        DropTypes(1);
        SpillValues();
        std::vector<StackValue> args = PopValues(if_.true_.decl.GetNumParams());
        DropTypes(args.size());
        size_t mark = MarkTypeStack();
        PushLabel(LabelType::If, if_.true_.label, if_.true_.decl.sig);
        PushTypes(if_.true_.decl.sig.param_types);
        PushValues(args);
        Write(LabelDecl(label), "do ", OpenBrace());
        Write("if ((", cond.value, ").inz()) ", OpenBrace());
        Write(if_.true_.exprs);
        if (!if_.false_.empty()) {
          if (!unreachable_) {
            SpillValues();
            PopValues(if_.true_.decl.GetNumResults());
          }
          unreachable_ = false;
          Write(CloseBrace());
          assert(value_stack_.size() == mark);
          ResetTypeStack(mark);
          PushTypes(if_.true_.decl.sig.param_types);
          PushValues(std::move(args));
          Write(" else ", OpenBrace(), if_.false_);
        }
        if (!unreachable_) {
          SpillValues();
          PopValues(if_.true_.decl.GetNumResults());
        }
        unreachable_ = false;
        Write(CloseBrace());
        Write(CloseBrace(), " while (false);");
        assert(value_stack_.size() == mark);
        ResetTypeStack(mark);
        Write(Newline());
        PopLabel();
        PushTypes(if_.true_.decl.sig.result_types);
        while (value_stack_.size() < type_stack_.size()) {
          PushVar();
        }
        break;
      }

      case ExprType::Load:
        Write(*cast<LoadExpr>(&expr));
        break;

      case ExprType::LocalGet: {
        const Var& var = cast<LocalGetExpr>(&expr)->var;
        PushType(func_->GetLocalType(var));
        StackValue sv;
        sv.precedence = 1;
        sv.depends_on.depends_locals.insert(var.name());
        PushValue(sv);
        WriteValue(var);
        break;
      }

      case ExprType::LocalSet: {
        const Var& var = cast<LocalSetExpr>(&expr)->var;
        assert(var.is_name());
        StackValue sv = PopValue();
        sv.side_effects.updates_locals.insert(var.name());
        DropTypes(1);
        SpillValues();
        Write(var, " = ", sv.value, ";", Newline());
        break;
      }

      case ExprType::LocalTee: {
        const Var& var = cast<LocalTeeExpr>(&expr)->var;
        assert(var.is_name());
        StackValue sv = PopValue();
        sv.side_effects.updates_locals.insert(var.name());
        sv.value = ("(" + sv.value) + ").also ";
        sv.precedence = 2;
        PushValue(sv);
        WriteValue("{", var, "=it}");
        break;
      }

      case ExprType::Loop: {
        const Block& block = cast<LoopExpr>(&expr)->block;
        if (!block.exprs.empty()) {
          std::string label = DefineLocalScopeName(block.label);
          SpillValues();
          PopValues(block.decl.GetNumParams());
          DropTypes(block.decl.GetNumParams());
          size_t mark = MarkTypeStack();
          PushLabel(LabelType::Loop, block.label, block.decl.sig);
          PushTypes(block.decl.sig.param_types);
          Write("");  // write indent if needed
          PushFuncSection(label);
          Write(LabelDecl(label));
          PushFuncSection();
          Write("while (true) ", OpenBrace());
          Write(block.exprs);
          std::vector<StackValue> output_values;
          if (!unreachable_) {
            output_values = PopValues(block.decl.GetNumResults());
          }
          unreachable_ = false;
          ResetTypeStack(mark);
          PopLabel();
          PushTypes(block.decl.sig.result_types);
          for (StackValue& value : output_values) {
            PushValue(std::move(value));
          }
          while (value_stack_.size() < type_stack_.size()) {
            PushVar();
          }
          Write("break;", Newline());
          Write(CloseBrace(), Newline());
        }
        break;
      }

      case ExprType::MemoryFill: {
        const auto inst = cast<MemoryFillExpr>(&expr);
        Memory* memory =
            module_->memories[module_->GetMemoryIndex(inst->memidx)];
        StackValue svsize = PopValue();
        StackValue svbyte = PopValue();
        StackValue svaddr = PopValue();
        DropTypes(3);
        SpillValues();
        Write(GlobalName(memory->name), ".fill(", svaddr.value, ", ",
              svbyte.value, ", ", svsize.value, ");");
      } break;

      case ExprType::MemoryCopy: {
        const auto inst = cast<MemoryCopyExpr>(&expr);
        Memory* dest_memory =
            module_->memories[module_->GetMemoryIndex(inst->destmemidx)];
        const Memory* src_memory = module_->GetMemory(inst->srcmemidx);
        StackValue svsize = PopValue();
        StackValue srcaddr = PopValue();
        StackValue dstaddr = PopValue();
        DropTypes(3);
        SpillValues();
        Write(GlobalName(dest_memory->name), ".copy_from(",
              GlobalName(src_memory->name), ", ", dstaddr.value, ", ",
              srcaddr.value, ", ", svsize.value, ");", Newline());
      } break;

      case ExprType::MemoryInit: {
        const auto inst = cast<MemoryInitExpr>(&expr);
        Memory* dest_memory =
            module_->memories[module_->GetMemoryIndex(inst->memidx)];
        const DataSegment* src_data = module_->GetDataSegment(inst->var);
        StackValue svsize = PopValue();
        StackValue srcaddr = PopValue();
        StackValue dstaddr = PopValue();
        DropTypes(3);
        SpillValues();
        Write(GlobalName(dest_memory->name), ".memory_init(");
        if (is_droppable(src_data)) {
          Write("data_segment_data_", GlobalName(src_data->name));
        } else {
          Write("byteArrayOf()");
        }
        Write(", ", dstaddr.value, ", ", srcaddr.value, ", ", svsize.value,
              ");", Newline());
      } break;

      case ExprType::TableInit: {
        const auto inst = cast<TableInitExpr>(&expr);
        Table* dest_table =
            module_->tables[module_->GetTableIndex(inst->table_index)];
        const ElemSegment* src_segment =
            module_->GetElemSegment(inst->segment_index);
        StackValue svsize = PopValue();
        StackValue srcaddr = PopValue();
        StackValue dstaddr = PopValue();
        DropTypes(3);
        SpillValues();
        Write(GlobalName(dest_table->name), ".table_init(", dstaddr.value);
        if (is_droppable(src_segment)) {
          Write(", elem_segment_exprs_", GlobalName(src_segment->name));
        } else {
          Write(", arrayOf()");
        }
        Write(", ", srcaddr.value, ", ", svsize.value, ", func_types);",
              Newline());
      } break;

      case ExprType::DataDrop: {
        const auto inst = cast<DataDropExpr>(&expr);
        const DataSegment* data = module_->GetDataSegment(inst->var);
        if (is_droppable(data)) {
          SpillValues();
          Write("data_segment_data_", GlobalName(data->name),
                " = byteArrayOf();", Newline());
        }
      } break;

      case ExprType::ElemDrop: {
        const auto inst = cast<ElemDropExpr>(&expr);
        const ElemSegment* seg = module_->GetElemSegment(inst->var);
        if (is_droppable(seg)) {
          SpillValues();
          Write("elem_segment_exprs_", GlobalName(seg->name), " = arrayOf();",
                Newline());
        }
      } break;

      case ExprType::TableCopy: {
        const auto inst = cast<TableCopyExpr>(&expr);
        Table* dest_table =
            module_->tables[module_->GetTableIndex(inst->dst_table)];
        const Table* src_table = module_->GetTable(inst->src_table);
        StackValue svsize = PopValue();
        StackValue srcaddr = PopValue();
        StackValue dstaddr = PopValue();
        DropTypes(3);
        SpillValues();
        Write(GlobalName(dest_table->name), ".copy_from(",
              GlobalName(src_table->name), ", ", dstaddr.value, ", ",
              srcaddr.value, ", ", svsize.value, ");", Newline());
      } break;

      case ExprType::TableGet:
      case ExprType::TableSet:
      case ExprType::TableGrow:
      case ExprType::TableSize:
      case ExprType::TableFill:
      case ExprType::RefFunc:
      case ExprType::RefNull:
      case ExprType::RefIsNull:
        UNIMPLEMENTED("...");
        break;

      case ExprType::MemoryGrow: {
        Memory* memory = module_->memories[module_->GetMemoryIndex(
            cast<MemoryGrowExpr>(&expr)->memidx)];

        assert(StackType(0) == Type::I32);
        StackValue sv = PopValue();
        DropTypes(1);
        sv.precedence = 2;
        sv.side_effects.updates_memory = true;
        std::string oldvalue;
        std::swap(oldvalue, sv.value);
        PushType(Type::I32);
        PushValue(sv);
        WriteValue(GetGlobalName(memory->name), ".resize(", oldvalue, ")");
        break;
      }

      case ExprType::MemorySize: {
        Memory* memory = module_->memories[module_->GetMemoryIndex(
            cast<MemorySizeExpr>(&expr)->memidx)];

        PushType(Type::I32);
        StackValue sv;
        sv.precedence = 2;
        sv.depends_on.depends_memory = true;
        PushValue(sv);
        WriteValue(GetGlobalName(memory->name), ".pages");
        break;
      }

      case ExprType::Nop:
        break;

      case ExprType::Return: {
        // Goto the function label instead; this way we can do shared function
        // cleanup code in one place.
        unreachable_ = true;
        std::vector<StackValue> values = PopValues(func_->GetNumResults());
        SpillValues();
        PushValues(std::move(values));
        assert(!label_stack_.empty());
        Write(GotoLabel(Var(label_stack_.size() - 1, {})), Newline());
        size_t mark = label_stack_.back().type_stack_size;
        while (value_stack_.size() > mark) {
          DropValue();
        }
        // Stop processing this ExprList, since the following are unreachable.
        return;
      }

      case ExprType::Select: {
        Type type = StackType(1);
        // order matters
        StackValue first = PopValue();
        StackValue second = PopValue();
        StackValue third = PopValue();
        DropTypes(3);
        PushType(type);
        StackValue sv;
        sv.precedence = 1;
        sv.depends_on |= first.depends_on;
        sv.depends_on |= second.depends_on;
        sv.depends_on |= third.depends_on;
        sv.side_effects |= first.side_effects;
        sv.side_effects |= second.side_effects;
        sv.side_effects |= third.side_effects;
        PushValue(sv);
        WriteValue("select(", third.value, ", ", second.value, ", ",
                   first.value, ")");
        break;
      }

      case ExprType::Store:
        Write(*cast<StoreExpr>(&expr));
        break;

      case ExprType::Unary:
        Write(*cast<UnaryExpr>(&expr));
        break;

      case ExprType::Ternary:
        Write(*cast<TernaryExpr>(&expr));
        break;

      case ExprType::SimdLaneOp: {
        Write(*cast<SimdLaneOpExpr>(&expr));
        break;
      }

      case ExprType::SimdLoadLane: {
        Write(*cast<SimdLoadLaneExpr>(&expr));
        break;
      }

      case ExprType::SimdStoreLane: {
        Write(*cast<SimdStoreLaneExpr>(&expr));
        break;
      }

      case ExprType::SimdShuffleOp: {
        Write(*cast<SimdShuffleOpExpr>(&expr));
        break;
      }

      case ExprType::LoadSplat:
        Write(*cast<LoadSplatExpr>(&expr));
        break;

      case ExprType::LoadZero:
        Write(*cast<LoadZeroExpr>(&expr));
        break;

      case ExprType::Unreachable: {
        assert(!label_stack_.empty());
        size_t mark = label_stack_.back().type_stack_size;
        while (value_stack_.size() > mark) {
          DropValue();
        }
        unreachable_ = true;
        Write("throw " WASM_RT_PKG ".UnreachableException(\"unreachable\");",
              Newline());
        return;
      }

      case ExprType::Throw: {
        const Var& var = cast<ThrowExpr>(&expr)->var;
        const Tag* tag = module_->GetTag(var);
        Index num_params = tag->decl.GetNumParams();
        SpillValues();
        Write("throw ", GlobalName(tag->name), ".newException() ", OpenBrace());
        Write("it(");
        for (Index i = 0; i < num_params; ++i) {
          if (i != 0) {
            Write(", ");
          }
          Write(StackVar(num_params - i - 1));
        }
        Write(");", Newline(), CloseBrace(), Newline());
        assert(!label_stack_.empty());
        size_t mark = label_stack_.back().type_stack_size;
        while (value_stack_.size() > mark) {
          DropValue();
        }
        unreachable_ = true;
        return;
      }

      case ExprType::Rethrow: {
        assert(!label_stack_.empty());
        size_t mark = label_stack_.back().type_stack_size;
        while (value_stack_.size() > mark) {
          DropValue();
        }
        unreachable_ = true;
        const RethrowExpr* rethrow = cast<RethrowExpr>(&expr);
        assert(rethrow->var.is_name());
        const LocalName ex{rethrow->var.name()};
        Write("throw ex_", ex, ";", Newline());
        return;
      }

      case ExprType::Try: {
        const TryExpr& tryexpr = *cast<TryExpr>(&expr);
        switch (tryexpr.kind) {
          case TryKind::Plain:
            Write(tryexpr.block);
            break;
          case TryKind::Catch:
            WriteTryCatch(tryexpr);
            break;
          case TryKind::Delegate:
            WriteTryDelegate(tryexpr);
            break;
        }
      } break;

      case ExprType::AtomicLoad:
      case ExprType::AtomicRmw:
      case ExprType::AtomicRmwCmpxchg:
      case ExprType::AtomicStore:
      case ExprType::AtomicWait:
      case ExprType::AtomicFence:
      case ExprType::AtomicNotify:
      case ExprType::ReturnCall:
      case ExprType::ReturnCallIndirect:
      case ExprType::CallRef:
        UNIMPLEMENTED("...");
        break;
    }
  }
}

void KotlinWriter::WriteSimpleUnaryExpr(Type result_type,
                                        const char* op,
                                        bool can_trap) {
  StackValue sv = PopValue();
  DropTypes(1);
  PushType(result_type);
  sv.value = op + ("(" + sv.value) + ")";
  sv.precedence = 3;
  sv.side_effects.can_trap = sv.side_effects.can_trap || can_trap;
  PushValue(sv);
}

void KotlinWriter::WritePostfixUnaryExpr(Type result_type, const char* op) {
  StackValue sv = PopValue();
  DropTypes(1);
  PushType(result_type);
  sv.value = ("(" + sv.value) + ")" + op;
  sv.precedence = 2;
  PushValue(sv);
}

void KotlinWriter::WriteInfixBinaryExpr(Opcode opcode,
                                        const char* op,
                                        uint8_t precedence,
                                        bool debooleanize) {
  Type result_type = opcode.GetResultType();
  StackValue sv_right = PopValue();
  StackValue sv_left = PopValue();
  DropTypes(2);
  PushType(result_type);
  if (sv_left.precedence > precedence) {
    sv_left.value = ("(" + sv_left.value) + ")";
  }
  if (sv_right.precedence >= precedence) {
    sv_right.value = ("(" + sv_right.value) + ")";
  }
  sv_left.value = sv_left.value + " " + op + " " + sv_right.value;
  sv_left.precedence = precedence;
  sv_left.depends_on |= sv_right.depends_on;
  sv_left.side_effects |= sv_right.side_effects;
  if (debooleanize) {
    sv_left.precedence = 2;
    sv_left.value = ("(" + sv_left.value) + ")";
  }
  PushValue(sv_left);
  if (debooleanize) {
    WriteValue(".bto", result_type, "()");
  }
}

void KotlinWriter::WritePrefixBinaryExpr(Opcode opcode,
                                         const char* op,
                                         bool can_trap) {
  Type result_type = opcode.GetResultType();
  StackValue sv_right = PopValue();
  StackValue sv_left = PopValue();
  DropTypes(2);
  PushType(result_type);
  sv_left.value = op + ("(" + sv_left.value) + ", " + sv_right.value + ")";
  sv_left.precedence = 2;
  sv_left.depends_on |= sv_right.depends_on;
  sv_left.side_effects |= sv_right.side_effects;
  sv_left.side_effects.can_trap = sv_left.side_effects.can_trap || can_trap;
  PushValue(sv_left);
}

void KotlinWriter::WriteUnsignedCompareExpr(Opcode opcode, const char* op) {
  Type result_type = opcode.GetResultType();
  Type type = opcode.GetParamType1();
  assert(opcode.GetParamType2() == type);
  std::string cls;
  // TODO(Soni): these are kinda ew. can we use UInt/ULong instead?
  if (type == Type::I32) {
    cls = "java.lang.Integer";
  } else {
    assert(type == Type::I64);
    cls = "java.lang.Long";
  }
  StackValue sv_right = PopValue();
  StackValue sv_left = PopValue();
  DropTypes(2);
  PushType(result_type);
  sv_left.precedence = 2;
  sv_left.depends_on |= sv_right.depends_on;
  sv_left.side_effects |= sv_right.side_effects;
  std::string oldleft;
  std::swap(oldleft, sv_left.value);
  PushValue(sv_left);
  WriteValue("(", cls, ".compareUnsigned(", oldleft, ", ", sv_right.value, ")",
             op, "0).bto", result_type, "()");
}

void KotlinWriter::Write(const BinaryExpr& expr) {
  switch (expr.opcode) {
    case Opcode::I32Add:
    case Opcode::I64Add:
    case Opcode::F32Add:
    case Opcode::F64Add:
      WriteInfixBinaryExpr(expr.opcode, "+", 5);
      break;

    case Opcode::I32Sub:
    case Opcode::I64Sub:
    case Opcode::F32Sub:
    case Opcode::F64Sub:
      WriteInfixBinaryExpr(expr.opcode, "-", 5);
      break;

    case Opcode::I32Mul:
    case Opcode::I64Mul:
    case Opcode::F32Mul:
    case Opcode::F64Mul:
      WriteInfixBinaryExpr(expr.opcode, "*", 4);
      break;

    case Opcode::I32DivS:
      WritePrefixBinaryExpr(expr.opcode, WASM_RT_PKG ".I32_DIV_S", true);
      break;

    case Opcode::I64DivS:
      WritePrefixBinaryExpr(expr.opcode, WASM_RT_PKG ".I64_DIV_S", true);
      break;

    case Opcode::I32DivU:
    case Opcode::I64DivU:
      WritePrefixBinaryExpr(expr.opcode, WASM_RT_PKG ".DIV_U", true);
      break;

    case Opcode::F32Div:
    case Opcode::F64Div:
      WriteInfixBinaryExpr(expr.opcode, "/", 4);
      break;

    case Opcode::I32RemS:
      WritePrefixBinaryExpr(expr.opcode, WASM_RT_PKG ".I32_REM_S", true);
      break;

    case Opcode::I64RemS:
      WritePrefixBinaryExpr(expr.opcode, WASM_RT_PKG ".I64_REM_S", true);
      break;

    case Opcode::I32RemU:
    case Opcode::I64RemU:
      WritePrefixBinaryExpr(expr.opcode, WASM_RT_PKG ".REM_U", true);
      break;

    case Opcode::I32And:
    case Opcode::I64And:
      WriteInfixBinaryExpr(expr.opcode, "and", 7);
      break;

    case Opcode::I32Or:
    case Opcode::I64Or:
      WriteInfixBinaryExpr(expr.opcode, "or", 7);
      break;

    case Opcode::I32Xor:
    case Opcode::I64Xor:
      WriteInfixBinaryExpr(expr.opcode, "xor", 7);
      break;

    case Opcode::I32Shl:
      WriteInfixBinaryExpr(expr.opcode, "shl", 7);
      break;

    case Opcode::I64Shl:
      WritePostfixUnaryExpr(Type::I32, ".toInt()");
      WriteInfixBinaryExpr(expr.opcode, "shl", 7);
      break;

    case Opcode::I32ShrS:
      WriteInfixBinaryExpr(expr.opcode, "shr", 7);
      break;

    case Opcode::I64ShrS:
      WritePostfixUnaryExpr(Type::I32, ".toInt()");
      WriteInfixBinaryExpr(expr.opcode, "shr", 7);
      break;

    case Opcode::I32ShrU:
      WriteInfixBinaryExpr(expr.opcode, "ushr", 7);
      break;

    case Opcode::I64ShrU:
      WritePostfixUnaryExpr(Type::I32, ".toInt()");
      WriteInfixBinaryExpr(expr.opcode, "ushr", 7);
      break;

    case Opcode::I32Rotl:
      WritePrefixBinaryExpr(expr.opcode, WASM_RT_PKG ".I32_ROTL");
      break;

    case Opcode::I64Rotl:
      WritePrefixBinaryExpr(expr.opcode, WASM_RT_PKG ".I64_ROTL");
      break;

    case Opcode::I32Rotr:
      WritePrefixBinaryExpr(expr.opcode, WASM_RT_PKG ".I32_ROTR");
      break;

    case Opcode::I64Rotr:
      WritePrefixBinaryExpr(expr.opcode, WASM_RT_PKG ".I64_ROTR");
      break;

    case Opcode::F32Min:
    case Opcode::F64Min:
      WritePrefixBinaryExpr(expr.opcode, WASM_RT_PKG ".MIN");
      break;

    case Opcode::F32Max:
    case Opcode::F64Max:
      WritePrefixBinaryExpr(expr.opcode, WASM_RT_PKG ".MAX");
      break;

    case Opcode::F32Copysign:
    case Opcode::F64Copysign:
      WritePrefixBinaryExpr(expr.opcode, "Math.copySign");
      break;

    default:
      WABT_UNREACHABLE;
  }
}

void KotlinWriter::Write(const CompareExpr& expr) {
  switch (expr.opcode) {
    case Opcode::I32Eq:
    case Opcode::I64Eq:
    case Opcode::F32Eq:
    case Opcode::F64Eq:
      WriteInfixBinaryExpr(expr.opcode, "==", 11, true);
      break;

    case Opcode::I32Ne:
    case Opcode::I64Ne:
    case Opcode::F32Ne:
    case Opcode::F64Ne:
      WriteInfixBinaryExpr(expr.opcode, "!=", 11, true);
      break;

    case Opcode::I32LtU:
    case Opcode::I64LtU:
      WriteUnsignedCompareExpr(expr.opcode, "<");
      break;

    case Opcode::I32LtS:
    case Opcode::I64LtS:
    case Opcode::F32Lt:
    case Opcode::F64Lt:
      WriteInfixBinaryExpr(expr.opcode, "<", 10, true);
      break;

    case Opcode::I32LeU:
    case Opcode::I64LeU:
      WriteUnsignedCompareExpr(expr.opcode, "<=");
      break;

    case Opcode::I32LeS:
    case Opcode::I64LeS:
    case Opcode::F32Le:
    case Opcode::F64Le:
      WriteInfixBinaryExpr(expr.opcode, "<=", 10, true);
      break;

    case Opcode::I32GtU:
    case Opcode::I64GtU:
      WriteUnsignedCompareExpr(expr.opcode, ">");
      break;

    case Opcode::I32GtS:
    case Opcode::I64GtS:
    case Opcode::F32Gt:
    case Opcode::F64Gt:
      WriteInfixBinaryExpr(expr.opcode, ">", 10, true);
      break;

    case Opcode::I32GeU:
    case Opcode::I64GeU:
      WriteUnsignedCompareExpr(expr.opcode, ">=");
      break;

    case Opcode::I32GeS:
    case Opcode::I64GeS:
    case Opcode::F32Ge:
    case Opcode::F64Ge:
      WriteInfixBinaryExpr(expr.opcode, ">=", 10, true);
      break;

    default:
      WABT_UNREACHABLE;
  }
}

void KotlinWriter::Write(const ConvertExpr& expr) {
  switch (expr.opcode) {
    case Opcode::I32Eqz:
    case Opcode::I64Eqz:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".isz()");
      break;

    case Opcode::I64ExtendI32S:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toLong()");
      break;

    case Opcode::I64ExtendI32U:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(),
                            ".toLong().and(0xFFFFFFFFL)");
      break;

    case Opcode::I32WrapI64:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toInt()");
      break;

    case Opcode::I32TruncF32S:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(),
                           WASM_RT_PKG ".I32_TRUNC_S_F32", true);
      break;

    case Opcode::I64TruncF32S:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(),
                           WASM_RT_PKG ".I64_TRUNC_S_F32", true);
      break;

    case Opcode::I32TruncF64S:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(),
                           WASM_RT_PKG ".I32_TRUNC_S_F64", true);
      break;

    case Opcode::I64TruncF64S:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(),
                           WASM_RT_PKG ".I64_TRUNC_S_F64", true);
      break;

    case Opcode::I32TruncF32U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(),
                           WASM_RT_PKG ".I32_TRUNC_U_F32", true);
      break;

    case Opcode::I64TruncF32U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(),
                           WASM_RT_PKG ".I64_TRUNC_U_F32", true);
      break;

    case Opcode::I32TruncF64U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(),
                           WASM_RT_PKG ".I32_TRUNC_U_F64", true);
      break;

    case Opcode::I64TruncF64U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(),
                           WASM_RT_PKG ".I64_TRUNC_U_F64", true);
      break;

    case Opcode::I32TruncSatF32S:
    case Opcode::I32TruncSatF64S:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toInt()");
      break;

    case Opcode::I64TruncSatF32S:
    case Opcode::I64TruncSatF64S:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toLong()");
      break;

    case Opcode::I32TruncSatF32U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(),
                           WASM_RT_PKG ".I32_TRUNC_SAT_U_F32");
      break;

    case Opcode::I64TruncSatF32U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(),
                           WASM_RT_PKG ".I64_TRUNC_SAT_U_F32");
      break;

    case Opcode::I32TruncSatF64U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(),
                           WASM_RT_PKG ".I32_TRUNC_SAT_U_F64");
      break;

    case Opcode::I64TruncSatF64U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(),
                           WASM_RT_PKG ".I64_TRUNC_SAT_U_F64");
      break;

    case Opcode::F32ConvertI32S:
    case Opcode::F32ConvertI64S:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toFloat()");
      break;

    case Opcode::F32ConvertI32U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(),
                           WASM_RT_PKG ".UIntToFloat");
      break;

    case Opcode::F32DemoteF64:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toFloat()");
      break;

    case Opcode::F32ConvertI64U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(),
                           WASM_RT_PKG ".ULongToFloat");
      break;

    case Opcode::F64ConvertI32S:
    case Opcode::F64ConvertI64S:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toDouble()");
      break;

    case Opcode::F64ConvertI32U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(),
                           WASM_RT_PKG ".UIntToDouble");
      break;

    case Opcode::F64PromoteF32:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toDouble()");
      break;

    case Opcode::F64ConvertI64U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(),
                           WASM_RT_PKG ".ULongToDouble");
      break;

    case Opcode::F32ReinterpretI32:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "Float.fromBits");
      break;

    case Opcode::I32ReinterpretF32:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toRawBits()");
      break;

    case Opcode::F64ReinterpretI64:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "Double.fromBits");
      break;

    case Opcode::I64ReinterpretF64:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toRawBits()");
      break;

    default:
      WABT_UNREACHABLE;
  }
}

void KotlinWriter::Write(const LoadExpr& expr) {
  const char* func = nullptr;
  switch (expr.opcode) {
    case Opcode::I32Load:
      func = "i32_load";
      break;
    case Opcode::I64Load:
      func = "i64_load";
      break;
    case Opcode::F32Load:
      func = "f32_load";
      break;
    case Opcode::F64Load:
      func = "f64_load";
      break;
    case Opcode::I32Load8S:
      func = "i32_load8_s";
      break;
    case Opcode::I64Load8S:
      func = "i64_load8_s";
      break;
    case Opcode::I32Load8U:
      func = "i32_load8_u";
      break;
    case Opcode::I64Load8U:
      func = "i64_load8_u";
      break;
    case Opcode::I32Load16S:
      func = "i32_load16_s";
      break;
    case Opcode::I64Load16S:
      func = "i64_load16_s";
      break;
    case Opcode::I32Load16U:
      func = "i32_load16_u";
      break;
    case Opcode::I64Load16U:
      func = "i64_load16_u";
      break;
    case Opcode::I64Load32S:
      func = "i64_load32_s";
      break;
    case Opcode::I64Load32U:
      func = "i64_load32_u";
      break;

    default:
      WABT_UNREACHABLE;
  }

  Memory* memory = module_->memories[module_->GetMemoryIndex(expr.memidx)];

  Type result_type = expr.opcode.GetResultType();
  StackValue sv = PopValue();
  DropTypes(1);
  PushType(result_type);
  std::string pos;
  std::swap(sv.value, pos);
  sv.precedence = 2;
  sv.depends_on.depends_memory = true;
  sv.side_effects.can_trap = true;
  PushValue(sv);
  WriteValue(GlobalName(memory->name), ".", func, "(", pos);
  if (expr.offset != 0)
    WriteValuef(", %d", static_cast<int32_t>(expr.offset));
  WriteValue(")");
}

void KotlinWriter::Write(const StoreExpr& expr) {
  const char* func = nullptr;
  switch (expr.opcode) {
    case Opcode::I32Store:
      func = "i32_store";
      break;
    case Opcode::I64Store:
      func = "i64_store";
      break;
    case Opcode::F32Store:
      func = "f32_store";
      break;
    case Opcode::F64Store:
      func = "f64_store";
      break;
    case Opcode::I32Store8:
      func = "i32_store8";
      break;
    case Opcode::I64Store8:
      func = "i64_store8";
      break;
    case Opcode::I32Store16:
      func = "i32_store16";
      break;
    case Opcode::I64Store16:
      func = "i64_store16";
      break;
    case Opcode::I64Store32:
      func = "i64_store32";
      break;

    default:
      WABT_UNREACHABLE;
  }

  Memory* memory = module_->memories[module_->GetMemoryIndex(expr.memidx)];

  StackValue sv_right = PopValue();
  StackValue sv_left = PopValue();
  DropTypes(2);
  SpillValues();
  Write(GlobalName(memory->name), ".", func, "(", sv_left.value);
  if (expr.offset != 0)
    Writef(", %d", static_cast<int32_t>(expr.offset));
  Write(", ", sv_right.value, ");", Newline());
}

void KotlinWriter::Write(const UnaryExpr& expr) {
  switch (expr.opcode) {
    case Opcode::I32Clz:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(),
                            ".countLeadingZeroBits()");
      break;

    case Opcode::I64Clz:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(),
                            ".countLeadingZeroBits().toLong()");
      break;

    case Opcode::I32Ctz:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(),
                            ".countTrailingZeroBits()");
      break;

    case Opcode::I64Ctz:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(),
                            ".countTrailingZeroBits().toLong()");
      break;

    case Opcode::I32Popcnt:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".countOneBits()");
      break;

    case Opcode::I64Popcnt:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(),
                            ".countOneBits().toLong()");
      break;

    case Opcode::F32Neg:
    case Opcode::F64Neg:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "-");
      break;

    case Opcode::F32Abs:
    case Opcode::F64Abs:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), WASM_RT_PKG ".abs");
      break;

    case Opcode::F32Sqrt:
    case Opcode::F64Sqrt:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "kotlin.math.sqrt");
      break;

    case Opcode::F32Ceil:
    case Opcode::F64Ceil:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), WASM_RT_PKG ".ceil");
      break;

    case Opcode::F32Floor:
    case Opcode::F64Floor:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), WASM_RT_PKG ".floor");
      break;

    case Opcode::F32Trunc:
    case Opcode::F64Trunc:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(),
                           WASM_RT_PKG ".truncate");
      break;

    case Opcode::F32Nearest:
    case Opcode::F64Nearest:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "kotlin.math.round");
      break;

    case Opcode::I32Extend8S:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toByte().toInt()");
      break;

    case Opcode::I32Extend16S:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toShort().toInt()");
      break;

    case Opcode::I64Extend8S:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toByte().toLong()");
      break;

    case Opcode::I64Extend16S:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toShort().toLong()");
      break;

    case Opcode::I64Extend32S:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toInt().toLong()");
      break;

    default:
      WABT_UNREACHABLE;
  }
}

void KotlinWriter::Write(const TernaryExpr& expr) {
  switch (expr.opcode) {
    case Opcode::V128BitSelect: {
      // Type result_type = expr.opcode.GetResultType();
      // Write(StackVar(2, result_type), " = ", "v128.bitselect", "(",
      // StackVar(0),
      //       ", ", StackVar(1), ", ", StackVar(2), ");", Newline());
      // DropTypes(3);
      // PushType(result_type);
      UNIMPLEMENTED("SIMD support");
      break;
    }
    default:
      WABT_UNREACHABLE;
  }
}

void KotlinWriter::Write(const SimdLaneOpExpr& expr) {
  Type result_type = expr.opcode.GetResultType();

  switch (expr.opcode) {
    case Opcode::I8X16ExtractLaneS:
    case Opcode::I8X16ExtractLaneU:
    case Opcode::I16X8ExtractLaneS:
    case Opcode::I16X8ExtractLaneU:
    case Opcode::I32X4ExtractLane:
    case Opcode::I64X2ExtractLane:
    case Opcode::F32X4ExtractLane:
    case Opcode::F64X2ExtractLane: {
      // Write(StackVar(0, result_type), " = ", expr.opcode.GetName(), "(",
      //       StackVar(0), ", lane Imm: ", expr.val, ");", Newline());
      // DropTypes(1);
      UNIMPLEMENTED("SIMD support");
      break;
    }
    case Opcode::I8X16ReplaceLane:
    case Opcode::I16X8ReplaceLane:
    case Opcode::I32X4ReplaceLane:
    case Opcode::I64X2ReplaceLane:
    case Opcode::F32X4ReplaceLane:
    case Opcode::F64X2ReplaceLane: {
      // Write(StackVar(1, result_type), " = ", expr.opcode.GetName(), "(",
      //       StackVar(0), ", ", StackVar(1), ", lane Imm: ", expr.val, ");",
      //       Newline());
      // DropTypes(2);
      UNIMPLEMENTED("SIMD support");
      break;
    }
    default:
      WABT_UNREACHABLE;
  }

  PushType(result_type);
}

void KotlinWriter::Write(const SimdLoadLaneExpr& expr) {
  UNIMPLEMENTED("SIMD support");
}

void KotlinWriter::Write(const SimdStoreLaneExpr& expr) {
  UNIMPLEMENTED("SIMD support");
}

void KotlinWriter::Write(const SimdShuffleOpExpr& expr) {
  // Type result_type = expr.opcode.GetResultType();
  // Write(StackVar(1, result_type), " = ", expr.opcode.GetName(), "(",
  //       StackVar(1), " ", StackVar(0), ", lane Imm: $0x%08x %08x %08x %08x",
  //       expr.val.u32(0), expr.val.u32(1), expr.val.u32(2), expr.val.u32(3),
  //       ")", Newline());
  // DropTypes(2);
  // PushType(result_type);
  UNIMPLEMENTED("SIMD support");
}

void KotlinWriter::Write(const LoadSplatExpr& expr) {
  // assert(module_->memories.size() == 1);
  // Memory* memory = module_->memories[0];

  // Type result_type = expr.opcode.GetResultType();
  // Write(StackVar(0, result_type), " = ", expr.opcode.GetName(), "(",
  //       ExternalPtr(memory->name), ", (long)(", StackVar(0));
  // if (expr.offset != 0)
  //   Write(" + ", expr.offset);
  // Write("));", Newline());
  // DropTypes(1);
  // PushType(result_type);
  UNIMPLEMENTED("SIMD support");
}

void KotlinWriter::Write(const LoadZeroExpr& expr) {
  UNIMPLEMENTED("SIMD support");
}

void KotlinWriter::WriteKotlinSource() {
  stream_ = kotlin_stream_;
  Write("/* Automatically generated by wasm2kotlin */", Newline());
  WriteSourceTop();
  WriteFuncTypes();
  WriteImports();
  WriteTags();
  AllocateFuncs();
  WriteGlobals();
  WriteMemories();
  WriteTables();
  WriteExports();
  WriteElemInitializers();
  WriteDataInitializers();
  WriteFuncs();
  WriteInit();
  WriteCallIndirectDefinitions();
  WriteSourceBottom();
}

Result KotlinWriter::WriteModule(const Module& module) {
  WABT_USE(options_);
  module_ = &module;
  WriteKotlinSource();
  return result_;
}

}  // end anonymous namespace

Result WriteKotlin(Stream* kotlin_stream,
                   const char* class_name,
                   const char* package_name,
                   const Module* module,
                   const WriteKotlinOptions& options) {
  KotlinWriter kotlin_writer(kotlin_stream, class_name, package_name, options);
  return kotlin_writer.WriteModule(*module);
}

}  // namespace wabt

// vim: set expandtab sts=-1 sw=2:
