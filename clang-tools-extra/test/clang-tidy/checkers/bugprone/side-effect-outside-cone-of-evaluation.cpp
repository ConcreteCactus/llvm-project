// RUN: %check_clang_tidy %s bugprone-side-effect-outside-cone-of-evaluation %t

extern void contract_assert(bool);

int g;

bool bad1() {
    g++;
    // CHECK-MESSAGES: :[[@LINE-1]]:5: warning: side effect outside cone of eval
    return g;
}

bool good1(int x) {
    int l = x;
    l++;
    return l == 2;
}

void f() {
    contract_assert(1);

    int a = 1;
    contract_assert(a--);
    // CHECK-MESSAGES: :[[@LINE-1]]:21: warning: side effect outside cone of eval

    contract_assert(a-- + 1);
    // CHECK-MESSAGES: :[[@LINE-1]]:21: warning: side effect outside cone of eval
    
    contract_assert(bad1());

    contract_assert(good1(9));
}
