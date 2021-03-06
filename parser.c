#include "ccc.h"

static Var *locals;
static Var *globals;
static Node *stmt(Token **token);
static Node *expr(Token **token);
static Node *compound_stmt(Token **token);
static Node *assign(Token **token);
static bool equal(Token *tok, char *c);
static void next(Token **token);

static Ty gettk(char *type) {
  if (strcmp(type, "int") == 0) {
    return TY_INT;
  }

  error("unknown type: %s\n", type);
}

static Type *gettype(Token **token) {
  Token *tok = *token;
  Type *t = calloc(1, sizeof(Type));

  if (tok->kind != TK_IDENT || tok->next->kind != TK_IDENT) {
    return NULL;
  }

  if (equal(tok, "*")) { 
    next(token);
    t->type = TY_PTR;
    t->ptr_to = gettype(token);
  } else {
    t->type = gettk(tok->str);
    next(token);
  }

  return t;
}

static void next(Token **token) {
  // fprintf(stderr, "%s,", strtk((*token)->kind));
  // fprintf(stderr, "%s,", (*token)->str);
  // fprintf(stderr, "%d,", (*token)->val);
  // fprintf(stderr, "%d\n", (*token)->len);

  *token = (*token)->next;
}

// if token is the same as op, read the next token;
// otherwise return error
static void expect(Token **token, char *op) {
  Token *tok = *token;
  if (tok->kind != TK_RESERVED || strlen(op) != tok->len ||
      memcmp(tok->str, op, tok->len) != 0) {
    fprintf(stderr, "given token kind is %d\n", tok->kind);
    fprintf(stderr, "given token len is %d\n", tok->len);
    fprintf(stderr, "given token str is %s\n", tok->str);
    error_at("", "error happened, expected %s but given %s", op, tok->str);
  }

  next(token);
}

static int expect_number(Token **token) {
  Token *tok = *token;
  if (tok->kind != TK_NUM) {
    fprintf(stderr, "given token kind is %d\n", tok->kind);
    fprintf(stderr, "given token len is %d\n", tok->len);
    fprintf(stderr, "given token str is %s\n", tok->str);
    error_at(tok->str, "%s is not a number", tok->str);
  }

  int val = tok->val;
  next(token);
  return val;
}

static bool equal(Token *tok, char *c) {
  if (tok->len == strlen(c) && memcmp(tok->str, c, tok->len) == 0) {
    return true;
  }

  return false;
}

static bool at_eof(Token *tok) { return tok->kind == TK_EOF; }

static Node *new_node(NodeKind kind, char *str, Node *lhs, Node *rhs) {
  Node *node = calloc(1, sizeof(Node));
  node->kind = kind;
  node->lhs = lhs;
  node->rhs = rhs;
  node->str = str;
  return node;
}

static Node *new_node_block(Node *body) {
  Node *node = calloc(1, sizeof(Node));
  node->kind = ND_BLOCK;
  node->body = body;
  return node;
}

static Node *new_node_num(int val) {
  Node *node = calloc(1, sizeof(Node));
  node->kind = ND_NUM;
  node->val = val;
  return node;
}

static Var *find_var(Token *tok) {
  for (Var *var = locals; var; var = var->next) {
    if (var->len == tok->len && !memcmp(tok->str, var->name, tok->len)) {
      return var;
    }
  }

  for (Var *var = globals; var; var = var->next) {
    if (var->len == tok->len && !memcmp(tok->str, var->name, tok->len)) {
      return var;
    }
  }

  return NULL;
}

// funcall = (assign ("," assign)*)? ")"
static Node *funcall(Token **token) {
  Node *node = calloc(1, sizeof(Node));
  node->kind = ND_FNCALL;
  node->str = (*token)->str;
  next(token);
  expect(token, "(");

  if (!equal(*token, ")")) {
    Node *p = assign(token);
    node->params = p;
    while (equal(*token, ",")) {
      next(token);
      p = p->nextp = assign(token);
    }
  }

  expect(token, ")");
  return node;
}

