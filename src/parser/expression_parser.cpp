#include "./expression_parser.hpp"

namespace zhexp {

STExp *callDtor(std::pair<Scope::VarInfo *, Function *> info) {
  auto token = Token(TOKEN::id, "tmp", 0, 0, "tmp");

  auto exp = new zhexp::BinOperator(
      token, token, ".call.dtor",
      new zhexp::PrefixOperator(
          token, token, "&",
          new zhexp::Variable(token, token, info.first->name, info.first->type,
                              info.first->id)),
      new zhexp::Tuple(token, token));
  exp->func = info.second;
  auto tmp = new STExp;
  tmp->exp = exp;
  return tmp;
};

void copyExp(Exp *&exp, Scope &scope) {
  if (!(exp->type.getLval() || exp->type.getRef()))
    return;
  if (exp->type.getTypeId()->defined == DEFINED::core)
    return;
  if (!scope.PR_OP.contains({exp->type.nonRefClone().rvalClone().toString(),
                           {exp->type.nonRefClone().rvalClone()}}))
    return;
  if (auto pr = dynamic_cast<PrefixOperator *>(exp))
    if (pr->val == exp->type.nonRefClone().rvalClone().toString()) return;

  auto new_exp = new zhexp::PrefixOperator(
      exp->begin, exp->end, exp->type.nonRefClone().rvalClone().toString(),
      exp);
  new_exp->func = scope.PR_OP.at({exp->type.nonRefClone().rvalClone().toString(),
                                 {exp->type.nonRefClone().rvalClone()}});
  new_exp->type = new_exp->func->type;
  exp = new_exp;
  return;
};

/**
 * @brief before: exp
 * after: *(t = exp, &t)
 *
 * @param exp is postprocessed
 */
Exp* makeLval(Exp*& exp, Scope& scope) {
  // if (exp->type.getLval()) return exp;

  auto type = exp->type;
  auto type_ptr = type;
  type_ptr.setPtr(type_ptr.getPtr() + 1);

  auto var_name = "tmp_rval_" + std::to_string(genId());
  scope.setVar(var_name, type);
  auto id = scope.vars.at(var_name)->id;

  auto var_exp1 = new Variable(exp->begin, exp->end, var_name, type, id);
  var_exp1->type = type;
  auto var_exp2 = new Variable(exp->begin, exp->end, var_name, type, id);
  var_exp2->type = type;

  auto assign = new BinOperator(exp->begin, exp->end, "=", var_exp1, exp);
  assign->type = types::Type();

  auto address_exp = new PrefixOperator(exp->begin, exp->end, "&", var_exp2);
  address_exp->type = type_ptr;

  auto tuple = new Tuple(exp->begin, exp->begin, {assign, address_exp});
  tuple->type = type_ptr;

  auto deref = new PrefixOperator(exp->begin, exp->end, "*", tuple);
  deref->type = type;
  deref->type.setLval(true);

  return exp = deref;
}

/**
 * @brief Converts expression to expression tree
 */
Exp *buildExp(Scope &scope, tokeniter begin, tokeniter end) {
  /** Flow operator skip */
  if (begin->token == TOKEN::id and tables::flow_ops.count(begin->val)) {
    auto new_val = begin->val;
    auto new_op = buildExp(scope, ++begin, end);
    return new FlowOperator(*begin, *begin, new_val, new_op);
  }

  enum TOKEN_TYPE {
    undef,
    other,
    open_p,
    close_p,
    any_op,
    pr_op,
    po_op,
    bin_op,
  };

  /** Define operators type (prefix, postfix or binary) */

  /** Init preprocessed sequence */
  const static auto used_tokens = std::set{
      TOKEN::open_p,
      TOKEN::close_p,
      TOKEN::id,
      TOKEN::int_literal,
      TOKEN::str_literal,
      TOKEN::block_end,
      TOKEN::fin_block,
      TOKEN::new_block,
      TOKEN::next_block,
  };
  std::vector<std::pair<tokeniter, TOKEN_TYPE>> raw;
  raw.emplace_back(begin, open_p);
  for (auto i = begin; i != end; ++i)
    if (used_tokens.contains(i->token)) raw.emplace_back(i, undef);
  raw.emplace_back(begin, close_p);

  /** Define Type literals */
  for (auto i = raw.begin() + 1; i != raw.end() - 1; ++i) {
    try {
      auto iter = i->first;
      auto type = types::parse(iter, scope);
      while (i != raw.end() - 1 && i->first < iter) {
        i->second = other;
        ++i;
      }
      --i;
    } catch (const types::TypeParsingError &err) {
    }
  }

  /** Define operators, `(` and `)` */
  for (auto i = raw.begin() + 1; i != raw.end() - 1; ++i)
    if (i->first->token == TOKEN::open_p)
      i->second = TOKEN_TYPE::open_p;
    else if (i->first->token == TOKEN::close_p)
      i->second = TOKEN_TYPE::close_p;
    else if (i->second == undef && i->first->token == TOKEN::id &&
             scope.containsOp(i->first->val))
      i->second = TOKEN_TYPE::any_op;

  /** Define prefix operators */
  for (auto i = raw.begin() + 1; i != raw.end() - 1; ++i)
    if (i->second == TOKEN_TYPE::any_op &&
        ((i - 1)->second == open_p || (i - 1)->second == pr_op))
      i->second = TOKEN_TYPE::pr_op;

  /** Define postfix operators */
  for (auto i = raw.rbegin() + 1; i != raw.rend() - 1; ++i)
    if (i->second == TOKEN_TYPE::any_op &&
        ((i - 1)->second == close_p || (i - 1)->second == po_op))
      i->second = TOKEN_TYPE::po_op;

  /** Define binary operators */
  for (auto i = raw.begin() + 1; i != raw.end() - 1; ++i)
    if (i->second == TOKEN_TYPE::any_op) {
      if ((i - 1)->first->val == ".")
        i->second = TOKEN_TYPE::undef;
      else if ((i - 1)->second == bin_op)
        i->second = TOKEN_TYPE::pr_op;
      else
        i->second = TOKEN_TYPE::bin_op;
    }
  for (auto i = raw.begin() + 1; i != raw.end() - 1; ++i)
    if (i->second == other) i->second = undef;


  /** Create implicit `,` and call operators */
  std::vector<std::pair<Exp *, TOKEN_TYPE>> preprocessed;
  std::unordered_map<size_t, char> parentheses_tags;

  preprocessed.emplace_back(nullptr, open_p);

  for (auto i = raw.begin() + 1; i != raw.end() - 1; ++i) {
    if (i->second == pr_op) {
      preprocessed.emplace_back(
          new PrefixOperator(*i->first, *i->first, i->first->val, nullptr),
          i->second);
    } else if (i->second == po_op) {
      preprocessed.emplace_back(
          new PostfixOperator(*i->first, *i->first, i->first->val, nullptr),
          i->second);
    } else if (i->second == bin_op) {
      preprocessed.emplace_back(
          new BinOperator(*i->first, *i->first, i->first->val, nullptr,
                          nullptr),
          i->second);
    } else if (i->second == open_p || i->second == close_p) {
      auto tag = i->first->val.front();
      if (tag == '}') tag = '{';
      if (tag == ']') tag = '[';
      if (tag == '(') tag = '(';

      parentheses_tags.emplace(preprocessed.size(), tag);
      preprocessed.emplace_back(nullptr, i->second);
    } else {
      auto &iter = *i->first;
      bool skip = false;

      try {
        auto tmp_token_iter = i->first;
        auto pos = iter.pos;
        auto type = types::parse(tmp_token_iter, scope);
        preprocessed.emplace_back(new TypeLiteral(iter, iter, type), i->second);
        while (i != raw.end() - 1 && i->first < tmp_token_iter) ++i;
        --i;
        skip = true;
      } catch (const types::TypeParsingError &err) {
      }

      if (!skip) {
        if (i->first->token == TOKEN::int_literal) {
          int base = 10;
          auto str = i->first->val;
          if (std::regex_match(str, std::regex("0x.+"))) {
            base = 16;
            str = str.substr(2, str.size() - 2);
          } else if (std::regex_match(str, std::regex("0b.+"))) {
            base = 2;
            str = str.substr(2, str.size() - 2);
          }

          try {
            if (std::regex_match(str, std::regex("true|tru")))
              preprocessed.emplace_back(new BoolLiteral(iter, iter, true),
                                        i->second);
            else if (std::regex_match(str, std::regex("false|fls")))
              preprocessed.emplace_back(new BoolLiteral(iter, iter, false),
                                        i->second);

            else if (std::regex_match(str, std::regex(".+i8")))
              preprocessed.emplace_back(
                  new I8Literal(iter, iter, std::stoll(str, 0, base)),
                  i->second);
            else if (std::regex_match(str, std::regex(".+i16")))
              preprocessed.emplace_back(
                  new I16Literal(iter, iter, std::stoll(str, 0, base)),
                  i->second);
            else if (std::regex_match(str, std::regex(".+i32")))
              preprocessed.emplace_back(
                  new I32Literal(iter, iter, std::stoll(str, 0, base)),
                  i->second);
            else if (std::regex_match(str, std::regex(".+i64")))
              preprocessed.emplace_back(
                  new I64Literal(iter, iter, std::stoll(str, 0, base)),
                  i->second);

            else if (std::regex_match(str, std::regex(".+u8")))
              preprocessed.emplace_back(
                  new U8Literal(iter, iter, std::stoull(str, 0, base)),
                  i->second);
            else if (std::regex_match(str, std::regex(".+u16")))
              preprocessed.emplace_back(
                  new U16Literal(iter, iter, std::stoull(str, 0, base)),
                  i->second);
            else if (std::regex_match(str, std::regex(".+u32")))
              preprocessed.emplace_back(
                  new U32Literal(iter, iter, std::stoull(str, 0, base)),
                  i->second);
            else if (std::regex_match(str, std::regex(".+u64")))
              preprocessed.emplace_back(
                  new U64Literal(iter, iter, std::stoull(str, 0, base)),
                  i->second);
            else if (std::regex_match(str, std::regex(".+f32")))
              preprocessed.emplace_back(
                  new F32Literal(iter, iter, std::stof(str)), i->second);
            else if (std::regex_match(str, std::regex(".+f64|.*\\..*")))
              preprocessed.emplace_back(
                  new F64Literal(iter, iter, std::stod(str)), i->second);
            else if (std::regex_match(str, std::regex(".+i")))
              preprocessed.emplace_back(
                  new I64Literal(iter, iter, std::stoll(str, 0, base)),
                  i->second);
            else if (std::regex_match(str, std::regex(".+u")))
              preprocessed.emplace_back(
                  new U64Literal(iter, iter, std::stoll(str, 0, base)),
                  i->second);
            else if (std::regex_match(str, std::regex("[0-9]+f")))
              preprocessed.emplace_back(
                  new F64Literal(iter, iter, std::stod(str)), i->second);
            else if (std::regex_match(str, std::regex(".+")))
              preprocessed.emplace_back(
                  new I64Literal(iter, iter, std::stoll(str, 0, base)),
                  i->second);
            else
              throw ParserError(iter, "Wrong int literal");
          } catch (const std::out_of_range &err) {
            throw ParserError(iter,
                              "The converted value fall out of the range of "
                              "the result type ");
          }
        } else if (i->first->token == TOKEN::str_literal) {
          bool do_escape = true;
          auto str = i->first->val;

          if (str[0] == '`') do_escape = false;
          str = str.substr(1, i->first->val.size() - 2);
          /** Apply escape sequences */
          if (do_escape) {
            str = std::regex_replace(str, std::regex(R"(\\')"), "\'");
            str = std::regex_replace(str, std::regex(R"(\\")"), "\"");
            str = std::regex_replace(str, std::regex(R"(\\\\)"), "\\");
            str = std::regex_replace(str, std::regex(R"(\\b)"), "\b");
            str = std::regex_replace(str, std::regex(R"(\\n)"), "\n");
            str = std::regex_replace(str, std::regex(R"(\\t)"), "\t");
            str = std::regex_replace(str, std::regex(R"(\\0)"), "\0");
          }
          preprocessed.emplace_back(new StrLiteral(iter, iter, str), i->second);
        } else if (i->first->token == TOKEN::id) {
          preprocessed.emplace_back(new IdLiteral(iter, iter, i->first->val),
                                    i->second);
        } else
          throw ParserError(*i->first, *i->first, "Unexpected token");
      }
    }

    if ((i->second == close_p || i->second == undef)) {
      /** Call operator */
      if ((i + 1)->second == open_p)
        preprocessed.emplace_back(
            new BinOperator(*(i + 1)->first, *(i + 1)->first,
                            (i + 1)->first->val, nullptr, nullptr),
            bin_op);

      /** Implicit `,` */
      if ((i + 1)->second == undef)
        preprocessed.emplace_back(
            new BinOperator(*i->first, *i->first, ",", nullptr, nullptr),
            bin_op);
    }

    /** () parse and push empty tuple */
    if (i->second == open_p && (i + 1)->second == close_p)
      preprocessed.emplace_back(new Tuple(*i->first, *i->first), undef);
  }
  preprocessed.emplace_back(nullptr, close_p);

  std::vector<std::pair<Exp*, TOKEN_TYPE>> stack, res;
  size_t i = -1;
  for (auto &[iter, type] : preprocessed) {
    ++i;
    if (type == open_p) {
      stack.emplace_back(iter, type);
    } else if (type == close_p) {
      while (true) {
        if (stack.back().second == open_p) {
          stack.pop_back();
          break;
        }
        res.emplace_back(stack.back());
        stack.pop_back();
      }

      if (parentheses_tags.contains(i))
        res.back().first->parentheses_tag = parentheses_tags.at(i);
    } else if (type == pr_op) {
      stack.emplace_back(iter, type);
    } else if (type == po_op) {
      res.emplace_back(iter, type);
    } else if (type == bin_op) {
      if (!scope.bin_operators.contains(dynamic_cast<BinOperator *>(iter)->val))
        throw ParserError(iter->begin, iter->end, "operator can't be binary operator");
      auto slf_priority =
          scope.bin_operators.at(dynamic_cast<BinOperator *>(iter)->val);
      while (!stack.empty() &&
             ((stack.back().second == pr_op && 3 <= slf_priority) ||
              (stack.back().second == bin_op &&
               scope.bin_operators.at(
                   dynamic_cast<BinOperator *>(stack.back().first)->val) <=
                   slf_priority))) {
        res.emplace_back(stack.back());
        stack.pop_back();
      }
      stack.emplace_back(iter, type);
    } else {
      res.emplace_back(iter, type);
    }
  }

  /** Convert RPN to tree */
  std::vector<Exp*> exp_stack;
  for (auto &[exp, type] : res) {
    if (auto b_op = dynamic_cast<BinOperator*>(exp)) {
      if (exp_stack.size() < 2)
        throw ParserError(exp->begin, exp->end,
                          "Not enough operands for binary operator");
      b_op->rhs = exp_stack.back();
      exp_stack.pop_back();
      b_op->lhs = exp_stack.back();
      exp_stack.pop_back();


      auto val = b_op->val;
      if (val == "(" || val == "[" || val == "{") {
        if (auto type_l = dynamic_cast<TypeLiteral*>(b_op->lhs)) {
          exp_stack.push_back(new PrefixOperator(
              b_op->begin, b_op->end, type_l->literal_type.toString(), b_op->rhs));
        } else if (auto id_l = dynamic_cast<IdLiteral*>(b_op->lhs)) {
          if (scope.vars.contains(id_l->val)) {
            std::string call_name = ".call.call";
            if (val == "[") call_name = ".call.sub";

            exp_stack.push_back(new BinOperator(b_op->begin, b_op->end,
                                                call_name, id_l, b_op->rhs));
          } else {
            exp_stack.push_back(new PrefixOperator(b_op->begin, b_op->end,
                                                   id_l->val, b_op->rhs));
          }
        } else if (dynamic_cast<BinOperator *>(b_op->lhs) &&
                   dynamic_cast<BinOperator *>(b_op->lhs)->val == ".") {
          auto ch_b_op = dynamic_cast<BinOperator *>(b_op->lhs);

          if (ch_b_op->val == ".")
            if (auto mem = dynamic_cast<IdLiteral *>(ch_b_op->rhs)) {
              exp_stack.push_back(new BinOperator(b_op->begin, b_op->end,
                                                  ".call." + mem->val,
                                                  ch_b_op->lhs, b_op->rhs));
            }
        } else {
          std::string call_name = ".call.call";
          if (val == "[") call_name = ".call.sub";

          exp_stack.push_back(new BinOperator(b_op->begin, b_op->end, call_name,
                                              b_op->lhs, b_op->rhs));
        }
      } else {
        exp_stack.push_back(b_op);
      }
    } else if (auto pr_op = dynamic_cast<PrefixOperator *>(exp)) {
      if (exp_stack.size() < 1)
        throw ParserError(exp->begin, exp->end,
                          "Not enough operands for prefix operator");
      pr_op->child = exp_stack.back();
      exp_stack.pop_back();
      exp_stack.push_back(pr_op);
    } else if (auto po_op = dynamic_cast<PostfixOperator *>(exp)) {
      if (exp_stack.size() < 1)
        throw ParserError(exp->begin, exp->end,
                          "Not enough operands for postfix operator");
      po_op->child = exp_stack.back();
      exp_stack.pop_back();
      exp_stack.push_back(po_op);
    } else
      exp_stack.push_back(exp);
  }

  if (exp_stack.size() != 1)
    throw ParserError(exp_stack.front()->begin, exp_stack.back()->end,
                      "Expresssion parsing failed (too many exps)");
  auto exp = exp_stack.front();
  if (zhdata.flags["exp_parser_logs"]) zhexp::printExpTree(exp);
  return exp;
}

Exp *postprocess(Exp *exp, Scope &scope) {
  if (auto op = dynamic_cast<FlowOperator *>(exp)) {
    exp->type = types::Type(types::voidT);
    if (op->operand) op->operand = postprocess(op->operand, scope);
  } else if (auto op = dynamic_cast<I8Literal *>(exp)) {
    exp->type = types::Type(types::i8T);
  } else if (auto op = dynamic_cast<I16Literal *>(exp)) {
    exp->type = types::Type(types::i16T);
  } else if (auto op = dynamic_cast<I32Literal *>(exp)) {
    exp->type = types::Type(types::i32T);
  } else if (auto op = dynamic_cast<I64Literal *>(exp)) {
    exp->type = types::Type(types::i64T);
  } else if (auto op = dynamic_cast<U8Literal *>(exp)) {
    exp->type = types::Type(types::u8T);
  } else if (auto op = dynamic_cast<U16Literal *>(exp)) {
    exp->type = types::Type(types::u16T);
  } else if (auto op = dynamic_cast<U32Literal *>(exp)) {
    exp->type = types::Type(types::u32T);
  } else if (auto op = dynamic_cast<U64Literal *>(exp)) {
    exp->type = types::Type(types::u64T);
  } else if (auto op = dynamic_cast<F32Literal *>(exp)) {
    exp->type = types::Type(types::f32T);
  } else if (auto op = dynamic_cast<F64Literal *>(exp)) {
    exp->type = types::Type(types::f64T);
  } else if (auto op = dynamic_cast<BoolLiteral *>(exp)) {
    exp->type = types::Type(types::boolT);
  } else if (auto op = dynamic_cast<CharLiteral *>(exp)) {
    exp->type = types::Type(types::charT);
  } else if (auto op = dynamic_cast<StrLiteral *>(exp)) {
    exp->type = types::Type(types::strT);
    exp->type.setLval(true);
  } else if (auto id = dynamic_cast<IdLiteral *>(exp)) {
    if (scope.vars.contains(id->val)) {
      auto tmp =
          new Variable(id->begin, id->end, id->val, scope.vars.at(id->val)->type,
                       scope.vars.at(id->val)->id);
      tmp->type = scope.vars.at(id->val)->type;
      exp = tmp;
    } else if (scope.last_fn.contains(id->val)) {
      auto fn = scope.last_fn.at(id->val);
      auto tmp = new FnLiteral(id->begin, id->end, fn);
      tmp->type = fn->getFnType();
      exp = tmp;
    } else
      throw ParserError(id->begin, id->end,
                        "Unknown variable '" + id->val + "'");
  } else if (auto op = dynamic_cast<BinOperator *>(exp)) {
    if (op->val == ",") {
      op->lhs = postprocess(op->lhs, scope);
      op->rhs = postprocess(op->rhs, scope);

      bool b = true;
      if (auto t = dynamic_cast<Tuple *>(op->lhs)) {
        t->content.insert(t->content.end(), op->rhs);
        t->type = op->rhs->type;
        exp = t;
        b = false;
      }
      if (b) {
        auto t = new Tuple(op->begin, op->end);
        t->content = {op->lhs, op->rhs};
        exp = t;
        exp->type = op->rhs->type;
      }
    }
      /** Regular assignment */
    else if (op->val == "=") {
      op->lhs = postprocess(op->lhs, scope);
      op->rhs = postprocess(op->rhs, scope);

      auto lhs = op->lhs->type.rvalClone();
      auto rhs = op->rhs->type.rvalClone();
      lhs.setRef(false);
      if (lhs.rvalClone().nonRefClone() <=> rhs.rvalClone().nonRefClone() !=
          0) {
        throw ParserError(op->begin, op->end, "Types (" +
            op->lhs->type.toString() + " " +
            op->rhs->type.toString() +
            ") for '=' are different");
      }

      if (op->lhs->type.getLval() || op->lhs->type.getRef()) {
        op->type = types::Type();
      } else {
        throw ParserError(op->lhs->begin, op->end, "Left operant for '=' must be lval or ref");
      }
      copyExp(op->rhs, scope);
    }

    /** Variable creation & assignment */
    else if (op->val == ":=") {
      auto tag = op->lhs->parentheses_tag;
      if (tag == '(') {
        op->rhs = postprocess(op->rhs, scope);
        if (auto id_l = dynamic_cast<IdLiteral *>(op->lhs)) {
          if (op->rhs->type.getTypeId() == types::voidT)
            throw ParserError(op->begin, op->rhs->end,
                              "Variable type cannot be void");
          try {
            scope.setVar(id_l->val, op->rhs->type.nonRefClone());
          } catch (const std::runtime_error &err) {
            throw ParserError(op->begin, op->rhs->end, err.what());
          }
          scope.vars.at(id_l->val)->type.setLval(true);

          auto tmp = new Variable(op->lhs->begin, op->lhs->end, id_l->val,
                                  scope.vars.at(id_l->val)->type,
                                  scope.vars.at(id_l->val)->id);
          tmp->type = scope.vars.at(id_l->val)->type;
          op->lhs = tmp;
          op->val = "=";
          op->type = types::Type(types::voidT);

          copyExp(op->rhs, scope);
        } else {
          throw ParserError(op->lhs->begin, op->lhs->end,
                            "Expected id literal");
        }
      } else {
        auto tuple = castTreeToTuple(op->lhs);

        auto res_tuple = new Tuple(op->begin, op->end, {});
        auto tmp_name = "tmp_destructured_" + std::to_string(genId());

        res_tuple->content.push_back(
          new BinOperator(
            op->begin, op->end,
            ":=", 
            new IdLiteral(op->begin, op->end, tmp_name),
            op->rhs
          )
        );
        /** Named destructuring
         *  {a b c} := t
         *  (tmp:=t),(a:=tmp.a),(b:=tmp.b),(c:=tmp.c)
         */
        if (tag == '{') {
          for (auto& i : tuple->content) {
            if (auto id_l = dynamic_cast<IdLiteral *>(i)) {
              res_tuple->content.push_back(new BinOperator(
                  i->begin, i->end,
                  ":=", new IdLiteral(i->begin, i->end, id_l->val),
                  new BinOperator(
                      i->begin, i->end, ".",
                      new IdLiteral(i->begin, i->end, tmp_name),
                      new IdLiteral(i->begin, i->end, id_l->val))));
            } else {
              throw ParserError(i->begin, i->end,
                                "Expected id literal");
            }
          }
        }
        /** Destructuring by index
         *  [a b c] := t
         *  (tmp:=t),(a:=tmp[0]),(b:=tmp[1]),(c:=tmp[2])
         */
        else if (tag == '[') {
          size_t id = -1;
          for (auto &i : tuple->content) {
            ++id;
            res_tuple->content.push_back(new BinOperator(
                op->begin, op->end, ":=", i,
                new BinOperator(op->begin, op->end, ".call.at",
                                new IdLiteral(op->begin, op->end, tmp_name),
                                new I64Literal(op->begin, op->end, id))));
          }
        }
        return postprocess(res_tuple, scope);
      }
    }
    /** Member access */
    else if (op->val == ".") {
      op->lhs = postprocess(op->lhs, scope);
      if (!(op->lhs->type.getLval() || op->lhs->type.getRef()))
        makeLval(op->lhs, scope);
        // throw ParserError(
        //         op->lhs->begin,
        //         op->begin,
        //         "Expression must be lval or reference to use `.`"
        //     );
      if (auto id = dynamic_cast<IdLiteral *>(op->rhs)) {
        if (!(op->lhs->type.getTypeId()->defined == DEFINED::core)) {
          if (op->lhs->type.getTypeId()->members.count(
              id->val)) {
            if (op->lhs->type.getPtr() > 1) {
              throw ParserError(
                  op->begin, op->end,
                  "Can't use member access with 2 or more pointer modifiers (" +
                      std::to_string(op->lhs->type.getPtr()) + " found)"
              );
            }
            op->type = op->lhs->type.getTypeId()->members[id->val];
            op->type.setLval(op->lhs->type.getLval());
          } else {
            throw ParserError(
                op->begin,
                op->end,
                "Type '" + op->lhs->type.toString() + "' doesn't have '" + id->val + "' member"
            );
          }
        } else {
          throw ParserError(
              op->begin, op->end, "Expression type can't be '" + op->lhs->type.toString() + "'"
          );
        }
      } else {
        throw ParserError(
            op->begin, op->end,
            "Expected identifier near '.', but (" +
                op->toString() + ") found"
        );
      }
    }
    /** Type cast */
    else if (op->val == "as") {
      op->lhs = postprocess(op->lhs, scope);
      if (auto type_l = dynamic_cast<TypeLiteral *>(op->rhs)) {
        op->type = type_l->literal_type;
      } else {
        throw ParserError(
            op->begin, op->rhs->end,
            "Expected type literal, but '" + op->rhs->toString() + "' found"
        );
      }
    /** Lambda function */
    } else if (op->val == "->") {
      auto fn_name = "lambda_" + std::to_string(genId());
      auto fn = new Function{fn_name, {}, types::Type()};
      // use global scope because lambdas aren't clojures yet
      fn->args_scope = new Scope(zhdata.global);
      Scope &args_scope = *fn->args_scope;

      /** Process param list */
      auto args_tuple = castTreeToTuple(op->lhs)->content;
      auto cur = args_tuple.begin(), end = args_tuple.end();
      std::unordered_set<std::string> used_vars;
      while (cur != end) {
        fn->args.emplace_back();
        auto type_ptr = dynamic_cast<TypeLiteral *>(*cur);
        if (!type_ptr)
          throw ParserError((*cur)->begin, "Expected argument type");
        fn->args.back().type = type_ptr->literal_type;
        ++cur;

        if (cur == end)
          throw ParserError((*cur)->begin, "Expected argument name");
        auto id_ptr = dynamic_cast<IdLiteral *>(*cur);
        if (!id_ptr)
          throw ParserError((*cur)->begin, "Expected argument name");

        if (used_vars.count(id_ptr->val)) 
          throw ParserError(id_ptr->begin, "Duplicate argument name");
        fn->args.back().name = id_ptr->val;
        used_vars.insert(id_ptr->val);
        ++cur;
      }
      for (auto &[name, type] : fn->args) {
        args_scope.setVar(name, type);
        args_scope.vars.at(name)->type.setLval(true);
      }

      /** Process body */
      fn->body = new STBlock(&args_scope);
      auto exp = postprocess(op->rhs, fn->body->scope_info); // global scope for now
      fn->type = exp->type;

      STNode* st_node;
      if (exp->type.getTypeId() != types::voidT) {
        auto st_ret = new STRet;
        st_ret->exp = exp;
        st_node = st_ret;
      } else {
        auto st_exp = new STExp;
        st_exp->exp = exp;
        st_node = st_exp;
      }
      fn->body->nodes.push_back(st_node);

      /** Finish */
      zhdata.functions.push_back(fn);
      scope.setPrOp(fn->getHead(), fn);
      auto fn_literal = new FnLiteral(op->begin, op->end, fn);
      fn_literal->type = fn->getFnType();
      return fn_literal;
    } else {
      op->lhs = postprocess(op->lhs, scope);
      op->rhs = postprocess(op->rhs, scope);

      /**
       * Pointer arithmetic
       * ptr + x(i64)
       * x(i64) + ptr
       * ptr - x(i64)
       */
      if ((op->val == "+" &&
          ((op->lhs->type.getPtr() &&
              op->rhs->type.getTypeId() == types::i64T) ||
              (op->rhs->type.getPtr() &&
                  op->lhs->type.getTypeId() == types::i64T))) or
          (op->val == "-" && op->lhs->type.getPtr() &&
              op->rhs->type.getTypeId() == types::i64T)) {
        if (op->rhs->type.getPtr()) std::swap(op->rhs, op->lhs);
        auto orig_type = op->lhs->type;
        auto int_type = types::Type(types::i64T);
        op->lhs =
            new BinOperator(op->begin, op->end, "as", op->lhs,
                            new TypeLiteral(op->begin, op->end, int_type));
        op->lhs->type = int_type;

        auto underlying_type = orig_type;
        underlying_type.setPtr(underlying_type.getPtr() - 1);

        auto int_literal = new PrefixOperator(
            op->begin, op->end, "sizeof",
            new TypeLiteral(op->begin, op->end, underlying_type));

        int_literal->type = int_type;
        auto t = new BinOperator(
            op->begin, op->end, "*", op->rhs, int_literal
        );
        t->func = scope.B_OP.at({"*", {int_type, int_type}});
        t->type = int_type;
        op->rhs = t;
        op->func = scope.B_OP.at({op->val, {int_type, int_type}});
        op->type = int_type;
        op = new BinOperator(op->begin, op->end, "as", op,
                             new TypeLiteral(op->begin, op->end, orig_type));
        op->type = orig_type;
        exp = op;
      } else if ((op->val == "==" || op->val == "!=" || op->val == "<=" ||
          op->val == ">=" || op->val == "<" || op->val == ">") &&
          (op->lhs->type.getPtr() && op->rhs->type.getPtr())) {
        auto int_type = types::Type(types::i64T);
        auto bool_type = types::Type(types::boolT);
        op->lhs =
            new BinOperator(op->begin, op->end, "as", op->lhs,
                            new TypeLiteral(op->begin, op->end, int_type));
        op->lhs->type = int_type;
        op->rhs =
            new BinOperator(op->begin, op->end, "as", op->rhs,
                            new TypeLiteral(op->begin, op->end, int_type));
        op->rhs->type = int_type;
        op->func = scope.B_OP.at({op->val, {int_type, int_type}});
        op->type = bool_type;
      } else {
        /** Member call */
        if (op->val.size() >= 6 && op->val.substr(0, 6) == ".call.") {
          if (op->lhs->type.getPtr() == 0 && !op->lhs->type.isFn()) {
            if (!(op->lhs->type.getLval() || op->lhs->type.getRef())) {
              op->lhs = makeLval(op->lhs, scope);
            }
            auto ltype = op->lhs->type;
            op->lhs = new PrefixOperator(op->begin, op->end, "&", op->lhs);
            op->lhs->type = ltype;
            op->lhs->type.setLval(false);
            op->lhs->type.setPtr(1);
          }
        }

        std::vector<types::Type> types;
        std::vector<Exp**> exps;
        types.push_back(op->lhs->type);
        exps.push_back(&op->lhs);

        types.back().setLval(false);
        if (auto tuple = dynamic_cast<Tuple *>(op->rhs)) {
          for (auto exp : tuple->content) {
            types.push_back(exp->type);
            exps.push_back(&exp);
          }
        } else {
          types.push_back(op->rhs->type);
          exps.push_back(&op->rhs);
        }

        for (auto &i : types) i.setLval(false);
        for (auto &i : types) i.setRef(false);

        types::FnHead func_head{op->val, types};
        if (op->val.size() >= 6 && op->val.substr(0, 6) == ".call." &&
            op->lhs->type.isFn()) {
          const auto& args = op->lhs->type.getTypes();
          op->func = nullptr;
          op->type = args.front();

          for (auto i = 1; i < args.size(); ++i) {
            /** Call copy ctors */
            if (!args[i].getRef()) copyExp(*exps[i], scope);
            /** Cast to lval if needed */
            else if (!(*exps[i])->type.getLval() && !(*exps[i])->type.getRef())
              makeLval(*(exps[i]), scope);
          }
        } else if (scope.B_OP.contains(func_head)) {
          auto bop = scope.B_OP.at(func_head);
          op->func = bop;
          exp->type = bop->type;
          for (int i = 0; i < bop->args.size(); ++i) {
            /** Call copy ctors */
            if (!bop->args[i].type.getRef()) {} // copyExp(*exps[i], scope);
            /** Cast to lval if needed */
            else if (!(*exps[i])->type.getLval() && !(*exps[i])->type.getRef())
              makeLval(*(exps[i]), scope);
          }
        } else {
          if (op->val.substr(0, 6) == ".call.") {
            std::string types_str;
            for (auto &i : types)
              if (&i != &types.front()) types_str += i.toString() + " ";
            throw ParserError(op->begin, op->end,
                              "There is no instance of member function `" +
                                  op->val.substr(6, op->val.size() - 6) +
                                  "` for type `" + types.front().toString() +
                                  "` with args types: " + types_str);
          } else {
            std::string types_str;
            for (auto &i : types) types_str += i.toString() + " ";
            throw ParserError(op->begin, op->end,
                              "There is no instance of binary operator '" +
                                  op->val + "' for types: " + types_str);
          }
        }
      }
    }
  } else if (auto op = dynamic_cast<PostfixOperator *>(exp)) {
    op->child = postprocess(op->child, scope);
    std::vector<types::Type> types;
    std::vector<Exp **> exps;
    if (auto tuple = dynamic_cast<Tuple *>(op->child)) {
      for (auto exp : tuple->content) {
        types.push_back(exp->type);
        exps.push_back(&exp);
      }
    } else {
      types.push_back(op->child->type);
      exps.push_back(&op->child);
    }

    for (auto &i : types) i.setLval(false);
    for (auto &i : types) i.setRef(false);

    types::FnHead func_head{op->val, types};
    if (scope.PO_OP.contains(func_head)) {
      auto& poop = scope.PO_OP.at(func_head);
      op->func = poop;
      exp->type = poop->type;

      for (int i = 0; i < poop->args.size(); ++i) {
        /** Call copy ctors */
        if (!poop->args[i].type.getRef())
          copyExp(*exps[i], scope);
        /** Cast to lval if needed */
        else if (!(*exps[i])->type.getLval() && !(*exps[i])->type.getRef())
          makeLval(*(exps[i]), scope);
      }
    } else {
      std::string types_str;
      for (auto &i : types) {
        types_str += i.toString() + " ";
      }
      throw ParserError(op->begin, op->end,
                        "There is no instance of postfix operator '" + op->val +
                            "' for types: " + types_str);
    }
  } else if (auto op = dynamic_cast<PrefixOperator *>(exp)) {
    if (scope.vars.contains(op->val)) {
      auto pr = (*((&op->begin) + 1)).val;
      std::string call_name = ".call.call";
      if (pr == "[") call_name = ".call.sub";
      
      auto var = scope.vars_id.at(scope.vars.at(op->val)->id);

      exp = postprocess(
          new BinOperator(op->begin, op->end, call_name,
                          new IdLiteral(op->begin, op->end, op->val),
                          op->child),
          scope);
      return exp;
    }

    op->child = postprocess(op->child, scope);

    if (op->val == "&") {
      makeLval(op->child, scope);
      if (!op->child->type.getLval() && !op->child->type.getRef())
        throw ParserError(op->begin, op->end, "Cannot get ptr of rval");
      exp->type = op->child->type;
      exp->type.setPtr(op->child->type.getPtr() + 1);
      exp->type.setLval(false);

      return exp;
    } else if (op->val == "*" && op->child->type.getPtr()) {
      exp->type = op->child->type;
      exp->type.setPtr(op->child->type.getPtr() - 1);
      exp->type.setLval(true);

      return exp;
    } else if (op->val == "sizeof") {
      exp->type = types::Type(types::i64T);
      return exp;
    }

    std::vector<types::Type> types;
    std::vector<Exp**> exps;
    if (auto tuple = dynamic_cast<Tuple *>(op->child)) {
      for (auto& exp : tuple->content) {
        types.push_back(exp->type);
        exps.push_back(&exp);
      }
    } else {
      types.push_back(op->child->type);
      exps.push_back(&op->child);
    }

    for (auto &i : types) i.setLval(false);
    for (auto &i : types) i.setRef(false);

    types::FnHead func_head{op->val, types};
    if (scope.PR_OP.contains(func_head)) {
      auto& prop = scope.PR_OP.at(func_head);
      op->func = prop;
      exp->type = prop->type;

      for (int i = 0; i < prop->args.size(); ++i) {
        /** Call copy ctors */
        if (!prop->args[i].type.getRef()) copyExp(*exps[i], scope);
        /** Cast to lval if needed */
        else if (!(*exps[i])->type.getLval() && !(*exps[i])->type.getRef())
          makeLval(*(exps[i]), scope);
      }
    } else {
      std::string types_str;

      for (auto &i : types) types_str += i.toString() + " ";

      throw ParserError(op->begin, op->end, "There is no instance of prefix operator '" +
          op->val +
          "' for types: " + types_str);
    }
  } else if (auto tpl = dynamic_cast<Tuple *>(exp)) {
    for (auto &i : tpl->content) i = postprocess(i, scope);

    if (tpl->content.empty()) {
      tpl->type = types::Type();
    } else {
      auto last_type = tpl->content.back()->type;
      tpl->type = last_type;
    }
    return tpl;
  }
  return exp;
}

Exp *parse(std::vector<Token>::iterator begin,
           std::vector<Token>::iterator end, Scope &scope_info) {
  Exp *exp = buildExp(scope_info, begin, end);
  if (zhdata.flags["exp_parser_logs"]) zhexp::printExpTree(exp);
  exp = postprocess(exp, scope_info);
  if (zhdata.flags["exp_parser_logs"]) zhexp::printExpTree(exp);
  return exp;
}
};
