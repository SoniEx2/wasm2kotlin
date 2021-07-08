/*
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

#include "src/cast.h"
#include "src/common.h"
#include "src/ir.h"
#include "src/literal.h"
#include "src/stream.h"
#include "src/string-view.h"

#define INDENT_SIZE 2

#define UNIMPLEMENTED(x) printf("unimplemented: %s\n", (x)), abort()

namespace wabt {

namespace {

struct Label {
  Label(LabelType label_type,
        const std::string& name,
        const TypeVector& sig,
        size_t type_stack_size,
        bool used = false)
      : label_type(label_type),
        name(name),
        sig(sig),
        type_stack_size(type_stack_size),
        used(used) {}

  bool HasValue() const {
    return !sig.empty();
  }

  LabelType label_type;
  const std::string& name;
  const TypeVector& sig;
  size_t type_stack_size;
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

struct Newline {};
struct OpenBrace {};
struct CloseBrace {};

struct StackValue {
  std::string value;
  std::set<std::string> depends_locals;
  std::set<std::string> depends_globals;
  bool depends_memory = false;
  uint8_t precedence = 0;
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
  typedef std::map<std::string, Type> TypeMap;
  typedef std::map<std::string, ExternalKind> ExtKindMap;
  typedef std::pair<Index, Type> StackTypePair;
  typedef std::map<StackTypePair, std::string> StackVarSymbolMap;

  void UseStream(Stream*);

  void WriteKotlinSource();

  size_t MarkTypeStack() const;
  void ResetTypeStack(size_t mark);
  Type StackType(Index) const;
  void PushType(Type);
  void PushTypes(const TypeVector&);
  void DropTypes(size_t count);
  void PushValue(StackValue);
  void PushVar();
  StackValue PopValue();
  void DropValue();
  void SpillValues(size_t preserve,
                   const SymbolSet& depends_locals,
                   const SymbolSet& depends_globals,
                   bool depends_memory,
                   bool is_block = false);

  void PushLabel(LabelType,
                 const std::string& name,
                 const FuncSignature&,
                 bool used = false);
  const Label* FindLabel(const Var& var);
  void PopLabel();

  static std::string AddressOf(const std::string&, const std::string&);

  static char MangleType(Type);
  static std::string MangleTypes(const TypeVector&);
  static std::string MangleName(string_view);
  static std::string MangleFuncName(string_view,
                                    const TypeVector& param_types,
                                    const TypeVector& result_types);
  static std::string MangleGlobalName(string_view, Type);
  static std::string LegalizeName(string_view);
  static std::string ExportName(string_view mangled_name);
  std::string DefineName(SymbolSet*, string_view);
  std::string DefineImportName(const std::string& name,
                               string_view module_name,
                               string_view mangled_field_name,
                               Type type);
  std::string DefineImportName(const std::string& name,
                               string_view module_name,
                               string_view mangled_field_name,
                               ExternalKind type);
  std::string DefineGlobalScopeName(const std::string&, Type);
  std::string DefineGlobalScopeName(const std::string&, ExternalKind);
  std::string DefineLocalScopeName(const std::string&);
  std::string DefineStackVarName(Index, Type, string_view);

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
  void WriteValue(string_view);
  void WriteValue(const Const&);
  void WriteValue(const GlobalVar&);
  void WriteValue(const Var&);
  void WriteValue(const LocalName&);
  void WriteValue(const GlobalName&);
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
  void Write(string_view);
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
  void WriteFuncTypes();
  //void WriteModuleImports();
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
  void WriteDataInitializers();
  void WriteElemInitializers();
  void WriteExports();
  void WriteInit();
  void WriteFuncs();
  void Write(const Func&);
  void WriteParams(const std::vector<std::string>&, std::vector<std::string>&);
  void WriteLocals(const std::vector<std::string>&, const std::vector<std::string>&);
  void WriteStackVarDeclarations();
  void Write(const ExprList&);

  void WriteSimpleUnaryExpr(Type, const char* op);
  void WritePostfixUnaryExpr(Type, const char* op);
  void WriteInfixBinaryExpr(Opcode,
                            const char* op,
                            uint8_t precedence,
                            bool = false);
  void WritePrefixBinaryExpr(Opcode, const char* op);
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

  const WriteKotlinOptions& options_;
  const Module* module_ = nullptr;
  const Func* func_ = nullptr;
  Stream* stream_ = nullptr;
  MemoryStream funkotlin_stream_;
  Stream* kotlin_stream_ = nullptr;
  std::string class_name_;
  std::string package_name_;
  Result result_ = Result::Ok;
  int indent_ = 0;
  bool should_write_indent_next_ = false;

  TypeMap type_map_;
  ExtKindMap extkind_map_;
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
  std::vector<StackValue> value_stack_;
};

static const char kImplicitFuncLabel[] = "$Bfunc";

#define SECTION_NAME(x) s_source_##x
#include "src/prebuilt/wasm2kotlin.include.kt"
#undef SECTION_NAME

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
  value.depends_memory = false;
  value_stack_.push_back(value);
}


StackValue KotlinWriter::PopValue() {
  assert(!value_stack_.empty());
  StackValue ret = value_stack_.back();
  value_stack_.pop_back();
  return ret;
}

void KotlinWriter::SpillValues(size_t preserve_count,
                   const SymbolSet& depends_locals,
                   const SymbolSet& depends_globals,
                   bool depends_memory,
                   bool is_block) {
  // function calls will pass depends_locals=empty, depends_globals=global_sym_map_, depends_memory=true
  // memory writes will pass depends_locals=empty, depends_globals=empty, depends_memory=true
  // global writes will pass depends_locals=empty, depends_globals=the_global, depends_memory=false
  // local writes will pass depends_locals=the_local, depends_globals=empty, depends_memory=false
  // globals are thread-local, locals are scope-local, so this is "safe"
  // preserve_count values will be preserved
  assert(depends_locals.size() <= 1 || depends_locals == local_syms_);
  assert(depends_globals.size() <= 1 || depends_globals == global_syms_);
  assert(value_stack_.size() == type_stack_.size());
  assert(preserve_count <= value_stack_.size());
  Index max = value_stack_.size() - preserve_count;
  bool all_globals = depends_globals == global_syms_;
  bool all_locals = depends_locals == local_syms_;
  for (Index i = 0; i < max; ++i) {
    StackValue& value = value_stack_[i];
    if (value.precedence == 0) {
      continue;
    }
    bool do_spill = is_block
                    || (depends_memory && value.depends_memory)
                    || (all_globals && !value.depends_globals.empty())
                    || (all_locals && !value.depends_locals.empty())
                    || (!depends_locals.empty() && value.depends_locals.find(*depends_locals.begin()) != value.depends_locals.end())
                    || (!depends_globals.empty() && value.depends_globals.find(*depends_globals.begin()) != value.depends_globals.end());
    if (do_spill) {
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
      value.depends_locals.clear();
      value.depends_globals.clear();
      value.depends_memory = false;
      value.precedence = 0;
      Write(value.value, " = ", thing, ";", Newline());
    }
  }
}

void KotlinWriter::PushLabel(LabelType label_type,
                        const std::string& name,
                        const FuncSignature& sig,
                        bool used) {
  // TODO(Soni): Add multi-value support.
  if ((label_type != LabelType::Func && sig.GetNumParams() != 0) ||
      sig.GetNumResults() > 1) {
    UNIMPLEMENTED("multi value support");
  }

  if (label_type == LabelType::Loop)
    label_stack_.emplace_back(label_type, name, sig.param_types,
                              type_stack_.size(), used);
  else
    label_stack_.emplace_back(label_type, name, sig.result_types,
                              type_stack_.size(), used);
}

const Label* KotlinWriter::FindLabel(const Var& var) {
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
  label->used = true;
  return label;
}

void KotlinWriter::PopLabel() {
  label_stack_.pop_back();
}

// static
std::string KotlinWriter::AddressOf(const std::string& s, const std::string& class_name) {
  return "this@" + class_name + "::" + s + "";
}

// static
char KotlinWriter::MangleType(Type type) {
  switch (type) {
    case Type::I32: return 'i';
    case Type::I64: return 'j';
    case Type::F32: return 'f';
    case Type::F64: return 'd';
    default: WABT_UNREACHABLE;
  }
}

// static
std::string KotlinWriter::MangleTypes(const TypeVector& types) {
  if (types.empty())
    return std::string("v");

  std::string result;
  for (auto type : types) {
    result += MangleType(type);
  }
  return result;
}

// static
std::string KotlinWriter::MangleName(string_view name) {
  const char kPrefix = 'Z';
  std::string result = "Z_";

  if (!name.empty()) {
    for (char c : name) {
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
std::string KotlinWriter::MangleFuncName(string_view name,
                                    const TypeVector& param_types,
                                    const TypeVector& result_types) {
  std::string sig = MangleTypes(result_types) + MangleTypes(param_types);
  return MangleName(name) + MangleName(sig);
}

// static
std::string KotlinWriter::MangleGlobalName(string_view name, Type type) {
  std::string sig(1, MangleType(type));
  return MangleName(name) + MangleName(sig);
}

// static
std::string KotlinWriter::ExportName(string_view mangled_name) {
  return mangled_name.to_string();
}

// static
std::string KotlinWriter::LegalizeName(string_view name) {
  if (name.empty())
    return "w2k_";

  std::string result;
  result = isalpha(name[0]) ? name[0] : '_';
  for (size_t i = 1; i < name.size(); ++i)
    result += isalnum(name[i]) ? name[i] : '_';

  // In addition to containing valid characters for C, we must also avoid
  // colliding with things C cares about, such as reserved words (e.g. "void")
  // or a function name like main() (which a compiler will  complain about if we
  // define it with another type). To avoid such problems, prefix.
  result = "w2k_" + result;

  return result;
}

std::string KotlinWriter::DefineName(SymbolSet* set, string_view name) {
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

string_view StripLeadingDollar(string_view name) {
  if (!name.empty() && name[0] == '$') {
    name.remove_prefix(1);
  }
  return name;
}

std::string KotlinWriter::DefineImportName(const std::string& name,
                                      string_view module,
                                      string_view mangled_field_name,
                                      Type type) {
  std::string mangled = mangled_field_name.to_string();
  import_syms_.insert(name);
  std::string unique = DefineName(&global_syms_, mangled);
  global_sym_map_.insert(SymbolMap::value_type(name, unique));
  type_map_.insert(TypeMap::value_type(unique, type));
  return unique;
}

std::string KotlinWriter::DefineImportName(const std::string& name,
                                      string_view module,
                                      string_view mangled_field_name,
                                      ExternalKind type) {
  std::string mangled = mangled_field_name.to_string();
  import_syms_.insert(name);
  std::string unique = DefineName(&global_syms_, mangled);
  global_sym_map_.insert(SymbolMap::value_type(name, unique));
  extkind_map_.insert(ExtKindMap::value_type(unique, type));
  return unique;
}

std::string KotlinWriter::DefineGlobalScopeName(const std::string& name, Type type) {
  std::string unique = DefineName(&global_syms_, StripLeadingDollar(name));
  global_sym_map_.insert(SymbolMap::value_type(name, unique));
  type_map_.insert(TypeMap::value_type(unique, type));
  return unique;
}

std::string KotlinWriter::DefineGlobalScopeName(const std::string& name, ExternalKind type) {
  std::string unique = DefineName(&global_syms_, StripLeadingDollar(name));
  global_sym_map_.insert(SymbolMap::value_type(name, unique));
  extkind_map_.insert(ExtKindMap::value_type(unique, type));
  return unique;
}

std::string KotlinWriter::DefineLocalScopeName(const std::string& name) {
  std::string unique = DefineName(&local_syms_, StripLeadingDollar(name));
  local_sym_map_.insert(SymbolMap::value_type(name, unique));
  return unique;
}

std::string KotlinWriter::DefineStackVarName(Index index,
                                        Type type,
                                        string_view name) {
  std::string unique = DefineName(&local_syms_, name);
  StackTypePair stp = {index, type};
  stack_var_sym_map_.insert(StackVarSymbolMap::value_type(stp, unique));
  return unique;
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

void WABT_PRINTF_FORMAT(2, 3) KotlinWriter::WriteValuef(const char* format, ...) {
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

void KotlinWriter::Write(string_view s) {
  WriteData(s.data(), s.size());
}

void KotlinWriter::WriteValue(string_view s) {
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

//Type KotlinWriter::TypeOf(const std::string& name) const {
//  assert(type_map_.count(name) == 1);
//  auto iter = type_map_.find(name);
//  assert(iter != type_map_.end());
//  return iter->second;
//}

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
    assert(label->sig.size() == 1);
    assert(type_stack_.size() >= label->type_stack_size);
    Index dst = type_stack_.size() - label->type_stack_size - 1;
    StackValue sv = PopValue();
    if (sv.precedence != 0 || dst != 0) {
      Write(StackVar(dst, label->sig[0]), " = ", sv.value, "; ");
    }
    PushValue(sv);
  }

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
    Write("break@", Var(kImplicitFuncLabel), ";");
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
    case Type::I32: Write("Int"); break;
    case Type::I64: Write("Long"); break;
    case Type::F32: Write("Float"); break;
    case Type::F64: Write("Double"); break;
    default:
      WABT_UNREACHABLE;
  }
}

void KotlinWriter::WriteValue(Type type) {
  switch (type) {
    case Type::I32: WriteValue("Int"); break;
    case Type::I64: WriteValue("Long"); break;
    case Type::F32: WriteValue("Float"); break;
    case Type::F64: WriteValue("Double"); break;
    default:
      WABT_UNREACHABLE;
  }
}

void KotlinWriter::Write(TypeEnum type) {
  switch (type.type) {
    case Type::I32: Write("Int::class"); break;
    case Type::I64: Write("Long::class"); break;
    case Type::F32: Write("Float::class"); break;
    case Type::F64: Write("Double::class"); break;
    default:
      WABT_UNREACHABLE;
  }
}

void KotlinWriter::Write(const ResultType& rt) {
  if (!rt.types.empty()) {
    Write(rt.types[0]);
  } else {
    Write("Unit");
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
          if (f64_bits == Bitcast<uint64_t>(std::numeric_limits<int64_t>::min())) {
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
          if (f64_bits == Bitcast<uint64_t>(std::numeric_limits<int64_t>::min())) {
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
  Write("class ", class_name_, " (moduleRegistry: wasm_rt_impl.ModuleRegistry, name: String) {");
  //WriteModuleImports();
  //Write("){");
  Indent();
}

//void KotlinWriter::WriteModuleImports() {
//  if (module_->imports.empty())
//    return;
//
//  int count = 0;
//
//  for (const Import* import : module_->imports) {
//    std::string legal = LegalizeName(import->module_name);
//    if (module_import_syms_.find(legal) == module_import_syms_.end()) {
//      module_import_syms_.insert(legal);
//      module_import_sym_map_.insert(SymbolMap::value_type(import->module_name, legal));
//      if (count != 0) {
//        Write(", ");
//      }
//      Write(legal, ": ", MangleName(import->module_name));
//      count++;
//    }
//  }
//}

//std::string KotlinWriter::GetModuleName(const std::string& name) const {
//  assert(module_import_sym_map_.count(name) == 1);
//  auto iter = module_import_sym_map_.find(name);
//  assert(iter != module_import_sym_map_.end());
//  return iter->second;
//}

void KotlinWriter::WriteImport(const char* type, const std::string& module, const std::string& mangled, bool delegate) {
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
  Write(Newline());
  Writef("private val func_types: IntArray = IntArray(%" PRIzd ")", module_->types.size());
  Write(Newline(), Newline());
  Write("init /* func_types */ {", Newline());
  Index func_type_index = 0;
  for (TypeEntry* type : module_->types) {
    FuncType* func_type = cast<FuncType>(type);
    Index num_params = func_type->GetNumParams();
    Index num_results = func_type->GetNumResults();
    Write("  func_types[", func_type_index, "] = wasm_rt_impl.register_func_type(",
          num_params, ", ", num_results);
    for (Index i = 0; i < num_params; ++i) {
      Write(", ", TypeEnum(func_type->GetParamType(i)));
    }

    for (Index i = 0; i < num_results; ++i) {
      Write(", ", TypeEnum(func_type->GetResultType(i)));
    }

    Write(");", Newline());
    ++func_type_index;
  }
  Write("}", Newline());
}