static Node *expect_ident(Token **token) {
  Node *node = calloc(1, sizeof(Node));
  node->kind = ND_LVAR;
  node->str = (*token)->str;

  Var *var = find_var(*token);

  if (var) {
    next(token);
    node->offset = var->offset;
    return node;
  }

  var = calloc(1, sizeof(Var));
  var->len = (*token)->len;
  var->name = (*token)->str;
  var->offset = 8;

  if (locals) {
    var->next = locals;
    var->offset = locals->offset + 8;
  }

  node->offset = var->offset;
  locals = var;
  next(token);
  return node;
}

static Node *primary(Token **token) {
  if (equal(*token, "(")) {
    next(token);
    Node *node = expr(token);
    expect(token, ")");
    return node;
  }

  if ((*token)->kind == TK_IDENT) {
    if (equal((*token)->next, "(")) {
      return funcall(token);
    }
    return expect_ident(token);
  }

  return new_node_num(expect_number(token));
}

static Node *unary(Token **token) {
  if (equal(*token, "+")) {
    next(token);
    return primary(token);
  } 

  if (equal(*token, "-")) {
    next(token);
    return new_node(ND_SUB, "-", new_node_num(0), primary(token));
  } 

  if (equal(*token, "*")){
    next(token);
    return new_node(ND_DEREF, "*",  unary(token),new_node_num(0));
  }

  if (equal(*token, "&")){
    next(token);
    return new_node(ND_ADDR, "&",  unary(token), new_node_num(0));
  }

  return primary(token);
}

static Node *mul(Token **token) {
  Node *node = unary(token);

  if (equal(*token, "*")) {
    next(token);
    node = new_node(ND_MUL, "*", node, mul(token));
  } else if (equal(*token, "/")) {
    next(token);
    node = new_node(ND_DIV, "/", node, mul(token));
  }
  return node;
}

static Node *add(Token **token) {
  Node *node = mul(token);

  if (equal(*token, "+")) {
    next(token);
    node = new_node(ND_ADD, "+", node, add(token));
  } else if (equal(*token, "-")) {
    next(token);
    node = new_node(ND_SUB, "-", node, add(token));
  }

  return node;
}

static Node *relational(Token **token) {
  Node *node = add(token);

  if (equal(*token, "<=")) {
    // node = new_node(ND_SUB, (*token)->str, node, mul(token));
    next(token);
    return new_node(ND_LTE, "<=", node, add(token));
  } else if (equal(*token, ">=")) {
    // node = new_node(ND_SUB, (*token)->str, node, mul(token));
    next(token);
    return new_node(ND_GTE, ">=", node, add(token));
  } else if (equal(*token, "<")) {
    next(token);
    return new_node(ND_LT, "<", node, add(token));
  } else if (equal(*token, ">")) {
    next(token);
    return new_node(ND_GT, ">", node, add(token));
  }
  return node;
}

static Node *equality(Token **token) {
  Node *node = relational(token);

  if (equal(*token, "==")) {
    next(token);
    return new_node(ND_EQ, "==", node, relational(token));
  } else if (equal(*token, "!=")) {
    next(token);
    return new_node(ND_NEQ, "!=", node, relational(token));
  } else {
    return node;
  }
}

static Node *assign(Token **token) {
  Node *node = equality(token);

  if (equal(*token, "=")) {
    next(token);
    return new_node(ND_ASSIGN, "=", node, assign(token));
  }

  return node;
}

static Node *expr(Token **token) {
  return assign(token); 
}

// compound-stmt = stmt* "}"
static Node *compound_stmt(Token **token) {
  Node head = {};
  Node *cur = &head;

  while (!equal(*token, "}")) {
    cur = cur->next = stmt(token);
  }

  Node *node = new_node_block(head.next);
  expect(token, "}");
  return node;
}

// init-declarator= types ( ident | assign ) 
static Node *init_declarator(Token **token) {
  Token *tok = *token;
  Node *node; 
  Type *ty;

  if (tok->next->kind == TK_IDENT) {
    ty = gettype(token);
  }

  if (equal((*token)->next, "=")) {
    node = assign(token);
  } else {
    node = expect_ident(token);
  }

  node->ty = ty;
  return node;
}

