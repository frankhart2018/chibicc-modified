#include "chibicc.h"

static int depth;
static char *argreg[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
static Function *current_fn;

static void gen_expr(Node *node);

static int count(void) {
  static int i = 1;
  return i++;
}

static void push(void) {
  printf("push rax\n");
  depth++;
}

static void pop(char *arg) {
  printf("pop %s\n", arg);
  depth--;
}

// Round up `n` to the nearest multiple of `align`
// For instance, align_to(5, 8) returns 8 and align_to(11, 8) returns 16
static int align_to(int n, int align) {
  return (n + align - 1) / align * align;
}

// Compute the absolute address of a given node
// It's an error if a given node does not reside in memory
static void gen_addr(Node *node) {
  switch(node->kind) {
  case ND_VAR:
    printf("lea $_%d, rax\n", node->var->offset);
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    return;
  }

  error_tok(node->tok, "Not an lvalue");
}

// Generate code for a given node
static void gen_expr(Node *node) {
  switch (node->kind) {
  case ND_NUM:
    printf("mov $%d, rax\n", node->val);
    return;
  case ND_NEG:
    gen_expr(node->lhs);
    printf("neg rax\n");
    return;
  case ND_VAR:
    gen_addr(node);
    printf("mov (rax), rax\n");
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    printf("mov (rax), rax\n");
    return;
  case ND_ADDR:
    gen_addr(node->lhs);
    return;
  case ND_ASSIGN:
    gen_addr(node->lhs);
    push();
    gen_expr(node->rhs);
    pop("rdi");
    printf("mov rax, (rdi)\n");
    return;
  case ND_FUNCALL: {
    int nargs = 0;
    for(Node *arg = node->args; arg; arg = arg->next) {
      gen_expr(arg);
      push();
      nargs++;
    }

    for(int i = nargs - 1; i >= 0; i--) 
      pop(argreg[i]);

    printf("mov $0, rax\n");
    printf("call .L.%s\n", node->funcname);
    return;
  }
  }

  gen_expr(node->rhs);
  push();
  gen_expr(node->lhs);
  pop("rdi");

  switch (node->kind) {
  case ND_ADD:
    printf("add rdi, rax\n");
    return;
  case ND_SUB:
    printf("sub rdi, rax\n");
    return;
  case ND_MUL:
    printf("imul rdi, rax\n");
    return;
  case ND_DIV:
    printf("cqo\n");
    printf("idiv rdi, rax\n");
    return;
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE:
    printf("cmp rdi, rax\n");

    if (node->kind == ND_EQ)
      printf("sete al\n");
    else if (node->kind == ND_NE)
      printf("setne al\n");
    else if (node->kind == ND_LT)
      printf("setl al\n");
    else if (node->kind == ND_LE)
      printf("setle al\n");

    printf("movzb al, rax\n");
    return;
  }

  error_tok(node->tok, "Invalid expression");
}

static void gen_stmt(Node *node) {
  switch(node->kind) {
  case ND_IF: {
    int c = count();
    gen_expr(node->cond);
    printf("cmp $0, rax\n");
    printf("je .L.else_%d\n", c);
    gen_stmt(node->then);
    printf("jmp .L.end_%d\n", c);
    printf(".L.else_%d:\n", c);
    if(node->els)
      gen_stmt(node->els);
    printf(".L.end_%d:\n", c);
    return;
  }
  case ND_FOR: {
    int c = count();
    if(node->init)
      gen_stmt(node->init);
    printf(".L.begin_%d:\n", c);
    if(node->cond) {
      gen_expr(node->cond);
      printf("cmp $0, rax\n");
      printf("je .L.end_%d\n", c);
    }
    gen_stmt(node->then);
    if(node->inc)
      gen_expr(node->inc);
    printf("jmp .L.begin_%d\n", c);
    printf(".L.end_%d:\n", c);
    return;
  }
  case ND_BLOCK:
    for(Node *n = node->body; n; n = n->next) 
      gen_stmt(n);
    return;
  case ND_RETURN:
    gen_expr(node->lhs);
    printf("jmp .L.return_%s\n", current_fn->name);
    return;
  case ND_EXPR_STMT:
    gen_expr(node->lhs);
    return;
  }

  error_tok(node->tok, "Invalid statement");
}

// Assign offsets to local variables
static void assign_lvar_offsets(Function *prog) {
  for(Function *fn = prog; fn; fn = fn->next) {
    int offset = 0;
    for(Obj *var = fn->locals; var; var = var->next) {
      offset += 8;
      var->offset = offset;
    }

    fn->stack_size = align_to(offset, 16);
  }
}

void codegen(Function *prog) {
  assign_lvar_offsets(prog);

  for(Function *fn = prog; fn; fn = fn->next) {
    printf(".L.%s:\n", fn->name);
    current_fn = fn;

    // Prologue
    printf("push rbp\n");
    printf("mov rsp, rbp\n");
    printf("sub $%d, rsp\n", fn->stack_size);

    // Save passed-by-register arguments to the stack
    int i = 0;
    for(Obj *var = fn->params; var; var = var->next)
      printf("mov %s, $_%d\n", argreg[i++], var->offset);

    // Emit code
    gen_stmt(fn->body);
    assert(depth == 0);

    printf(".L.return_%s:\n", fn->name);
    printf("mov rbp, rsp\n");
    printf("pop rbp\n");
    printf("ret\n");
  }
}
