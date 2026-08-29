// RUN: %check_clang_tidy %s bugprone-side-effect-outside-cone-of-evaluation %t

extern void contract_assert(bool);

void f() {
    contract_assert(1);

    int a = 1;
    contract_assert(a--);
}