void KotlinWriter::WriteImports() {
  if (module_->imports.empty())
    return;

  Write(Newline());

  // TODO(binji): Write imports ordered by type.
  for (const Import* import : module_->imports) {
    Write("/* import: '", import->module_name, "' '", import->field_name,
          "' */", Newline());
    Write("private var ");
    std::string mangled;
    const char *type;
    bool delegate = true;
    switch (import->kind()) {
      case ExternalKind::Func: {
        const Func& func = cast<FuncImport>(import)->func;
        mangled = MangleFuncName(
                                      import->field_name,
                                      func.decl.sig.param_types,
                                      func.decl.sig.result_types);
        std::string name = DefineImportName(
                func.name, import->module_name,
                mangled, Type::Func);
        Write(name, ": ");
        WriteFuncType(func.decl);
        type = "Func";
        delegate = false;
        break;
      }

      case ExternalKind::Global: {
        const Global& global = cast<GlobalImport>(import)->global;
        mangled = MangleGlobalName(import->field_name, global.type);
        WriteGlobal(global,
                    DefineImportName(
                        global.name, import->module_name,
                        mangled, global.type));
        type = "Global";
        break;
      }

      case ExternalKind::Memory: {
        const Memory& memory = cast<MemoryImport>(import)->memory;
        mangled = MangleName(import->field_name);
        WriteMemory(DefineImportName(memory.name, import->module_name,
                                     mangled, ExternalKind::Memory));
        type = "Memory";
        break;
      }

      case ExternalKind::Table: {
        const Table& table = cast<TableImport>(import)->table;
        mangled = MangleName(import->field_name);
        WriteTable(DefineImportName(table.name, import->module_name,
                                    mangled, ExternalKind::Table));
        type = "Table";
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
      DefineGlobalScopeName(func->name, Type::Func);
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
        Write("private var ");
        WriteGlobal(*global, DefineGlobalScopeName(global->name, global->type));
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

  assert(module_->memories.size() <= 1);
  Index memory_index = 0;
  for (const Memory* memory : module_->memories) {
    bool is_import = memory_index < module_->num_memory_imports;
    if (!is_import) {
      Write("private var ");
      WriteMemory(DefineGlobalScopeName(memory->name, ExternalKind::Memory));
      Write(" = wasm_rt_impl.Memory(0, 0);");
      Write(Newline());
    }
    ++memory_index;
  }
}

void KotlinWriter::WriteMemory(const std::string& name) {
  Write(name, ": wasm_rt_impl.Memory");
}

void KotlinWriter::WriteTables() {
  if (module_->tables.size() == module_->num_table_imports)
    return;

  Write(Newline());

  assert(module_->tables.size() <= 1);
  Index table_index = 0;
  for (const Table* table : module_->tables) {
    bool is_import = table_index < module_->num_table_imports;
    if (!is_import) {
      Write("private var ");
      WriteTable(DefineGlobalScopeName(table->name, ExternalKind::Table));
      Write(" = wasm_rt_impl.Table(0, 0);");
      Write(Newline());
    }
    ++table_index;
  }
}

void KotlinWriter::WriteTable(const std::string& name) {
  Write(name, ": wasm_rt_impl.Table");
}

void KotlinWriter::WriteDataInitializers() {
  const Memory* memory = nullptr;
  Index data_segment_index = 0;

  if (!module_->memories.empty()) {
    if (module_->data_segments.empty()) {
      Write(Newline());
    } else {
      for (const DataSegment* data_segment : module_->data_segments) {
        Write(Newline(), "private val data_segment_data_",
              data_segment_index, ": String = \"");
        size_t i = 0;
        uint8_t bottom = 0;
        for (uint8_t x : data_segment->data) {
          if ((i++ % 2) == 0) {
            Write("\\u");
            bottom = x;
          } else {
            // NOTE(Soni): we *want* these swapped because reasons
            Writef("%02hhx%02hhx", x, bottom);
          }
        }
        if ((i % 2) == 1) {
          Writef("00%02hhx", bottom);
        }
        Write("\";", Newline());
        ++data_segment_index;
      }
    }

    memory = module_->memories[0];
  }

  Write(Newline(), "init /* memory */ ", OpenBrace());
  if (memory && module_->num_memory_imports == 0) {
    uint32_t max =
        memory->page_limits.has_max ? memory->page_limits.max : 65536;
    Write("wasm_rt_impl.allocate_memory(", ExternalPtr(memory->name), ", ",
          memory->page_limits.initial, ", ");
    Writef("%d", static_cast<int32_t>(max));
    Write(");", Newline());
  }
  data_segment_index = 0;
  for (const DataSegment* data_segment : module_->data_segments) {
    Write(GetGlobalName(memory->name), ".put(");
    WriteInitExpr(data_segment->offset);
    Write(", data_segment_data_", data_segment_index, ", ",
          data_segment->data.size(), ");", Newline());
    ++data_segment_index;
  }

  Write(CloseBrace(), Newline());
}

void KotlinWriter::WriteElemInitializers() {
  const Table* table = module_->tables.empty() ? nullptr : module_->tables[0];

  Write(Newline(), "init /* table */ ", OpenBrace());
  Write("var offset: Int = 0;", Newline());
  if (table && module_->num_table_imports == 0) {
    uint32_t max =
        table->elem_limits.has_max ? table->elem_limits.max : UINT32_MAX;
    Write("wasm_rt_impl.allocate_table(", ExternalPtr(table->name), ", ",
          table->elem_limits.initial, ", ");
    Writef("%d", static_cast<int32_t>(max));
    Write(");", Newline());
  }
  Index elem_segment_index = 0;
  for (const ElemSegment* elem_segment : module_->elem_segments) {
    Write("offset = ");
    WriteInitExpr(elem_segment->offset);
    Write(";", Newline());

    size_t i = 0;
    for (const ElemExpr& elem_expr : elem_segment->elem_exprs) {
      // We don't support the bulk-memory proposal here, so we know that we
      // don't have any passive segments (where ref.null can be used).
      assert(elem_expr.kind == ElemExprKind::RefFunc);
      const Func* func = module_->GetFunc(elem_expr.var);
      Index func_type_index = module_->GetFuncTypeIndex(func->decl.type_var);
      bool is_import = import_syms_.count(func->name) != 0;

      Write(GetGlobalName(table->name), "[offset + ", i,
            "] = wasm_rt_impl.Elem(func_types[", func_type_index,
            "], ");
      if (!is_import) {
        Write(ExternalPtr(func->name));
      } else {
        Write(GlobalName(func->name));
      }
      Write(");", Newline());
      ++i;
    }
    ++elem_segment_index;
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
    bool external_ptr = true;

    switch (export_->kind) {
      case ExternalKind::Func: {
        const Func* func = module_->GetFunc(export_->var);
        mangled_name =
            ExportName(MangleFuncName(export_->name, func->decl.sig.param_types,
                                      func->decl.sig.result_types));
        internal_name = func->name;
        external_ptr = import_syms_.count(func->name) == 0;
        type = "Func";
        break;
      }

      case ExternalKind::Global: {
        const Global* global = module_->GetGlobal(export_->var);
        mangled_name =
            ExportName(MangleGlobalName(export_->name, global->type));
        internal_name = global->name;
        type = "Global";
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

void KotlinWriter::Write(const Func& func) {
  func_ = &func;
  // Copy symbols from global symbol table so we don't shadow them.
  local_syms_ = global_syms_;
  local_sym_map_.clear();
  stack_var_sym_map_.clear();

  std::vector<std::string> index_to_name;
  std::vector<std::string> to_shadow;
  MakeTypeBindingReverseMapping(func_->GetNumParamsAndLocals(), func_->bindings,
                                &index_to_name);

  Write("fun ", GlobalName(func.name), "(");
  WriteParams(index_to_name, to_shadow);
  Write(": ", ResultType(func.decl.sig.result_types), OpenBrace());
  WriteLocals(index_to_name, to_shadow);
  Write("try ", OpenBrace());

  stream_ = &funkotlin_stream_;
  stream_->ClearOffset();

  std::string label = DefineLocalScopeName(kImplicitFuncLabel);
  value_stack_.clear();
  ResetTypeStack(0);
  std::string empty;  // Must not be temporary, since address is taken by Label.
  PushLabel(LabelType::Func, empty, func.decl.sig);
  Write(LabelDecl(label), "do ", OpenBrace());
  Write(func.exprs);
  //if (func.GetNumResults() > 0 && IsConst(0)) {
  //  Const const_ = PopConst();
  //  Write(StackVar(0, const_.type()), " = ", const_, ";", Newline());
  //}
  SpillValues(0, local_syms_, global_syms_, true, true);
  while (value_stack_.size() > 0) {
    DropValue();
  }
  Write(CloseBrace(), " while (false);", Newline());
  PopLabel();
  ResetTypeStack(0);
  PushTypes(func.decl.sig.result_types);
  while (value_stack_.size() < type_stack_.size()) {
    PushVar();
  }

  if (!func.decl.sig.result_types.empty()) {
    // Return the top of the stack implicitly.
    Write("return ", StackVar(0), ";", Newline());
  }

  stream_ = kotlin_stream_;

  WriteStackVarDeclarations();

  std::unique_ptr<OutputBuffer> buf = funkotlin_stream_.ReleaseOutputBuffer();
  stream_->WriteData(buf->data.data(), buf->data.size());

  Write(CloseBrace(), " catch(e: StackOverflowError) { throw wasm_rt_impl.ExhaustionException(null, e) }", Newline());

  Write(CloseBrace());

  funkotlin_stream_.Clear();
  func_ = nullptr;
}

void KotlinWriter::WriteParams(const std::vector<std::string>& index_to_name, std::vector<std::string>& to_shadow) {
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

void KotlinWriter::WriteLocals(const std::vector<std::string>& index_to_name, const std::vector<std::string>& to_shadow) {
  if (!to_shadow.empty()) {
    for (auto param : to_shadow) {
      Write("var ", param, " = ", param, ";", Newline());
    }
  }
  Index num_params = func_->GetNumParams();
  for (Type type : {Type::I32, Type::I64, Type::F32, Type::F64}) {
    Index local_index = 0;
    for (Type local_type : func_->local_types) {
      if (local_type == type) {
        Write("var ", DefineLocalScopeName(index_to_name[num_params + local_index]),
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

void KotlinWriter::WriteStackVarDeclarations() {
  for (Type type : {Type::I32, Type::I64, Type::F32, Type::F64}) {
    size_t count = 0;
    for (const auto& pair : stack_var_sym_map_) {
      Type stp_type = pair.first.second;
      const std::string& name = pair.second;

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
  // as it turns out these can trap, which is, in fact, a side-effect!
  StackValue sv = PopValue();
  // ...unless it's already been spilled or is a local/global
  if (sv.precedence > 1) Write(sv.value, ";", Newline());
}

void KotlinWriter::Write(const ExprList& exprs) {
  for (const Expr& expr : exprs) {
    switch (expr.type()) {
      case ExprType::Binary:
        Write(*cast<BinaryExpr>(&expr));
        break;

      case ExprType::Block: {
        const Block& block = cast<BlockExpr>(&expr)->block;
        std::string label = DefineLocalScopeName(block.label);
        size_t mark = MarkTypeStack();
        PushLabel(LabelType::Block, block.label, block.decl.sig);
        SpillValues(0, local_syms_, global_syms_, true, true);
        Write(LabelDecl(label), "do ", OpenBrace());
        Write(block.exprs);
        if (value_stack_.size() > mark) {
          SpillValues(0, local_syms_, global_syms_, true, true);
          while (value_stack_.size() > mark) {
            DropValue();
          }
        }
        Write(CloseBrace(), " while (false);", Newline());
        ResetTypeStack(mark);
        PopLabel();
        PushTypes(block.decl.sig.result_types);
        while (value_stack_.size() < type_stack_.size()) {
          PushVar();
        }
        break;
      }

      case ExprType::Br:
        Write(GotoLabel(cast<BrExpr>(&expr)->var), Newline());
        // Stop processing this ExprList, since the following are unreachable.
        return;

      case ExprType::BrIf:
        Write("if ((", PopValue().value, ").inz()) {");
        DropTypes(1);
        Write(GotoLabel(cast<BrIfExpr>(&expr)->var), "}", Newline());
        break;

      case ExprType::BrTable: {
        const auto* bt_expr = cast<BrTableExpr>(&expr);
        SpillValues(1, local_syms_, global_syms_, true, true);
        Write("when (", PopValue().value, ") ", OpenBrace());
        DropTypes(1);
        Index i = 0;
        for (const Var& var : bt_expr->targets) {
          Write(i++, " -> ", OpenBrace(), GotoLabel(var), CloseBrace(), Newline());
        }
        Write("else -> ", OpenBrace());
        Write(GotoLabel(bt_expr->default_target), CloseBrace(), Newline(), CloseBrace(),
              Newline());
        // Stop processing this ExprList, since the following are unreachable.
        return;
      }

      case ExprType::Call: {
        const Var& var = cast<CallExpr>(&expr)->var;
        const Func& func = *module_->GetFunc(var);
        Index num_params = func.GetNumParams();
        Index num_results = func.GetNumResults();
        assert(type_stack_.size() >= num_params);
        SymbolSet depends_locals;
        SpillValues(num_params, depends_locals, global_syms_, true);
        if (num_results > 0) {
          assert(num_results == 1);
          Write(StackVar(num_params - 1, func.GetResultType(0)), " = ");
        }

        Write(GlobalName(var.name()), "(");
        std::vector<std::string> args;
        for (Index i = 0; i < num_params; ++i) {
          args.push_back(PopValue().value);
        }
        for (Index i = 0; i < num_params; ++i) {
          Write(args.back(), ", ");
          args.pop_back();
        }
        Write(");", Newline());
        DropTypes(num_params);
        PushTypes(func.decl.sig.result_types);
        while (value_stack_.size() < type_stack_.size()) {
          PushVar();
        }
        break;
      }

      case ExprType::CallIndirect: {
        const FuncDeclaration& decl = cast<CallIndirectExpr>(&expr)->decl;
        Index num_params = decl.GetNumParams();
        Index num_results = decl.GetNumResults();
        assert(type_stack_.size() > num_params);
        SymbolSet depends_locals;
        SpillValues(num_params + 1, depends_locals, global_syms_, true);
        if (num_results > 0) {
          assert(num_results == 1);
          Write(StackVar(num_params, decl.GetResultType(0)), " = ");
        }

        assert(module_->tables.size() == 1);
        const Table* table = module_->tables[0];

        assert(decl.has_func_type);
        Index func_type_index = module_->GetFuncTypeIndex(decl.type_var);

        Write("wasm_rt_impl.CALL_INDIRECT<");
        WriteFuncType(decl);
        Write(">(", GetGlobalName(table->name), ", func_types[");
        Write(func_type_index, "], ", PopValue().value, ")(");
        std::vector<std::string> args;
        for (Index i = 0; i < num_params; ++i) {
          args.push_back(PopValue().value);
        }
        for (Index i = 0; i < num_params; ++i) {
          Write(args.back(), ", ");
          args.pop_back();
        }
        Write(");", Newline());
        DropTypes(num_params + 1);
        PushTypes(decl.sig.result_types);
        while (value_stack_.size() < type_stack_.size()) {
          PushVar();
        }
        break;
      }

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
        sv.depends_globals.insert(var.name());
        PushValue(sv);
        WriteValue(GlobalVar(var));
        break;
      }

      case ExprType::GlobalSet: {
        const Var& var = cast<GlobalSetExpr>(&expr)->var;
        assert(var.is_name());
        SymbolSet depends_locals;
        SymbolSet depends_globals { var.name() };
        SpillValues(1, depends_locals, depends_globals, false);
        StackValue sv = PopValue();
        Write(GlobalVar(var), " = ", sv.value, ";", Newline());
        DropTypes(1);
        break;
      }

      case ExprType::If: {
        const IfExpr& if_ = *cast<IfExpr>(&expr);
        std::string label = DefineLocalScopeName(if_.true_.label);
        // spill everything before block.
        SpillValues(1, local_syms_, global_syms_, true, true);
        // FIXME(Soni) put the block INSIDE the if.
        Write(LabelDecl(label), "do ", OpenBrace());
        Write("if ((", PopValue().value, ").inz()) ", OpenBrace());
        DropTypes(1);
        size_t mark = MarkTypeStack();
        PushLabel(LabelType::If, if_.true_.label, if_.true_.decl.sig);
        Write(if_.true_.exprs);
        if (!if_.false_.empty()) {
          if (value_stack_.size() > mark) {
            // FIXME(Soni) this is lazy and not very efficient.
            SpillValues(0, local_syms_, global_syms_, true, true);
            while (value_stack_.size() > mark) {
              DropValue();
            }
          }
          Write(CloseBrace());
          ResetTypeStack(mark);
          Write(" else ", OpenBrace(), if_.false_);
        }
        if (value_stack_.size() > mark) {
          // FIXME(Soni) this is lazy and not very efficient.
          SpillValues(0, local_syms_, global_syms_, true, true);
          while (value_stack_.size() > mark) {
            DropValue();
          }
        }
        Write(CloseBrace());
        Write(CloseBrace(), " while (false);");
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
        sv.depends_locals.insert(var.name());
        PushValue(sv);
        WriteValue(var);
        break;
      }

      case ExprType::LocalSet: {
        const Var& var = cast<LocalSetExpr>(&expr)->var;
        assert(var.is_name());
        SymbolSet depends_locals { var.name() };
        SymbolSet depends_globals;
        SpillValues(1, depends_locals, depends_globals, false);
        StackValue sv = PopValue();
        Write(var, " = ", sv.value, ";", Newline());
        DropTypes(1);
        break;
      }

      case ExprType::LocalTee: {
        const Var& var = cast<LocalTeeExpr>(&expr)->var;
        assert(var.is_name());
        SymbolSet depends_locals { var.name() };
        SymbolSet depends_globals;
        SpillValues(1, depends_locals, depends_globals, false);
        StackValue sv = PopValue();
        Write(var, " = ", sv.value, ";", Newline());
        StackValue new_sv;
        new_sv.precedence = 1;
        new_sv.depends_locals.insert(var.name());
        PushValue(new_sv);
        WriteValue(var);
        break;
      }

      case ExprType::Loop: {
        const Block& block = cast<LoopExpr>(&expr)->block;
        if (!block.exprs.empty()) {
          std::string label = DefineLocalScopeName(block.label);
          SpillValues(0, local_syms_, global_syms_, true, true);
          Write(LabelDecl(label), "while (true) ", OpenBrace());
          size_t mark = MarkTypeStack();
          PushLabel(LabelType::Loop, block.label, block.decl.sig);
          Write(block.exprs);
          if (value_stack_.size() > mark) {
            SpillValues(0, local_syms_, global_syms_, true, true);
            while (value_stack_.size() > mark) {
              DropValue();
            }
          }
          ResetTypeStack(mark);
          PopLabel();
          PushTypes(block.decl.sig.result_types);
          while (value_stack_.size() < type_stack_.size()) {
            PushVar();
          }
          Write("break@", label, ";", Newline());
          Write(CloseBrace(), Newline());
        }
        break;
      }

      case ExprType::MemoryCopy:
      case ExprType::DataDrop:
      case ExprType::MemoryInit:
      case ExprType::MemoryFill:
      case ExprType::TableCopy:
      case ExprType::ElemDrop:
      case ExprType::TableInit:
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
        assert(module_->memories.size() == 1);
        Memory* memory = module_->memories[0];
        SymbolSet depends_locals;
        SymbolSet depends_globals;
        // spill memory accesses
        SpillValues(0, depends_locals, depends_globals, true);

        assert(StackType(0) == Type::I32);
        Write(StackVar(0, Type::I32), " = wasm_rt_impl.grow_memory(", ExternalPtr(memory->name),
              ", ", PopValue().value, ");", Newline());
        DropTypes(1);
        PushType(Type::I32);
        PushVar();
        break;
      }

      case ExprType::MemorySize: {
        assert(module_->memories.size() == 1);
        Memory* memory = module_->memories[0];

        PushType(Type::I32);
        StackValue sv;
        sv.precedence = 2;
        sv.depends_memory = true;
        PushValue(sv);
        WriteValue(GetGlobalName(memory->name), ".pages");
        break;
      }

      case ExprType::Nop:
        break;

      case ExprType::Return:
        // Goto the function label instead; this way we can do shared function
        // cleanup code in one place.
        Write(GotoLabel(Var(label_stack_.size() - 1)), Newline());
        // Stop processing this ExprList, since the following are unreachable.
        return;

      case ExprType::Select: {
        Type type = StackType(1);
        // do we have to spill everything due to order of evaluation?
        //SpillValues(0, local_syms_, global_syms_, true, true);
        StackValue first = PopValue();
        StackValue second = PopValue();
        StackValue third = PopValue();
        //Write(StackVar(2), " = if ((", first.value, ").inz()) (", third.value, ") else (",
        //      second.value, ");", Newline());
        DropTypes(3);
        PushType(type);
        //PushVar();
        StackValue sv;
        sv.precedence = 1;
        sv.depends_locals.insert(first.depends_locals.begin(), first.depends_locals.end());
        sv.depends_locals.insert(second.depends_locals.begin(), second.depends_locals.end());
        sv.depends_locals.insert(third.depends_locals.begin(), third.depends_locals.end());
        sv.depends_globals.insert(first.depends_globals.begin(), first.depends_globals.end());
        sv.depends_globals.insert(second.depends_globals.begin(), second.depends_globals.end());
        sv.depends_globals.insert(third.depends_globals.begin(), third.depends_globals.end());
        sv.depends_memory = first.depends_memory | second.depends_memory | third.depends_memory;
        PushValue(sv);
        WriteValue("select(", third.value, ", ", second.value, ", ", first.value, ")");
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

      case ExprType::Unreachable:
        Write("throw wasm_rt_impl.UnreachableException(\"unreachable\");", Newline());
        return;

      case ExprType::AtomicLoad:
      case ExprType::AtomicRmw:
      case ExprType::AtomicRmwCmpxchg:
      case ExprType::AtomicStore:
      case ExprType::AtomicWait:
      case ExprType::AtomicFence:
      case ExprType::AtomicNotify:
      case ExprType::Rethrow:
      case ExprType::ReturnCall:
      case ExprType::ReturnCallIndirect:
      case ExprType::Throw:
      case ExprType::Try:
        UNIMPLEMENTED("...");
        break;
    }
  }
}

void KotlinWriter::WriteSimpleUnaryExpr(Type result_type, const char* op) {
  StackValue sv = PopValue();
  DropTypes(1);
  PushType(result_type);
  sv.value = op + ("(" + sv.value) + ")";
  sv.precedence = 3;
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
  sv_left.depends_locals.insert(sv_right.depends_locals.begin(), sv_right.depends_locals.end());
  sv_left.depends_globals.insert(sv_right.depends_globals.begin(), sv_right.depends_globals.end());
  sv_left.depends_memory |= sv_right.depends_memory;
  if (debooleanize) {
    sv_left.precedence = 2;
    sv_left.value = ("(" + sv_left.value) + ")";
  }
  PushValue(sv_left);
  if (debooleanize) {
    WriteValue(".bto", result_type, "()");
  }
}

void KotlinWriter::WritePrefixBinaryExpr(Opcode opcode, const char* op) {
  Type result_type = opcode.GetResultType();
  StackValue sv_right = PopValue();
  StackValue sv_left = PopValue();
  DropTypes(2);
  PushType(result_type);
  sv_left.value = op + ("(" + sv_left.value) + ", " + sv_right.value + ")";
  sv_left.precedence = 2;
  sv_left.depends_locals.insert(sv_right.depends_locals.begin(), sv_right.depends_locals.end());
  sv_left.depends_globals.insert(sv_right.depends_globals.begin(), sv_right.depends_globals.end());
  sv_left.depends_memory |= sv_right.depends_memory;
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
  sv_left.depends_locals.insert(sv_right.depends_locals.begin(), sv_right.depends_locals.end());
  sv_left.depends_globals.insert(sv_right.depends_globals.begin(), sv_right.depends_globals.end());
  sv_left.depends_memory |= sv_right.depends_memory;
  std::string oldleft;
  std::swap(oldleft, sv_left.value);
  PushValue(sv_left);
  WriteValue("(", cls, ".compareUnsigned(", oldleft, ", ", sv_right.value, ")", op, "0).bto", result_type, "()");
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
      WritePrefixBinaryExpr(expr.opcode, "wasm_rt_impl.I32_DIV_S");
      break;

    case Opcode::I64DivS:
      WritePrefixBinaryExpr(expr.opcode, "wasm_rt_impl.I64_DIV_S");
      break;

    case Opcode::I32DivU:
    case Opcode::I64DivU:
      WritePrefixBinaryExpr(expr.opcode, "wasm_rt_impl.DIV_U");
      break;

    case Opcode::F32Div:
    case Opcode::F64Div:
      WriteInfixBinaryExpr(expr.opcode, "/", 4);
      break;

    case Opcode::I32RemS:
      WritePrefixBinaryExpr(expr.opcode, "wasm_rt_impl.I32_REM_S");
      break;

    case Opcode::I64RemS:
      WritePrefixBinaryExpr(expr.opcode, "wasm_rt_impl.I64_REM_S");
      break;

    case Opcode::I32RemU:
    case Opcode::I64RemU:
      WritePrefixBinaryExpr(expr.opcode, "wasm_rt_impl.REM_U");
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
      WritePrefixBinaryExpr(expr.opcode, "wasm_rt_impl.I32_ROTL");
      break;

    case Opcode::I64Rotl:
      WritePrefixBinaryExpr(expr.opcode, "wasm_rt_impl.I64_ROTL");
      break;

    case Opcode::I32Rotr:
      WritePrefixBinaryExpr(expr.opcode, "wasm_rt_impl.I32_ROTR");
      break;

    case Opcode::I64Rotr:
      WritePrefixBinaryExpr(expr.opcode, "wasm_rt_impl.I64_ROTR");
      break;

    case Opcode::F32Min:
    case Opcode::F64Min:
      WritePrefixBinaryExpr(expr.opcode, "wasm_rt_impl.MIN");
      break;

    case Opcode::F32Max:
    case Opcode::F64Max:
      WritePrefixBinaryExpr(expr.opcode, "wasm_rt_impl.MAX");
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
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toLong().and(0xFFFFFFFFL)");
      break;

    case Opcode::I32WrapI64:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toInt()");
      break;

    case Opcode::I32TruncF32S:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.I32_TRUNC_S_F32");
      break;

    case Opcode::I64TruncF32S:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.I64_TRUNC_S_F32");
      break;

    case Opcode::I32TruncF64S:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.I32_TRUNC_S_F64");
      break;

    case Opcode::I64TruncF64S:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.I64_TRUNC_S_F64");
      break;

    case Opcode::I32TruncF32U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.I32_TRUNC_U_F32");
      break;

    case Opcode::I64TruncF32U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.I64_TRUNC_U_F32");
      break;

    case Opcode::I32TruncF64U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.I32_TRUNC_U_F64");
      break;

    case Opcode::I64TruncF64U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.I64_TRUNC_U_F64");
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
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.I32_TRUNC_SAT_U_F32");
      break;

    case Opcode::I64TruncSatF32U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.I64_TRUNC_SAT_U_F32");
      break;

    case Opcode::I32TruncSatF64U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.I32_TRUNC_SAT_U_F64");
      break;

    case Opcode::I64TruncSatF64U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.I64_TRUNC_SAT_U_F64");
      break;

    case Opcode::F32ConvertI32S:
    case Opcode::F32ConvertI64S:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toFloat()");
      break;

    case Opcode::F32ConvertI32U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.UIntToFloat");
      break;

    case Opcode::F32DemoteF64:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toFloat()");
      break;

    case Opcode::F32ConvertI64U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.ULongToFloat");
      break;

    case Opcode::F64ConvertI32S:
    case Opcode::F64ConvertI64S:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toDouble()");
      break;

    case Opcode::F64ConvertI32U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.UIntToDouble");
      break;

    case Opcode::F64PromoteF32:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".toDouble()");
      break;

    case Opcode::F64ConvertI64U:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.ULongToDouble");
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
    case Opcode::I32Load: func = "i32_load"; break;
    case Opcode::I64Load: func = "i64_load"; break;
    case Opcode::F32Load: func = "f32_load"; break;
    case Opcode::F64Load: func = "f64_load"; break;
    case Opcode::I32Load8S: func = "i32_load8_s"; break;
    case Opcode::I64Load8S: func = "i64_load8_s"; break;
    case Opcode::I32Load8U: func = "i32_load8_u"; break;
    case Opcode::I64Load8U: func = "i64_load8_u"; break;
    case Opcode::I32Load16S: func = "i32_load16_s"; break;
    case Opcode::I64Load16S: func = "i64_load16_s"; break;
    case Opcode::I32Load16U: func = "i32_load16_u"; break;
    case Opcode::I64Load16U: func = "i64_load16_u"; break;
    case Opcode::I64Load32S: func = "i64_load32_s"; break;
    case Opcode::I64Load32U: func = "i64_load32_u"; break;

    default:
      WABT_UNREACHABLE;
  }

  assert(module_->memories.size() == 1);
  Memory* memory = module_->memories[0];

  Type result_type = expr.opcode.GetResultType();
  StackValue sv = PopValue();
  DropTypes(1);
  PushType(result_type);
  std::string pos;
  std::swap(sv.value, pos);
  sv.precedence = 2;
  sv.depends_memory = true;
  PushValue(sv);
  WriteValue(GlobalName(memory->name), ".", func, "(", pos);
  if (expr.offset != 0)
    WriteValuef(", %d", static_cast<int32_t>(expr.offset));
  WriteValue(")");

  //Write(StackVar(0), " = ", GlobalName(memory->name), ".", func, "(", pos);
  //if (expr.offset != 0)
  //  Writef(", %d", static_cast<int32_t>(expr.offset));
  //Write(");", Newline());
}

void KotlinWriter::Write(const StoreExpr& expr) {
  const char* func = nullptr;
  switch (expr.opcode) {
    case Opcode::I32Store: func = "i32_store"; break;
    case Opcode::I64Store: func = "i64_store"; break;
    case Opcode::F32Store: func = "f32_store"; break;
    case Opcode::F64Store: func = "f64_store"; break;
    case Opcode::I32Store8: func = "i32_store8"; break;
    case Opcode::I64Store8: func = "i64_store8"; break;
    case Opcode::I32Store16: func = "i32_store16"; break;
    case Opcode::I64Store16: func = "i64_store16"; break;
    case Opcode::I64Store32: func = "i64_store32"; break;

    default:
      WABT_UNREACHABLE;
  }

  assert(module_->memories.size() == 1);
  Memory* memory = module_->memories[0];

  SymbolSet depends_locals;
  SymbolSet depends_globals;
  SpillValues(2, depends_locals, depends_globals, true);
  StackValue sv_right = PopValue();
  StackValue sv_left = PopValue();
  DropTypes(2);
  Write(GlobalName(memory->name), ".", func, "(",
        sv_left.value);
  if (expr.offset != 0)
    Writef(", %d", static_cast<int32_t>(expr.offset));
  Write(", ", sv_right.value, ");", Newline());
}

void KotlinWriter::Write(const UnaryExpr& expr) {
  switch (expr.opcode) {
    case Opcode::I32Clz:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".countLeadingZeroBits()");
      break;

    case Opcode::I64Clz:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".countLeadingZeroBits().toLong()");
      break;

    case Opcode::I32Ctz:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".countTrailingZeroBits()");
      break;

    case Opcode::I64Ctz:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".countTrailingZeroBits().toLong()");
      break;

    case Opcode::I32Popcnt:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".countOneBits()");
      break;

    case Opcode::I64Popcnt:
      WritePostfixUnaryExpr(expr.opcode.GetResultType(), ".countOneBits().toLong()");
      break;

    case Opcode::F32Neg:
    case Opcode::F64Neg:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "-");
      break;

    case Opcode::F32Abs:
    case Opcode::F64Abs:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.abs");
      break;

    case Opcode::F32Sqrt:
    case Opcode::F64Sqrt:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "kotlin.math.sqrt");
      break;

    case Opcode::F32Ceil:
    case Opcode::F64Ceil:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.ceil");
      break;

    case Opcode::F32Floor:
    case Opcode::F64Floor:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.floor");
      break;

    case Opcode::F32Trunc:
    case Opcode::F64Trunc:
      WriteSimpleUnaryExpr(expr.opcode.GetResultType(), "wasm_rt_impl.truncate");
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
      //Type result_type = expr.opcode.GetResultType();
      //Write(StackVar(2, result_type), " = ", "v128.bitselect", "(", StackVar(0),
      //      ", ", StackVar(1), ", ", StackVar(2), ");", Newline());
      //DropTypes(3);
      //PushType(result_type);
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
      //Write(StackVar(0, result_type), " = ", expr.opcode.GetName(), "(",
      //      StackVar(0), ", lane Imm: ", expr.val, ");", Newline());
      //DropTypes(1);
      UNIMPLEMENTED("SIMD support");
      break;
    }
    case Opcode::I8X16ReplaceLane:
    case Opcode::I16X8ReplaceLane:
    case Opcode::I32X4ReplaceLane:
    case Opcode::I64X2ReplaceLane:
    case Opcode::F32X4ReplaceLane:
    case Opcode::F64X2ReplaceLane: {
      //Write(StackVar(1, result_type), " = ", expr.opcode.GetName(), "(",
      //      StackVar(0), ", ", StackVar(1), ", lane Imm: ", expr.val, ");",
      //      Newline());
      //DropTypes(2);
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
  //Type result_type = expr.opcode.GetResultType();
  //Write(StackVar(1, result_type), " = ", expr.opcode.GetName(), "(",
  //      StackVar(1), " ", StackVar(0), ", lane Imm: $0x%08x %08x %08x %08x",
  //      expr.val.u32(0), expr.val.u32(1), expr.val.u32(2), expr.val.u32(3), ")",
  //      Newline());
  //DropTypes(2);
  //PushType(result_type);
  UNIMPLEMENTED("SIMD support");
}

void KotlinWriter::Write(const LoadSplatExpr& expr) {
  //assert(module_->memories.size() == 1);
  //Memory* memory = module_->memories[0];

  //Type result_type = expr.opcode.GetResultType();
  //Write(StackVar(0, result_type), " = ", expr.opcode.GetName(), "(",
  //      ExternalPtr(memory->name), ", (long)(", StackVar(0));
  //if (expr.offset != 0)
  //  Write(" + ", expr.offset);
  //Write("));", Newline());
  //DropTypes(1);
  //PushType(result_type);
  UNIMPLEMENTED("SIMD support");
}

void KotlinWriter::Write(const LoadZeroExpr& expr) {
  UNIMPLEMENTED("SIMD support");
}

void KotlinWriter::WriteKotlinSource() {
  stream_ = kotlin_stream_;
  WriteSourceTop();
  WriteFuncTypes();
  WriteImports();
  AllocateFuncs();
  WriteGlobals();
  WriteMemories();
  WriteTables();
  WriteExports();
  WriteFuncs();
  WriteDataInitializers();
  WriteElemInitializers();
  WriteInit();
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
