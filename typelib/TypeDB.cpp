//
// Created by sebastian on 22.03.18.
//

#include "TypeDB.h"

//#include <form.h> FIXME why needed?
#include <iostream>

namespace typeart {

std::string TypeDB::BuiltinNames[] = {"int8",   "int16",    "int32",       "int64",        "half",   "float",
                                      "double", "float128", "x86_float80", "ppc_float128", "unknown"};

// TODO: Builtin ID changes lead to wrong type sizes/names
size_t TypeDB::BuiltinSizes[] = {1,  2, 4, 8, 2, 4, 8, 16,
                                 16,  // TODO: Always correct?
                                 16, 0};

TypeInfo TypeDB::InvalidType = TypeInfo{BUILTIN, TA_UNKNOWN_TYPE};

std::string TypeDB::UnknownStructName{"UnknownStruct"};

TypeDB::TypeDB() = default;

void TypeDB::clear() {
  structInfoList.clear();
  id2Idx.clear();
  // reverseTypeMap.clear();
}

bool TypeDB::isBuiltinType(int id) const {
  return id < N_BUILTIN_TYPES;
}

bool TypeDB::isStructType(int id) const {
  return id2Idx.find(id) != id2Idx.end();
}

bool TypeDB::isValid(int id) const {
  if (isBuiltinType(id)) {
    return true;
  }
  return id2Idx.find(id) != id2Idx.end();
}

void TypeDB::registerStruct(StructTypeInfo structType) {
  if (isValid(structType.id)) {
    std::cerr << "Invalid type ID for struct " << structType.name << std::endl;
    if (isBuiltinType(structType.id)) {
      std::cerr << "Type ID is reserved for builtin types" << std::endl;
    } else {
      std::cerr << "Conflicting struct is " << getStructInfo(structType.id)->name << std::endl;
    }
    // TODO: Error handling
    return;
  }
  structInfoList.push_back(structType);
  id2Idx.insert({structType.id, structInfoList.size() - 1});
  // reverseTypeMap.insert({id, typeName});
}

const std::string& TypeDB::getTypeName(int id) const {
  if (isBuiltinType(id)) {
    return BuiltinNames[id];
  }
  if (isStructType(id)) {
    const auto* structInfo = getStructInfo(id);
    if (structInfo) {
      return structInfo->name;
    }
  }
  return UnknownStructName;
}

size_t TypeDB::getTypeSize(const TypeInfo& typeInfo) const {
  if (typeInfo.kind == BUILTIN) {
    return BuiltinSizes[typeInfo.id];
  }
  if (typeInfo.kind == POINTER) {
    return sizeof(void*);
  }
  const auto* structInfo = getStructInfo(typeInfo.id);
  if (structInfo) {
    return structInfo->extent;
  }
  return 0;
}

const StructTypeInfo* TypeDB::getStructInfo(int id) const {
  auto it = id2Idx.find(id);
  if (it != id2Idx.end()) {
    return &structInfoList[it->second];
  }
  return nullptr;
}

TypeInfo TypeDB::getTypeInfo(int id) const {
  if (isBuiltinType(id)) {
    return TypeInfo{BUILTIN, id};
  }
  if (isStructType(id)) {
    return TypeInfo{STRUCT, id};
  }
  return InvalidType;
}

const std::vector<StructTypeInfo>& TypeDB::getStructList() const {
  return structInfoList;
}

}  // namespace typeart
