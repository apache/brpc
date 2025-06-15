Disassembly of section .init:

0000000000001000 <_init>:
    1000:       f3 0f 1e fa             endbr64
    1004:       48 83 ec 08             sub    $0x8,%rsp
    1008:       48 8b 05 e1 2f 00 00    mov    0x2fe1(%rip),%rax        # 3ff0 <__gmon_start__@Base>
    100f:       48 85 c0                test   %rax,%rax
    1012:       74 02                   je     1016 <_init+0x16>
    1014:       ff d0                   call   *%rax
    1016:       48 83 c4 08             add    $0x8,%rsp
    101a:       c3                      ret

Disassembly of section .plt:

0000000000001020 <.plt>:
    1020:       ff 35 8a 2f 00 00       push   0x2f8a(%rip)        # 3fb0 <_GLOBAL_OFFSET_TABLE_+0x8>
    1026:       ff 25 8c 2f 00 00       jmp    *0x2f8c(%rip)        # 3fb8 <_GLOBAL_OFFSET_TABLE_+0x10>
    102c:       0f 1f 40 00             nopl   0x0(%rax)
    1030:       f3 0f 1e fa             endbr64
    1034:       68 00 00 00 00          push   $0x0
    1039:       e9 e2 ff ff ff          jmp    1020 <_init+0x20>
    103e:       66 90                   xchg   %ax,%ax
    1040:       f3 0f 1e fa             endbr64
    1044:       68 01 00 00 00          push   $0x1
    1049:       e9 d2 ff ff ff          jmp    1020 <_init+0x20>
    104e:       66 90                   xchg   %ax,%ax

Disassembly of section .plt.got:

0000000000001050 <__cxa_finalize@plt>:
    1050:       f3 0f 1e fa             endbr64
    1054:       ff 25 76 2f 00 00       jmp    *0x2f76(%rip)        # 3fd0 <__cxa_finalize@GLIBC_2.2.5>
    105a:       66 0f 1f 44 00 00       nopw   0x0(%rax,%rax,1)

Disassembly of section .plt.sec:

0000000000001060 <_ZStlsISt11char_traitsIcEERSt13basic_ostreamIcT_ES5_PKc@plt>:
    1060:       f3 0f 1e fa             endbr64
    1064:       ff 25 56 2f 00 00       jmp    *0x2f56(%rip)        # 3fc0 <_ZStlsISt11char_traitsIcEERSt13basic_ostreamIcT_ES5_PKc@GLIBCXX_3.4>
    106a:       66 0f 1f 44 00 00       nopw   0x0(%rax,%rax,1)

0000000000001070 <_ZNSolsEPFRSoS_E@plt>:
    1070:       f3 0f 1e fa             endbr64
    1074:       ff 25 4e 2f 00 00       jmp    *0x2f4e(%rip)        # 3fc8 <_ZNSolsEPFRSoS_E@GLIBCXX_3.4>
    107a:       66 0f 1f 44 00 00       nopw   0x0(%rax,%rax,1)

Disassembly of section .text:

0000000000001080 <_start>:
    1080:       f3 0f 1e fa             endbr64
    1084:       31 ed                   xor    %ebp,%ebp
    1086:       49 89 d1                mov    %rdx,%r9
    1089:       5e                      pop    %rsi
    108a:       48 89 e2                mov    %rsp,%rdx
    108d:       48 83 e4 f0             and    $0xfffffffffffffff0,%rsp
    1091:       50                      push   %rax
    1092:       54                      push   %rsp
    1093:       45 31 c0                xor    %r8d,%r8d
    1096:       31 c9                   xor    %ecx,%ecx
    1098:       48 8d 3d 7c 01 00 00    lea    0x17c(%rip),%rdi        # 121b <main>
    109f:       ff 15 3b 2f 00 00       call   *0x2f3b(%rip)        # 3fe0 <__libc_start_main@GLIBC_2.34>
    10a5:       f4                      hlt
    10a6:       66 2e 0f 1f 84 00 00    cs nopw 0x0(%rax,%rax,1)
    10ad:       00 00 00 

