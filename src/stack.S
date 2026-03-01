#define _CONCAT(a, b) a##b
#define CONCAT(a, b) _CONCAT(a, b)
#define G(l) CONCAT(__USER_LABEL_PREFIX__, l)

.global G(on_alt_stack)

.text
.align 2
G(on_alt_stack):
// r4 - void *stack
// r5 - void (*fun)()
  mov.l r15, @-r4
  sts.l pr, @-r4
  jsr @r5
  mov r4, r15
  lds.l @r15+, pr
  rts
  mov.l @r15+, r15
