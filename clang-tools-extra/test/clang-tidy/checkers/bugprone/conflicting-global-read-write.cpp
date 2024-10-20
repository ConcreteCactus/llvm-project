// RUN: %check_clang_tidy %s bugprone-conflicting-global-read-write %t

int a;

int f(void) {
    a++;
    return 0;
}

int g(void) {
    return a;
}

int t1(void) {

    int b = g() + f();
    // CHECK-MESSAGES: :[[@LINE-1]]:13: warning: read/write conflict on global variable

    return a + f();
    // CHECK-MESSAGES: :[[@LINE-1]]:12: warning: read/write conflict on global variable

}

int h(int a, int b, int c, int d) {
    return a + b + c + d;
}

int t2(void) {
    int b;

    // Make sure the order does not affect the outcome

    b = g() + (a++);
    // CHECK-MESSAGES: :[[@LINE-1]]:9: warning: read/write conflict on global variable

    b = (a++) + g();
    // CHECK-MESSAGES: :[[@LINE-1]]:9: warning: read/write conflict on global variable

    b = f() + a;
    // CHECK-MESSAGES: :[[@LINE-1]]:9: warning: read/write conflict on global variable

    b = h(a++, g(), 0, 0);
    // CHECK-MESSAGES: :[[@LINE-1]]:9: warning: read/write conflict on global variable

    b = h(g(), a++, 0, 0);
    // CHECK-MESSAGES: :[[@LINE-1]]:9: warning: read/write conflict on global variable

    // This is already checked by the unsequenced clang warning, so we don't
    // want to warn about this.
    return a + (++a);
}

int t3(void) {

    // Make sure double reads are not flagged
    int b = a + a;
    b = a + g();

    return a - a;
}

bool t4(void) {

    // Make sure || and && operators are not flagged
    bool b = a || (a++);
    if(a && (a--)) {

        b = a || (a++) + g();
        // CHECK-MESSAGES: :[[@LINE-1]]:18: warning: read/write conflict on global variable

        return (++a) || b || g();
    }
}

int i(int& p) {
    p++;
    return 0;
}

int t5() {

    if(a > f()) {
    // CHECK-MESSAGES: :[[@LINE-1]]:8: warning: read/write conflict on global variable

        return 1;
    }
    return 0;
}

class C1 {
    static int B;

    int F() {
        B++;
        return 0;
    }

    int G() {
        return B;
    }

    int T6() {
        
        return F() + G();
        // CHECK-MESSAGES: :[[@LINE-1]]:16: warning: read/write conflict on global variable

    }
};

struct {
    int A;
    int B;
} GlobalStruct;

int G;

int t7() {
    int C;

    // There is a clang warning here
    C = G + G++;

    // But not here
    C = GlobalStruct.A + GlobalStruct.A++;


    // Should I check the latter case as well?
    
    return C;
}
