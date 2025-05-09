; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc -mtriple=aarch64-linux-gnu -mattr=+sve2p1 -mattr=+sve-b16b16 -verify-machineinstrs < %s | FileCheck %s

define <vscale x 8 x bfloat> @bfmla_m(<vscale x 8 x i1> %pg, <vscale x 8 x bfloat> %a, <vscale x 8 x bfloat> %b, <vscale x 8 x bfloat> %c){
; CHECK-LABEL: bfmla_m:
; CHECK:       // %bb.0:
; CHECK-NEXT:    bfmla z0.h, p0/m, z1.h, z2.h
; CHECK-NEXT:    ret
  %res = call <vscale x 8 x bfloat> @llvm.aarch64.sve.fmla.nxv8bf16(<vscale x 8 x i1> %pg, <vscale x 8 x bfloat> %a, <vscale x 8 x bfloat> %b, <vscale x 8 x bfloat> %c)
  ret <vscale x 8 x bfloat> %res
}

define <vscale x 8 x bfloat> @bfmla_x(<vscale x 8 x i1> %pg, <vscale x 8 x bfloat> %a, <vscale x 8 x bfloat> %b, <vscale x 8 x bfloat> %c){
; CHECK-LABEL: bfmla_x:
; CHECK:       // %bb.0:
; CHECK-NEXT:    bfmla z0.h, p0/m, z1.h, z2.h
; CHECK-NEXT:    ret
  %res = call <vscale x 8 x bfloat> @llvm.aarch64.sve.fmla.u.nxv8bf16(<vscale x 8 x i1> %pg, <vscale x 8 x bfloat> %a, <vscale x 8 x bfloat> %b, <vscale x 8 x bfloat> %c)
  ret <vscale x 8 x bfloat> %res
}

define <vscale x 8 x bfloat> @bfmla_z(<vscale x 8 x i1> %pg, <vscale x 8 x bfloat> %a, <vscale x 8 x bfloat> %b, <vscale x 8 x bfloat> %c){
; CHECK-LABEL: bfmla_z:
; CHECK:       // %bb.0:
; CHECK-NEXT:    movi v3.2d, #0000000000000000
; CHECK-NEXT:    sel z0.h, p0, z0.h, z3.h
; CHECK-NEXT:    bfmla z0.h, p0/m, z1.h, z2.h
; CHECK-NEXT:    ret
  %a_z = select <vscale x 8 x i1> %pg, <vscale x 8 x bfloat> %a, <vscale x 8 x bfloat> zeroinitializer
  %res = call <vscale x 8 x bfloat> @llvm.aarch64.sve.fmla.nxv8bf16(<vscale x 8 x i1> %pg, <vscale x 8 x bfloat> %a_z, <vscale x 8 x bfloat> %b, <vscale x 8 x bfloat> %c)
  ret <vscale x 8 x bfloat> %res
}

declare <vscale x 8 x bfloat> @llvm.aarch64.sve.fmla.nxv8bf16(<vscale x 8 x i1>, <vscale x 8 x bfloat>, <vscale x 8 x bfloat>, <vscale x 8 x bfloat>)
declare <vscale x 8 x bfloat> @llvm.aarch64.sve.fmla.u.nxv8bf16(<vscale x 8 x i1>, <vscale x 8 x bfloat>, <vscale x 8 x bfloat>, <vscale x 8 x bfloat>)