00000000000010b0 <deregister_tm_clones>:
    10b0:       48 8d 3d 61 2f 00 00    lea    0x2f61(%rip),%rdi        # 4018 <__TMC_END__>
    10b7:       48 8d 05 5a 2f 00 00    lea    0x2f5a(%rip),%rax        # 4018 <__TMC_END__>
    10be:       48 39 f8                cmp    %rdi,%rax
    10c1:       74 15                   je     10d8 <deregister_tm_clones+0x28>
    10c3:       48 8b 05 1e 2f 00 00    mov    0x2f1e(%rip),%rax        # 3fe8 <_ITM_deregisterTMCloneTable@Base>
    10ca:       48 85 c0                test   %rax,%rax
    10cd:       74 09                   je     10d8 <deregister_tm_clones+0x28>
    10cf:       ff e0                   jmp    *%rax
    10d1:       0f 1f 80 00 00 00 00    nopl   0x0(%rax)
    10d8:       c3                      ret
    10d9:       0f 1f 80 00 00 00 00    nopl   0x0(%rax)

00000000000010e0 <register_tm_clones>:
    10e0:       48 8d 3d 31 2f 00 00    lea    0x2f31(%rip),%rdi        # 4018 <__TMC_END__>
    10e7:       48 8d 35 2a 2f 00 00    lea    0x2f2a(%rip),%rsi        # 4018 <__TMC_END__>
    10ee:       48 29 fe                sub    %rdi,%rsi
    10f1:       48 89 f0                mov    %rsi,%rax
    10f4:       48 c1 ee 3f             shr    $0x3f,%rsi
    10f8:       48 c1 f8 03             sar    $0x3,%rax
    10fc:       48 01 c6                add    %rax,%rsi
    10ff:       48 d1 fe                sar    $1,%rsi
    1102:       74 14                   je     1118 <register_tm_clones+0x38>
    1104:       48 8b 05 ed 2e 00 00    mov    0x2eed(%rip),%rax        # 3ff8 <_ITM_registerTMCloneTable@Base>
    110b:       48 85 c0                test   %rax,%rax
    110e:       74 08                   je     1118 <register_tm_clones+0x38>
    1110:       ff e0                   jmp    *%rax
    1112:       66 0f 1f 44 00 00       nopw   0x0(%rax,%rax,1)
    1118:       c3                      ret
    1119:       0f 1f 80 00 00 00 00    nopl   0x0(%rax)

0000000000001120 <__do_global_dtors_aux>:
    1120:       f3 0f 1e fa             endbr64
    1124:       80 3d 25 30 00 00 00    cmpb   $0x0,0x3025(%rip)        # 4150 <completed.0>
    112b:       75 2b                   jne    1158 <__do_global_dtors_aux+0x38>
    112d:       55                      push   %rbp
    112e:       48 83 3d 9a 2e 00 00    cmpq   $0x0,0x2e9a(%rip)        # 3fd0 <__cxa_finalize@GLIBC_2.2.5>
    1135:       00 
    1136:       48 89 e5                mov    %rsp,%rbp
    1139:       74 0c                   je     1147 <__do_global_dtors_aux+0x27>
    113b:       48 8b 3d c6 2e 00 00    mov    0x2ec6(%rip),%rdi        # 4008 <__dso_handle>
    1142:       e8 09 ff ff ff          call   1050 <__cxa_finalize@plt>
    1147:       e8 64 ff ff ff          call   10b0 <deregister_tm_clones>
    114c:       c6 05 fd 2f 00 00 01    movb   $0x1,0x2ffd(%rip)        # 4150 <completed.0>
    1153:       5d                      pop    %rbp
    1154:       c3                      ret
    1155:       0f 1f 00                nopl   (%rax)
    1158:       c3                      ret
    1159:       0f 1f 80 00 00 00 00    nopl   0x0(%rax)

0000000000001160 <frame_dummy>:
    1160:       f3 0f 1e fa             endbr64
    1164:       e9 77 ff ff ff          jmp    10e0 <register_tm_clones>

0000000000001169 <_Z4fun3v>:
    1169:       f3 0f 1e fa             endbr64
    116d:       55                      push   %rbp
    116e:       48 89 e5                mov    %rsp,%rbp
    1171:       48 83 ec 10             sub    $0x10,%rsp
    1175:       48 8d 05 88 0e 00 00    lea    0xe88(%rip),%rax        # 2004 <_IO_stdin_used+0x4>
    117c:       48 89 c6                mov    %rax,%rsi
    117f:       48 8d 05 ba 2e 00 00    lea    0x2eba(%rip),%rax        # 4040 <_ZSt4cout@GLIBCXX_3.4>
    1186:       48 89 c7                mov    %rax,%rdi
    1189:       e8 d2 fe ff ff          call   1060 <_ZStlsISt11char_traitsIcEERSt13basic_ostreamIcT_ES5_PKc@plt>
    118e:       48 8b 15 43 2e 00 00    mov    0x2e43(%rip),%rdx        # 3fd8 <_ZSt4endlIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_@GLIBCXX_3.4>
    1195:       48 89 d6                mov    %rdx,%rsi
    1198:       48 89 c7                mov    %rax,%rdi
    119b:       e8 d0 fe ff ff          call   1070 <_ZNSolsEPFRSoS_E@plt>
    11a0:       c7 45 fc 01 00 00 00    movl   $0x1,-0x4(%rbp)
    11a7:       90                      nop
    11a8:       c9                      leave
    11a9:       c3                      ret

