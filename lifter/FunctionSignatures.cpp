#include "FunctionSignatures.h"
#include "GEPTracker.h"
#include <iostream>
#include <llvm/Transforms/Utils/SCCPSolver.h>
#include <queue>

namespace funcsignatures {
  // TODO: this is ugly

  std::unordered_map<uint64_t, functioninfo> functions;
  std::unordered_map<std::string, functioninfo> functionsByName{
      {"MessageBoxW", functioninfo("MessageBoxW",
                                   {
                                       funcArgInfo(ZYDIS_REGISTER_RCX, I64, 0),
                                       funcArgInfo(ZYDIS_REGISTER_RDX, I64, 1),
                                       funcArgInfo(ZYDIS_REGISTER_R8, I64, 1),
                                       funcArgInfo(ZYDIS_REGISTER_R9, I64, 0),
                                   })},
      {"GetTickCount64", functioninfo("GetTickCount64", {})},
  };

  void createOffsetMap() {
    for (auto value : siglookup) {
      for (auto offsets : value.second.offsets) {
        functions[offsets] = value.second;
      }
      functionsByName[value.second.name] = value.second;
    }
  }

  functioninfo* getFunctionInfo(uint64_t addr) {
    if (functions.count(addr) == 0)
      return nullptr;
    return &(functions[addr]);
  }

  functioninfo* getFunctionInfo(const std::string& name) {
    if (functionsByName.count(name) == 0)
      return nullptr;
    return &(functionsByName[name]);
  }

  void functioninfo::add_offset(uint64_t offset) {
    offsets.push_back(BinaryOperations::fileOffsetToRVA(offset));
  }

  void functioninfo::display() const {
    std::cout << "Function Name: " << name << ", Offsets: ";
    for (const auto& offset : offsets) {
      std::cout << offset << " ";
    }
    std::cout << "end" << std::endl;
  }

  std::unordered_map<std::vector<unsigned char>, functioninfo, VectorHash>
      siglookup{
          {{0x55, 0x48, 0x81, 0xEC, 0xA0, 00, 00, 00, 0x48, 0x8D, 0xAC, 0x24,
            0x80, 00, 00, 00},
           functioninfo("??$?6U?$char_traits@D@std@@@std@@YAAEAV?$basic_"
                        "ostream@DU?$char_traits@D@std@@@0@AEAV10@PEBD@Z")},

          {{0x4C, 0x8B, 0xDC, 0x4D, 0x89, 0x43, 0x18, 0x4D, 0x89, 0x4B, 0x20,
            0x48, 0x83, 0xEC, 0x38},
           functioninfo("swprintf_s",
                        {
                            funcArgInfo(ZYDIS_REGISTER_RCX, I64, 1),
                            funcArgInfo(ZYDIS_REGISTER_RDX, I64, 0),
                            funcArgInfo(ZYDIS_REGISTER_R8, I64, 1),
                            funcArgInfo(ZYDIS_REGISTER_R9, I64, 0),
                        })}};

  AhoCorasick::AhoCorasick(
      const std::unordered_map<std::vector<unsigned char>, functioninfo,
                               VectorHash>& patterns_map) {
    trie.emplace_back();
    int id = 0;
    for (const auto& [pattern, _] : patterns_map) {
      int current = 0;
      for (unsigned char c : pattern) {
        if (trie[current].children.count(c) == 0) {
          trie[current].children[c] = trie.size();
          trie.emplace_back();
        }
        current = trie[current].children[c];
      }
      trie[current].output.push_back(id);
      patterns[id++] = pattern;
    }
    build();
  }

  void AhoCorasick::build() {
    std::queue<int> q;
    for (const auto& [c, next] : trie[0].children) {
      trie[next].fail = 0;
      q.push(next);
    }
    while (!q.empty()) {
      int current = q.front();
      q.pop();
      for (const auto& [c, next] : trie[current].children) {
        int fail = trie[current].fail;
        while (fail != -1 && trie[fail].children.count(c) == 0) {
          fail = trie[fail].fail;
        }
        if (fail != -1) {
          trie[next].fail = trie[fail].children[c];
        } else {
          trie[next].fail = 0;
        }
        trie[next].output.insert(trie[next].output.end(),
                                 trie[trie[next].fail].output.begin(),
                                 trie[trie[next].fail].output.end());
        q.push(next);
      }
    }
  }

  std::vector<std::pair<int, int>>
  AhoCorasick::search(const std::vector<unsigned char>& text) {
    std::vector<std::pair<int, int>> results;
    int current = 0;
    for (uint64_t i = 0; i < text.size(); ++i) {
      while (current != -1 && trie[current].children.count(text[i]) == 0) {
        current = trie[current].fail;
      }
      if (current == -1) {
        current = 0;
        continue;
      }
      current = trie[current].children[text[i]];
      for (int id : trie[current].output) {
        results.emplace_back(i - patterns[id].size() + 1, id);
      }
    }
    return results;
  }

  std::unordered_map<std::vector<unsigned char>, functioninfo, VectorHash>
  search_signatures(const std::vector<unsigned char>& data) {
    AhoCorasick ac(siglookup);
    std::vector<std::pair<int, int>> matches = ac.search(data);
    for (const auto& [pos, id] : matches) {
      auto it = siglookup.find(ac.patterns[id]);
      if (it != siglookup.end()) {
        it->second.add_offset(pos);
      }
    }
    return siglookup;
  }

  std::vector<unsigned char> convertToVector(const unsigned char* data,
                                             size_t size) {
    return std::vector<unsigned char>(data, data + size);
  }
} // namespace funcsignatures
