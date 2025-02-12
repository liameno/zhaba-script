#include "./to_c.hpp"

size_t tab_size = 2;

std::string id2C(const std::string& str) {
  static std::unordered_map<std::string, std::string> cache;

  if (cache.contains(str)) return cache.at(str) + "/*" + str + "*/";

  std::string ans = "id_" + std::to_string(genId());
  cache.emplace(str, ans);
  return ans;
}

std::string module2C(ZHModule* block) {
  std::string includes = R"(#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
)";

  std::function<void(ZHModule*)> gather_deps;
  Vec<Path> deps;

  gather_deps = [&](ZHModule* mod) {
    auto ext = mod->path.extension().string();
    if (!(ext == ".zh" || ext == ".json")) {
      deps.push_back(mod->path);
    }
    for (auto i : mod->dependencies) gather_deps(i);
  };
  gather_deps(block);

  for (const auto& dep : deps) {
    auto ext = dep.extension().string();
    includes += "#include \"";
    includes += dep.string();
    includes += "\"\n";
  }
  std::string res = includes + R"(
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef char* str;

typedef float f32;
typedef double f64;

i64 alloc(i64 size) {
  void* ptr = calloc(size, 1);
  return (i64)ptr;
}

char *in_str() {
  size_t size = 10;
  char *str;
  int ch;
  size_t len = 0;
  str = (char*)realloc(NULL, sizeof(*str)*size);
  if(!str)return str;
  while(EOF!=(ch=fgetc(stdin)) && ch != '\n'){
    str[len++]=ch;
    if (len==size) {
      str = (char*)realloc(str, sizeof(*str)*(size+=16));
      if(!str) return str;
    }
  }
  str[len++]='\0';
  return (char*)realloc(str, sizeof(*str)*len);
}
void panic(const char* str) {
  printf("%s", str);
  exit(EXIT_FAILURE);
}
)";

#define MAKE_SCAN(name, type, mod)     \
  res += #type " " #name "() {\n";     \
  res += "  " #type " tmp;\n";         \
  res += "  scanf(" #mod ", &tmp);\n"; \
  res += "  return tmp;\n";            \
  res += "}\n";

  MAKE_SCAN(in_i8, i8, "%i")
  MAKE_SCAN(in_i16, i16, "%i")
  MAKE_SCAN(in_i32, i32, "%i")
  MAKE_SCAN(in_i64, i64, "%i")
  MAKE_SCAN(in_u8, u8, "%i")
  MAKE_SCAN(in_u16, u16, "%i")
  MAKE_SCAN(in_u32, u32, "%i")
  MAKE_SCAN(in_u64, u64, "%i")
  MAKE_SCAN(in_char, char, "%i")
  MAKE_SCAN(in_bool, bool, "%i")
  MAKE_SCAN(in_f32, float, "%f")
  MAKE_SCAN(in_f64, double, "%lf")

  std::vector<std::pair<size_t, types::TYPE>> order;
  for (const auto& info : zhdata.structs) {
    if (info->defined == DEFINED::core) continue;
    order.emplace_back(info->order, info);
  }
  std::sort(order.begin(), order.end(),
            [](auto& lhs, auto& rhs) { return lhs.first < rhs.first; });

  for (auto [_, i] : order) {
    if (i->defined == DEFINED::zh) {
      auto id = i;
      res += structHead2C(id);
      res += ";\n";
      res += "typedef struct PROT_" + id2C(id->name);
      res += " " + id2C(id->name);
      res += ";\n";
    }
  }

  res += "\n";

  for (auto [_, i] : order) {
    if (i->defined == DEFINED::zh) {
      auto id = i;
      res += struct2C(id);
    }
  }

  for (auto i : zhdata.functions) {
    if (i->defined == DEFINED::zh) {
      res += funcHead2C(i);
      res += ";\n";
    }
  }

  res += "\n";

  for (auto i : zhdata.functions) {
    if (i->defined == DEFINED::zh) {
      res += func2C(i);
      res += "\n";
    }
  }
  return res;
}

std::string type2C(const types::Type& type, std::string name = "") {
  std::string res;
  if (type.isFn()) {
    const auto& types = type.getTypes();
    res += type2C(types.front());
    res += "(*";
    res += name;
    res += ")(";
    bool start = true;
    for (int i = 1; i < types.size(); ++i) {
      if (!start) res += ", ";
      res += type2C(types[i]);
      start = false;
    }
    res += ")";
  } else if (type.getTypeId()->defined == DEFINED::extern_c) {
    res += type.getTypeId()->extern_name;
  } else if (type.getTypeId()->defined == DEFINED::core) {
    res += tables::cpp_type_names.at(type.getTypeId());
  } else {
    res += id2C(type.getTypeId()->name);
  }
  res += std::string(type.getPtr(), '*');
  if (type.getRef()) res += "*";
  return res;
}