// stmt = "return" expr ";"
//      | "{" compound-stmt
//      | "if" "(" expr ")" stmt ("else" stmt)?
//      | "for" "(" expr? ";" expr? ";" expr? ")" stmt
//      | "while" "(" expr ")" stmt
//      | types ";"
//      | expr-stmt
static Node *stmt(Token **token) {
  Node *node;
  Type *ty = gettype(token);

  if (equal(*token, "return")) {
    next(token);
    node = calloc(1, sizeof(Node));
    node->kind = ND_RETURN;
    if (equal(*token, ";")) {
      next(token);
      return node;
    }
    node->lhs = expr(token);
    expect(token, ";");
    return node;
  }

  if (equal(*token, "{")) {
    next(token);
    return compound_stmt(token);
  }

  if (equal(*token, "if")) {
    next(token);
    expect(token, "(");

    node = calloc(1, sizeof(Node));
    node->kind = ND_IF;
    node->cond = expr(token);

    expect(token, ")");
    node->then = stmt(token);

    if (equal(*token, "else")) {
      next(token);
      node->els = stmt(token);
    }

    return node;
  }

  if (equal(*token, "while")) {
    next(token);
    expect(token, "(");

    node = calloc(1, sizeof(Node));
    node->kind = ND_FOR;
    node->cond = expr(token);

    expect(token, ")");
    node->then = stmt(token);

    return node;
  }

  if (equal(*token, "for")) {
    next(token);
    expect(token, "(");
    node = calloc(1, sizeof(Node));
    node->kind = ND_FOR;

    if (!equal(*token, ";")) {
      node->init = init_declarator(token);
    }
    expect(token, ";");

    if (!equal(*token, ";")) {
      node->cond = expr(token);
    }
    expect(token, ";");

    if (!equal(*token, ")")) {
      node->inc = expr(token);
    }

    expect(token, ")");
    node->then = stmt(token);

    return node;
  }

  node = expr(token);
  node->ty = ty;
  expect(token, ";");
  return node;
}

// function = ident "(" ident? ("," ident)?  ")" "{" compound-stmt
Var *function_def(Token **token) {
  //next(token);

  if ((*token)->kind != TK_IDENT) {
    error("Failed to parse. Got %s\n", (*token)->str);
  }

  Node *node = calloc(1, sizeof(Node));
  node->kind = ND_BLOCK;

  Var *func = calloc(1, sizeof(Var));
  func->name = (*token)->str;
  func->is_func = true;
  func->argc = 6;

  next(token);
  expect(token, "(");

  if (!equal(*token, ")")) {
    Type *ty = gettype(token);
    if (!ty) {
      error("function arg needs type declaration\n");
    }

    Node *arg = expect_ident(token);
    arg->ty=ty;
    if (func->args) {
      func->args->next = arg;
    } else {
      func->args = arg;
    }
    func->argc++;

    while (equal(*token, ",")) {
      if (++func->argc > 128) {
        error("too many arguments for a function");
      }

      next(token);
      Type *ty = gettype(token);
      if (!ty) {
        error("function arg needs type declaration\n");
      }

      func->args->next = expect_ident(token);
      func->args->next->ty = ty;
    }
  }

  expect(token, ")");
  expect(token, "{");

  func->body = compound_stmt(token);
  func->locals = locals;
  locals = NULL;
  return func;
}

// bultin-type = "int"
// types = ("*")? bultin-type | ident
// declaration = types ( function | ( ident | assign ) ";" )
Var *declaration(Token **token) {
  Token *tok = *token;
  Var *var = calloc(1, sizeof(Var));
  var->ty = gettype(token);

  if (equal(tok->next->next, "(")) {
    Var *v = function_def(token);
    v->ty = var->ty;
    return v;
  } 

  Node *node = init_declarator(token);
  expect(token, ";");
  
  var->body = node;
  return var; 
}

// program = function-definition
Var *program(Token **token) { return declaration(token); }

void parse(Program **prog) {
  Program *p = *prog;
  Token *cur = p->tok;

  Segment *cur_seg = calloc(1, sizeof(Segment));
  p->head = cur_seg;

  while (!at_eof(cur)) {
    cur_seg->contents = program(&cur);
    if (!at_eof(cur)) {
      cur_seg->next = calloc(1, sizeof(Segment));
      cur_seg = cur_seg->next;
    }
  }
}

