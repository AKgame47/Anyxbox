// Anyxbox DXBC container reader and SM4/SM5 token decoder. GPL-2.0.
// Reads shader bytecode that a program hands to Direct3D. The container checksum is NOT verified
// (reported as such); nothing here bypasses any signing or protection, it only parses the format.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace anyxbox { namespace shader {

struct SigElement { std::string name; uint32_t index = 0, sysval = 0, comp_type = 0, reg = 0; uint8_t mask = 0, rw_mask = 0; };
struct DxbcContainer {
  bool ok = false;
  std::string error;
  std::vector<std::string> chunk_names;
  std::vector<SigElement> inputs, outputs;     // ISGN / OSGN
  const uint8_t* shader = nullptr;             // SHDR or SHEX chunk payload (points into the input)
  size_t shader_size = 0;                      // bytes
  bool checksum_verified = false;              // always false for now
};
DxbcContainer parse_dxbc(const uint8_t* data, size_t size);

// ---- SM4/SM5 token stream ----
enum Op : uint32_t {
  OP_ADD = 0, OP_AND = 1, OP_BREAK = 2, OP_BREAKC = 3, OP_CONTINUE = 7, OP_CONTINUEC = 8, OP_DISCARD = 13,
  OP_ELSE = 18, OP_ENDIF = 21, OP_ENDLOOP = 22, OP_EQ = 24, OP_FTOI = 27, OP_GE = 29, OP_IADD = 30, OP_IF = 31,
  OP_IEQ = 32, OP_IGE = 33, OP_ILT = 34, OP_INE = 39, OP_INEG = 40, OP_ITOF = 43, OP_LOOP = 48, OP_LT = 49,
  OP_MOVC = 55, OP_NE = 57, OP_NOT = 59, OP_OR = 60, OP_RETC = 63, OP_XOR = 87, OP_DIV = 14, OP_DP2 = 15, OP_DP3 = 16, OP_DP4 = 17, OP_EXP = 25, OP_FRC = 26, OP_LOG = 47,
  OP_MAD = 50, OP_MIN = 51, OP_MAX = 52, OP_CUSTOMDATA = 53, OP_MOV = 54, OP_MUL = 56, OP_RET = 62,
  OP_RSQ = 68, OP_SQRT = 75,
  OP_SAMPLE = 69, OP_SAMPLE_L = 72, OP_DCL_RESOURCE = 88, OP_DCL_SAMPLER = 90,
  OP_DCL_CONSTANT_BUFFER = 89, OP_DCL_INPUT = 95, OP_DCL_INPUT_SGV = 96, OP_DCL_INPUT_SIV = 97, OP_DCL_INPUT_PS = 98,
  OP_DCL_INPUT_PS_SGV = 99, OP_DCL_INPUT_PS_SIV = 100, OP_DCL_OUTPUT = 101, OP_DCL_OUTPUT_SGV = 102,
  OP_DCL_OUTPUT_SIV = 103, OP_DCL_TEMPS = 104, OP_DCL_GLOBAL_FLAGS = 106
};
enum OperandType : uint32_t { OT_TEMP = 0, OT_INPUT = 1, OT_OUTPUT = 2, OT_IMM32 = 4, OT_SAMPLER = 6, OT_RESOURCE = 7, OT_CBUFFER = 8 };
enum SelMode : uint32_t { SEL_MASK = 0, SEL_SWIZZLE = 1, SEL_SELECT1 = 2 };

struct Operand {
  uint32_t type = 0, ncomp = 0, selmode = 0, index_dim = 0;
  uint8_t mask = 0;            // SEL_MASK: bit i = component i
  uint8_t swizzle[4] = {0, 1, 2, 3};   // SEL_SWIZZLE (and SEL_SELECT1: all four = the selected component)
  bool neg = false, abs = false;
  uint32_t index[3] = {0, 0, 0};
  uint32_t imm[4] = {0, 0, 0, 0};
};
struct Instruction {
  uint32_t opcode = 0;
  bool saturate = false;
  uint32_t control = 0;         // opcode-token bits 11..23 (interpolation mode for dcl_input_ps)
  uint32_t value = 0;           // dcl_temps count, or system-value id for *_SIV / *_SGV declarations
  std::vector<Operand> ops;
};
struct Program {
  bool ok = false;
  std::string error;
  uint32_t type = 0;            // 0 pixel, 1 vertex, 2 geometry, 3 hull, 4 domain, 5 compute
  uint32_t major = 0, minor = 0;
  std::vector<Instruction> instructions;
};
Program decode_shader(const uint8_t* shdr, size_t size);
}}
