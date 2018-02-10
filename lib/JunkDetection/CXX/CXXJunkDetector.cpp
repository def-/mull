#include "JunkDetection/CXX/CXXJunkDetector.h"
#include "MutationPoint.h"

#include "Logger.h"

#include <iostream>

#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/DebugLoc.h>
#include <llvm/IR/Instruction.h>

using namespace mull;
using namespace llvm;
using namespace std;

ostream& operator<<(ostream& stream, const CXString& str) {
  stream << clang_getCString(str);
  clang_disposeString(str);
  return stream;
}

void dump_cursor(CXCursor cursor, CXSourceLocation location, PhysicalAddress &address) {
  CXCursorKind kind = clang_getCursorKind(cursor);
  cout << "Kind '" << clang_getCursorKindSpelling(kind) << "'\n";

  CXSourceRange range = clang_getCursorExtent(cursor);
  CXSourceLocation begin = clang_getRangeStart(range);
  CXSourceLocation end = clang_getRangeEnd(range);

  unsigned int beginOffset = 0;
  unsigned int endOffset = 0;
  unsigned int origOffset = 0;
  clang_getFileLocation(begin, nullptr, nullptr, nullptr, &beginOffset);
  clang_getFileLocation(end, nullptr, nullptr, nullptr, &endOffset);
  clang_getFileLocation(location, nullptr, nullptr, nullptr, &origOffset);

  unsigned int offset = origOffset - beginOffset;

  auto length = endOffset - beginOffset;

  FILE *f = fopen(address.filepath.c_str(), "rb");

  fseek(f, beginOffset, SEEK_SET);
  char *buffer = new char[length + 1];
  fread(buffer, sizeof(char), length, f);

  buffer[length] = '\0';
  cout << buffer << "\n";

  for (unsigned int i = 0; i < offset; i++) {
    printf(" ");
  }
  printf("^\n");

  delete[] buffer;
}

PhysicalAddress getAddress(MutationPoint *point) {
  PhysicalAddress address;

  if (auto instruction = dyn_cast<Instruction>(point->getOriginalValue())) {
    if (instruction->getMetadata(0)) {
      auto debugInfo = instruction->getDebugLoc();
      address.filepath = debugInfo->getFilename().str();
      address.line = debugInfo->getLine();
      address.column = debugInfo->getColumn();
    }
  }

  return address;
}

CXXJunkDetector::CXXJunkDetector() : index(clang_createIndex(true, true)) {

}

CXXJunkDetector::~CXXJunkDetector() {
  for (auto &pair : units) {
    clang_disposeTranslationUnit(pair.second);
  }
  clang_disposeIndex(index);
}

pair<CXCursor, CXSourceLocation> CXXJunkDetector::cursorAndLocation(PhysicalAddress &address) {
  if (units.count(address.filepath) == 0) {
    const char *argv[] = { "-x", "c++", nullptr };
    const int argc = sizeof(argv) / sizeof(argv[0]) - 1;
    CXTranslationUnit unit = clang_parseTranslationUnit(index,
                                                        address.filepath.c_str(),
                                                        argv, argc,
                                                        nullptr, 0,
                                                        CXTranslationUnit_KeepGoing);
    if (unit == nullptr) {
      Logger::error() << "Cannot parse translation unit: " << address.filepath << "\n";
      return make_pair(clang_getNullCursor(), clang_getNullLocation());
    }
    units[address.filepath] = unit;
  }

  CXTranslationUnit unit = units[address.filepath];
  if (unit == nullptr) {
    return make_pair(clang_getNullCursor(), clang_getNullLocation());
  }

  CXFile file = clang_getFile(unit, address.filepath.c_str());
  if (file == nullptr) {
    Logger::error() << "Cannot get file from TU: " << address.filepath << "\n";
    return make_pair(clang_getNullCursor(), clang_getNullLocation());
  }

  CXSourceLocation location = clang_getLocation(unit, file, address.line, address.column);

  return make_pair(clang_getCursor(unit, location), location);
}

bool CXXJunkDetector::isJunk(MutationPoint *point) {
  auto address = getAddress(point);
  if (!address.valid()) {
    return true;
  }

  auto pair = cursorAndLocation(address);
  auto cursor = pair.first;
  auto location = pair.second;

  if (clang_Cursor_isNull(cursor)) {
    return true;
  }

  CXCursorKind kind = clang_getCursorKind(cursor);
  if (kind != CXCursor_BinaryOperator && kind != CXCursor_UnaryOperator && kind != CXCursor_CompoundAssignOperator) {
    return true;
  } else {
    unsigned int mutationOffset = 0;
    clang_getFileLocation(location, nullptr, nullptr, nullptr, &mutationOffset);
    FILE *f = fopen(address.filepath.c_str(), "rb");
    char symbol;
    fseek(f, mutationOffset, SEEK_SET);
    fread(&symbol, sizeof(char), 1, f);
    fclose(f);

    if (symbol != '+' && symbol != '-') {
      return true;
    }

    return false;
  }

  return false;
}
