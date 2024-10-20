// RUN: %check_clang_tidy %s bugprone-conflicting-global-read-write %t

int GlobalVarA;

int incGlobalVarA(void) {
    GlobalVarA++;
    return 0;
}

int getGlobalVarA(void) {
    return GlobalVarA;
}

int testFunc1(void) {

    int B = getGlobalVarA() + incGlobalVarA();
    (void)B;
    // CHECK-MESSAGES: :[[@LINE-1]]:13: warning: read/write conflict on global variable

    return GlobalVarA + incGlobalVarA();
    // CHECK-MESSAGES: :[[@LINE-1]]:12: warning: read/write conflict on global variable

}

int addAll(int A, int B, int C, int D) {
    return A + B + C + D;
}

int testFunc2(void) {
    int B;
    (void)B;
    // Make sure the order does not affect the outcome

    B = getGlobalVarA() + (GlobalVarA++);
    // CHECK-MESSAGES: :[[@LINE-1]]:9: warning: read/write conflict on global variable

    B = (GlobalVarA++) + getGlobalVarA();
    // CHECK-MESSAGES: :[[@LINE-1]]:9: warning: read/write conflict on global variable

    B = incGlobalVarA() + GlobalVarA;
    // CHECK-MESSAGES: :[[@LINE-1]]:9: warning: read/write conflict on global variable

    B = addAll(GlobalVarA++, getGlobalVarA(), 0, 0);
    // CHECK-MESSAGES: :[[@LINE-1]]:9: warning: read/write conflict on global variable

    B = addAll(getGlobalVarA(), GlobalVarA++, 0, 0);
    // CHECK-MESSAGES: :[[@LINE-1]]:9: warning: read/write conflict on global variable

    // This is already checked by the unsequenced clang warning, so we don't
    // want to warn about this.
    return GlobalVarA + (++GlobalVarA);
}

int testFunc3(void) {

    // Make sure double reads are not flagged
    int B = GlobalVarA + GlobalVarA;
    B = GlobalVarA + getGlobalVarA();

    return GlobalVarA - GlobalVarA;
}

bool testFunc4(void) {

    // Make sure || and && operators are not flagged
    bool B = GlobalVarA || (GlobalVarA++);
    if(GlobalVarA && (GlobalVarA--)) {

        B = GlobalVarA || (GlobalVarA++) + getGlobalVarA();
        // CHECK-MESSAGES: :[[@LINE-1]]:18: warning: read/write conflict on global variable

        return (++GlobalVarA) || B || getGlobalVarA();
    }

    return false;
}

int incArg(int& P) {
    P++;
    return 0;
}

int testFunc5() {

    if(GlobalVarA > incGlobalVarA()) {
    // CHECK-MESSAGES: :[[@LINE-1]]:8: warning: read/write conflict on global variable

        return 1;
    }
    return 0;
}

class TestClass1 {
    static int StaticVar1;

    int incStaticVar1() {
        StaticVar1++;
        return 0;
    }

    int getStaticVar1() {
        return StaticVar1;
    }

    int testClass1MemberFunc1() {
        
        return incStaticVar1() + getStaticVar1();
        // CHECK-MESSAGES: :[[@LINE-1]]:16: warning: read/write conflict on global variable

    }
};