std::string exp2C(zhexp::Exp* exp) {
  std::string res;
  res += "(";
  if (auto lt = dynamic_cast<zhexp::I8Literal*>(exp)) {
    res += "(i8)" + std::to_string(lt->val);
  } else if (auto lt = dynamic_cast<zhexp::I16Literal*>(exp)) {
    res += "(i16)" + std::to_string(lt->val);
  } else if (auto lt = dynamic_cast<zhexp::I32Literal*>(exp)) {
    res += "(i32)" + std::to_string(lt->val);
  } else if (auto lt = dynamic_cast<zhexp::I64Literal*>(exp)) {
    res += "(i64)" + std::to_string(lt->val);
  } else if (auto lt = dynamic_cast<zhexp::U8Literal*>(exp)) {
    res += "(u8)" + std::to_string(lt->val);
  } else if (auto lt = dynamic_cast<zhexp::U16Literal*>(exp)) {
    res += "(u16)" + std::to_string(lt->val);
  } else if (auto lt = dynamic_cast<zhexp::U32Literal*>(exp)) {
    res += "(u32)" + std::to_string(lt->val);
  } else if (auto lt = dynamic_cast<zhexp::U64Literal*>(exp)) {
    res += "(u64)" + std::to_string(lt->val);
  } else if (auto lt = dynamic_cast<zhexp::F32Literal*>(exp)) {
    res += "(f32)" + std::to_string(lt->val);
  } else if (auto lt = dynamic_cast<zhexp::F64Literal*>(exp)) {
    res += "(f64)" + std::to_string(lt->val);
  } else if (auto lt = dynamic_cast<zhexp::BoolLiteral*>(exp)) {
    res += "(bool)" + std::to_string(lt->val);
  } else if (auto lt = dynamic_cast<zhexp::CharLiteral*>(exp)) {
    res += "(char)" + std::to_string(lt->val);
  } else if (auto lt = dynamic_cast<zhexp::StrLiteral*>(exp)) {
    res += "(str)";
    res += "\"";
    for (auto i : lt->val) {
      if (i == '\"')
        res += R"(\")";
      else if (i == '\\')
        res += R"(\\)";
      else if (i == '\b')
        res += R"(\b)";
      else if (i == '\n')
        res += R"(\n)";
      else if (i == '\t')
        res += R"(\t)";
      else if (i == '\0')
        res += R"(\0)";
      else
        res += i;
    }
    res += "\"";
  } else if (auto lt = dynamic_cast<zhexp::FnLiteral*>(exp)) {
    res += funcName2C(lt->val);
  } else if (auto op = dynamic_cast<zhexp::BinOperator*>(exp)) {
    if (op->val == "as") {
      /** I believe in weak typing supremacy 💪 */
      auto type_a = op->lhs->type;
      auto type_b = static_cast<zhexp::TypeLiteral*>(op->rhs)->literal_type;

      if (!(type_b.getPtr() || type_b.getRef() || type_b.getTypeId()->defined == DEFINED::core)||
          !(type_a.getPtr() || type_a.getRef() || type_a.getTypeId()->defined == DEFINED::core))
        throw ParserError(
            op->begin, op->end,
            "C doesn't allow conversion to non-scalar type `" + type_b.toString() + "`");
      res += "(";
      res += type2C(type_b);
      res += ")";
      res += exp2C(op->lhs);
    } else if (op->val == "=") {
      res += exp2C(op->lhs);
      res += "=";
      res += exp2C(op->rhs);
    } else if (op->val == ".") {
      if (op->type.getRef()) {
        res += "*";
      }
      res += "(";
      if (!op->lhs->type.getPtr()) {
        res += "&";
      }
      res += exp2C(op->lhs);
      res += ")->";
      res += id2C(static_cast<zhexp::IdLiteral*>(op->rhs)->val);
    } else if (op->val == ".call.call" && op->lhs->type.isFn()) {
      res += "(*";
      res += exp2C(op->lhs);
      res += ")";
      res += "(";
      auto rhs_tuple = castToTuple(op->rhs);
      auto types = op->lhs->type.getTypes();
      types.erase(types.begin());
      res += args2C(rhs_tuple, types);
      res += ")";
    } else {
      if (op->func && op->func->defined == DEFINED::core) {
        res += "(";
        res += args2C(op->lhs, {types::Type()});
        res += ")";
        res += op->func->name != "|||" ? op->func->name : "|";
        res += "(";
        res += args2C(op->rhs, {types::Type()});
        res += ")";
      } else {
        auto lhs_tuple = castToTuple(op->lhs);
        auto rhs_tuple = castToTuple(op->rhs);
        *lhs_tuple += *rhs_tuple;

        std::string res;
        if (op->type.getRef()) res += "*";
        res += funcName2C(op->func) + "(";
        res += args2C(lhs_tuple, op->func->getHead().types);
        res += ")";
        return res;
      }
    }
  } else if (auto op = dynamic_cast<zhexp::PrefixOperator*>(exp)) {
    if (op->func && op->func->defined == DEFINED::core) {
      if (0) {
      }
#define MAKE_LOP_C(name, type_, impl_)                          \
  else if (op->val == #name &&                                  \
           op->child->type.getTypeId() == types::type_) { \
    res += impl_;                                               \
    res += args2C(op->child, {types::Type()});                  \
    res += ")";                                                 \
  }

#define MAKE_C_FN_INT(type)                     \
  MAKE_LOP_C(!, type##T, "!(")                  \
  MAKE_LOP_C(-, type##T, "-(")                  \
  MAKE_LOP_C(+, type##T, "+(")                  \
  MAKE_LOP_C(~, type##T, "~(")                  \
  MAKE_LOP_C(in_##type, voidT, "in_" #type "(") \
  MAKE_LOP_C(i8, type##T, "(i8)(")                \
  MAKE_LOP_C(i16, type##T, "(i16)(")              \
  MAKE_LOP_C(i32, type##T, "(i32)(")              \
  MAKE_LOP_C(i64, type##T, "(i64)(")              \
  MAKE_LOP_C(u8, type##T, "(u8)(")                \
  MAKE_LOP_C(u16, type##T, "(u16)(")              \
  MAKE_LOP_C(u32, type##T, "(u32)(")              \
  MAKE_LOP_C(u64, type##T, "(u64)(")              \
  MAKE_LOP_C(f32, type##T, "(f32)(")              \
  MAKE_LOP_C(f64, type##T, "(f64)(")              \
  MAKE_LOP_C(bool, type##T, "(bool)(")            \
  MAKE_LOP_C(char, type##T, "(char)(")

  MAKE_C_FN_INT(i8)
  MAKE_C_FN_INT(i16)
  MAKE_C_FN_INT(i32)
  MAKE_C_FN_INT(i64)
  MAKE_C_FN_INT(u8)
  MAKE_C_FN_INT(u16)
  MAKE_C_FN_INT(u32)
  MAKE_C_FN_INT(u64)
  MAKE_C_FN_INT(bool)
  MAKE_C_FN_INT(char)

#define MAKE_C_FN_FLOAT(type)                   \
  MAKE_LOP_C(!, type##T, "!(")                  \
  MAKE_LOP_C(-, type##T, "-(")                  \
  MAKE_LOP_C(+, type##T, "+(")                  \
  MAKE_LOP_C(in_##type, voidT, "in_" #type "(") \
  MAKE_LOP_C(i8, type##T, "(i8)(")                \
  MAKE_LOP_C(i16, type##T, "(i16)(")              \
  MAKE_LOP_C(i32, type##T, "(i32)(")              \
  MAKE_LOP_C(i64, type##T, "(i64)(")              \
  MAKE_LOP_C(u8, type##T, "(u8)(")                \
  MAKE_LOP_C(u16, type##T, "(u16)(")              \
  MAKE_LOP_C(u32, type##T, "(u32)(")              \
  MAKE_LOP_C(u64, type##T, "(u64)(")              \
  MAKE_LOP_C(f32, type##T, "(f32)(")              \
  MAKE_LOP_C(f64, type##T, "(f64)(")              \
  MAKE_LOP_C(bool, type##T, "(bool)(")            \
  MAKE_LOP_C(char, type##T, "(char)(")

  MAKE_C_FN_FLOAT(f32)
  MAKE_C_FN_FLOAT(f64)

  MAKE_LOP_C(in_str, voidT, "in_str(")

  MAKE_LOP_C(put, i8T, "printf(\"%d\", ")
  MAKE_LOP_C(out, i8T, "printf(\"%d\\n\", ")

  MAKE_LOP_C(put, i16T, "printf(\"%hd\", ")
  MAKE_LOP_C(out, i16T, "printf(\"%hd\\n\", ")

  MAKE_LOP_C(put, i32T, "printf(\"%d\", ")
  MAKE_LOP_C(out, i32T, "printf(\"%d\\n\", ")

  MAKE_LOP_C(put, i64T, "printf(\"%lld\", ")
  MAKE_LOP_C(out, i64T, "printf(\"%lld\\n\", ")

  MAKE_LOP_C(put, u8T, "printf(\"%d\", ")
  MAKE_LOP_C(out, u8T, "printf(\"%d\\n\", ")

  MAKE_LOP_C(put, u16T, "printf(\"%hd\", ")
  MAKE_LOP_C(out, u16T, "printf(\"%hd\\n\", ")

  MAKE_LOP_C(put, u32T, "printf(\"%u\", ")
  MAKE_LOP_C(out, u32T, "printf(\"%u\\n\", ")

  MAKE_LOP_C(put, u64T, "printf(\"%llu\", ")
  MAKE_LOP_C(out, u64T, "printf(\"%llu\\n\", ")

  MAKE_LOP_C(put, f32T, "printf(\"%f\", ")
  MAKE_LOP_C(out, f32T, "printf(\"%f\\n\", ")

  MAKE_LOP_C(put, f64T, "printf(\"%f\", ")
  MAKE_LOP_C(out, f64T, "printf(\"%f\\n\", ")

  MAKE_LOP_C(put, strT, "printf(\"%s\", ")
  MAKE_LOP_C(out, strT, "printf(\"%s\\n\", ")

  MAKE_LOP_C(put, charT, "printf(\"%c\", ")
  MAKE_LOP_C(out, charT, "printf(\"%c\\n\", ")

  MAKE_LOP_C(put, boolT, "printf(\"%d\", ")
  MAKE_LOP_C(out, boolT, "printf(\"%d\\n\", ")

  MAKE_LOP_C(malloc, i64T, "alloc(")
  MAKE_LOP_C(free, i64T, "free((void*) ")

  else {
    throw ParserError(op->begin, op->end, "unimplemented C op " + op->val);
      }
    } else if (op->val == "&") {
      res += "&";
      res += exp2C(op->child);
    } else if (op->val == "sizeof") {
      res += "sizeof";
      res += exp2C(op->child);
    } else if (op->val == "*" && op->func == nullptr) {
      res += "*";
      res += exp2C(op->child);
    } else {
      std::string res;
      if (op->type.getRef()) res += "*";
      res += funcName2C(op->func) + "(";
      res += args2C(op->child, op->func->getHead().types);
      res += ")";
      return res;
    }
  } else if (auto tp = dynamic_cast<zhexp::TypeLiteral*>(exp)) {
    res += type2C(tp->literal_type);
  } else if (auto var = dynamic_cast<zhexp::Variable*>(exp)) {
    if (var->getType().getRef()) res += "*";
    res += "v" + std::to_string(var->getId());
  } else if (auto op = dynamic_cast<zhexp::PostfixOperator*>(exp)) {
    std::string res;
    if (op->type.getRef()) res += "*";
    res += funcName2C(op->func) + "(";
    res += args2C(op->child, op->func->getHead().types);
    res += ")";
    return res;
  } else if (auto tuple = dynamic_cast<zhexp::Tuple*>(exp)) {
    for (size_t i = 0; i < tuple->content.size(); ++i) {
      if (i) res += ", ";
      res += exp2C(tuple->content[i]);
    }
  } else {
    throw std::runtime_error("unimplemented expToC ");
  }
  res += ")";
  return res;
}

std::string args2C(zhexp::Exp* exp, const std::vector<types::Type>& types) {
  std::string res;
  auto tuple = zhexp::castToTuple(exp);
  for (int i = 0; i < tuple->content.size(); ++i) {
    if (i) res += ", ";
    if (types[i].getRef()) {
      if (!tuple->content[i]->type.getLval() &&
          !tuple->content[i]->type.getRef())
        throw ParserError(
            tuple->content[i]->begin, tuple->content[i]->end,
            "Expression must be lval to be able pass by reference");
      res += "&";
    }
    res += exp2C(tuple->content[i]);
  }
  return res;
}

std::string block2C(STBlock* block, Function* fn, size_t depth) {
  std::string res;
  res += "{\n";

  for (const auto [name, varInfo] : block->scope_info.vars.get()) {
    res += std::string((depth+1) * tab_size , ' ');
    if (varInfo->type.isFn()) {
      res += type2C(varInfo->type, "v" + std::to_string(varInfo->id));
      res += ";";
    } else {
      res += type2C(varInfo->type);
      res += " ";
      res += "v" + std::to_string(varInfo->id);
      res += ";";
    }
    res += " /*" + name + "*/\n"; 
  }

  for (auto& i : block->nodes) {
    res += node2C(i, fn, depth + 1);
    res += "\n";
  }

  res += std::string(depth * tab_size, ' ');
  res += "}";
  return res;
}

std::string node2C(STNode* node, Function* fn, size_t depth) {
  std::string res;
  res += std::string(depth * tab_size, ' ');
  if (auto exp = dynamic_cast<STExp*>(node)) {
    res += exp2C(exp->exp);
    res += ";";
  } else if (auto ret = dynamic_cast<STRet*>(node)) {
    res += "return ";
    if (fn->type.getRef()) res += "&";
    res += exp2C(ret->exp);
    res += ";";
  } else if (auto stif = dynamic_cast<STIf*>(node)) {
    res += "if (";
    res += exp2C(stif->condition);
    res += ") ";

    res += block2C(stif->body, fn, depth);
    for (int i = 0; i < stif->elseif_body.size(); ++i) {
      res += "\n else if (";
      res += exp2C(stif->elseif_body[i].first);
      res += ") ";
      res += block2C(stif->elseif_body[i].second, fn, depth);
    }

    /** <else> */
    if (stif->else_body) {
      res += " else ";
      res += block2C(stif->else_body, fn, depth);
    }
  } else if (auto stwhile = dynamic_cast<STWhile*>(node)) {
    res += "while (" + exp2C(stwhile->condition) + ") ";
    res += block2C(stwhile->body, fn, depth);
  } else if (auto block = dynamic_cast<STBlock*>(node)) {
    res += block2C(block, fn, depth + 1);
  } else {
    throw std::runtime_error("unimplemented nodeToB");
  }
  return res;
};

std::string funcName2C(Function* func) {
  if (!func) throw std::runtime_error("null head bop");
  if (func->defined == DEFINED::extern_c) \
    return func->extern_name;
  return id2C(func->toUniqueStr());
}

std::string funcHead2C(Function* func) {
  std::string str;
  if (func->name == "main") {
    str += "int main(int argc, char *argv[]) ";
  } else {
    str += type2C(func->type) + " ";
    str += funcName2C(func) + "(";
    bool start = true;
    for (auto& [name, type] : func->args) {
      if (!start) str += ", ";
      if (type.isFn()) {
        str +=
            type2C(type, "v" + std::to_string(func->args_scope->vars.at(name)->id));
      } else {
        str += type2C(type) + " ";
        str += "v" + std::to_string(func->args_scope->vars.at(name)->id);
      }
      start = false;
    }
    str += ")";
  }
  return str;
}

std::string funcHead2FnPtr(Function* func) {
  std::string str;
  if (func->name == "main") {
    str += "int main(int argc, char *argv[]) ";
  } else {
    str += type2C(func->type) + " ";
    str += funcName2C(func) + "(";
    bool start = true;
    for (auto& [name, type] : func->args) {
      if (!start) str += ", ";
      str += type2C(type) + " ";
      str += "v" + std::to_string(func->args_scope->vars.at(name)->id);
      start = false;
    }
    str += ")";
  }
  return str;
}

std::string func2C(Function* fn) {
  bool is_void = fn->type.getTypeId() == types::voidT;
  std::string res = funcHead2C(fn) + (is_void ? "" : " {") +
                    block2C(fn->body, fn);
  if (!is_void) {
    /** Emergency exit */
    res += " panic(\"reached function end without returning anything ";
    res += fn->toUniqueStr();
    res += "\\n\");}";
  }
  res += "\n";
  return res;
}

std::string structHead2C(types::TYPE id) {
  return "struct PROT_" + id2C(id->name);
}

std::string struct2C(types::TYPE id) {
  std::string res = structHead2C(id);
  res += " {\n";
  for (const auto& [name, type] : id->members) {
    res += "  " +
           (type.isFn() ? type2C(type, id2C(name)) : type2C(type) + " " + id2C(name)) +
           ";\n";
  }
  res += "};\n";
  return res;
}