00000000000011aa <_Z4fun1v>:
    11aa:       f3 0f 1e fa             endbr64
    11ae:       55                      push   %rbp
    11af:       48 89 e5                mov    %rsp,%rbp
    11b2:       48 8d 05 50 0e 00 00    lea    0xe50(%rip),%rax        # 2009 <_IO_stdin_used+0x9>
    11b9:       48 89 c6                mov    %rax,%rsi
    11bc:       48 8d 05 7d 2e 00 00    lea    0x2e7d(%rip),%rax        # 4040 <_ZSt4cout@GLIBCXX_3.4>
    11c3:       48 89 c7                mov    %rax,%rdi
    11c6:       e8 95 fe ff ff          call   1060 <_ZStlsISt11char_traitsIcEERSt13basic_ostreamIcT_ES5_PKc@plt>
    11cb:       48 8b 15 06 2e 00 00    mov    0x2e06(%rip),%rdx        # 3fd8 <_ZSt4endlIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_@GLIBCXX_3.4>
    11d2:       48 89 d6                mov    %rdx,%rsi
    11d5:       48 89 c7                mov    %rax,%rdi
    11d8:       e8 93 fe ff ff          call   1070 <_ZNSolsEPFRSoS_E@plt>
    11dd:       90                      nop
    11de:       5d                      pop    %rbp
    11df:       c3                      ret

00000000000011e0 <_Z4fun2v>:
    11e0:       f3 0f 1e fa             endbr64
    11e4:       55                      push   %rbp
    11e5:       48 89 e5                mov    %rsp,%rbp
    11e8:       48 8d 05 1f 0e 00 00    lea    0xe1f(%rip),%rax        # 200e <_IO_stdin_used+0xe>
    11ef:       48 89 c6                mov    %rax,%rsi
    11f2:       48 8d 05 47 2e 00 00    lea    0x2e47(%rip),%rax        # 4040 <_ZSt4cout@GLIBCXX_3.4>
    11f9:       48 89 c7                mov    %rax,%rdi
    11fc:       e8 5f fe ff ff          call   1060 <_ZStlsISt11char_traitsIcEERSt13basic_ostreamIcT_ES5_PKc@plt>
    1201:       48 8b 15 d0 2d 00 00    mov    0x2dd0(%rip),%rdx        # 3fd8 <_ZSt4endlIcSt11char_traitsIcEERSt13basic_ostreamIT_T0_ES6_@GLIBCXX_3.4>
    1208:       48 89 d6                mov    %rdx,%rsi
    120b:       48 89 c7                mov    %rax,%rdi
    120e:       e8 5d fe ff ff          call   1070 <_ZNSolsEPFRSoS_E@plt>
    1213:       e8 51 ff ff ff          call   1169 <_Z4fun3v>
    1218:       90                      nop
    1219:       5d                      pop    %rbp
    121a:       c3                      ret

000000000000121b <main>:
    121b:       f3 0f 1e fa             endbr64
    121f:       55                      push   %rbp
    1220:       48 89 e5                mov    %rsp,%rbp
    1223:       48 83 ec 20             sub    $0x20,%rsp
    1227:       89 7d ec                mov    %edi,-0x14(%rbp)
    122a:       48 89 75 e0             mov    %rsi,-0x20(%rbp)
    122e:       c7 45 fc 01 00 00 00    movl   $0x1,-0x4(%rbp)
    1235:       e8 70 ff ff ff          call   11aa <_Z4fun1v>
    123a:       e8 a1 ff ff ff          call   11e0 <_Z4fun2v>
    123f:       b8 00 00 00 00          mov    $0x0,%eax
    1244:       c9                      leave
    1245:       c3                      ret

Disassembly of section .fini:

0000000000001248 <_fini>:
    1248:       f3 0f 1e fa             endbr64
    124c:       48 83 ec 08             sub    $0x8,%rsp
    1250:       48 83 c4 08             add    $0x8,%rsp
    1254:       c3                      ret