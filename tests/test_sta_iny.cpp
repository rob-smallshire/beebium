#include <catch2/catch_test_macros.hpp>
#include <6502/6502.h>
#include <cstring>

// Minimal test of STA ($00),Y - correctly following the library's expected usage
TEST_CASE("STA ($00),Y advances PC by 2", "[6502][minimal]") {
    M6502 cpu;
    M6502_Init(&cpu, &M6502_nmos6502_config);

    // Simple memory
    uint8_t mem[65536] = {0};

    // Set up code at $1000:
    // $1000: STA ($10),Y  ; opcode $91, operand $10
    // $1002: NOP          ; opcode $EA
    mem[0x1000] = 0x91;  // STA (zp),Y
    mem[0x1001] = 0x10;  // zero page address
    mem[0x1002] = 0xEA;  // NOP (next instruction)

    // Zero page pointer at $10-$11 points to $2000
    mem[0x0010] = 0x00;  // low byte
    mem[0x0011] = 0x20;  // high byte

    // Set CPU state
    cpu.pc.w = 0x1000;
    cpu.a = 0x42;  // Value to store
    cpu.y = 0x05;  // Y offset
    cpu.s.w = 0x01FF;  // Stack pointer
    cpu.p.value = 0x24;  // Typical flags
    cpu.d1x1 = 1;  // Not in interrupt

    // Call M6502_NextInstruction to set up the opcode fetch
    M6502_NextInstruction(&cpu);

    printf("After NextInstruction: PC=$%04X abus=$%04X read=%d\n",
           cpu.pc.w, cpu.abus.w, cpu.read);

    // Verify state: ready to fetch opcode
    REQUIRE(cpu.read == 5);  // M6502ReadType_Opcode
    REQUIRE(cpu.abus.w == 0x1000);

    // Prime dbus with opcode (this is what the previous cycle's memory access would have done)
    cpu.dbus = mem[cpu.abus.w];  // $91

    // The library's cycle model:
    // 1. M6502_NextInstruction sets abus and read for opcode fetch
    // 2. Caller provides data on dbus
    // 3. Caller calls tfn which processes this cycle and sets up the next
    // 4. Repeat from step 2

    // Use same order as Klaus test: tfn THEN memory
    int cycles = 0;
    do {
        // Call tfn to advance state (uses previous dbus, sets up next abus/read)
        (*cpu.tfn)(&cpu);
        cycles++;

        // Then: do memory access for the access just set up
        if (cpu.read) {
            cpu.dbus = mem[cpu.abus.w];
            printf("Cycle %d: READ  $%04X -> $%02X  PC=$%04X\n",
                   cycles, cpu.abus.w, cpu.dbus, cpu.pc.w);
        } else {
            mem[cpu.abus.w] = cpu.dbus;
            printf("Cycle %d: WRITE $%04X <- $%02X  PC=$%04X\n",
                   cycles, cpu.abus.w, cpu.dbus, cpu.pc.w);
        }

        if (cycles > 10) {
            printf("ERROR: Too many cycles!\n");
            break;
        }
    } while (!M6502_IsAboutToExecute(&cpu));

    printf("\nAfter STA: PC=$%04X, cycles=%d\n", cpu.pc.w, cycles);
    printf("mem[$2005]=$%02X (expected $42)\n", mem[0x2005]);

    REQUIRE(cpu.pc.w == 0x1002);
    REQUIRE(mem[0x2005] == 0x42);
    REQUIRE(cycles == 6);
}
